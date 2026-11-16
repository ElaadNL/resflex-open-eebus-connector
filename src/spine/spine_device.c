// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_device.c
 *
 * spine_device_t implementation: state data center + dispatch/send/notify.
 *
 * Design principles:
 *   - get_local(dev) uniformly retrieves local data: local->xxx or dev->xxx (when self is local)
 *   - dispatch looks up get_local(dev)->features[], passing dev itself to the handler
 *   - send routes directly via dev->ski (dev must be remote)
 *   - notify iterates all remotes via router, checks each remote's sub_bind
 */

#include "spine_device_internal.h"
#include "spine_node_mgmt.h"
#include "codec/spine_codec_datagram.h"
#include "codec/spine_codec_result.h"
#include "spine_router.h"
#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#define LOG_TAG "spine.dev"

/* SPINE §5.2.5.3: defaultMaxResponseDelay = 10 seconds */
#define SPINE_RESPONSE_TIMEOUT_MS  10000

/* Monotonic clock in milliseconds (immune to wall-clock adjustments). */
static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════════════════════ */

static spine_device_t* alloc_device(void) {
    spine_device_t* dev = EEBUS_NEW(spine_device_t);
    if (!dev) return NULL;
    dev->is_remote      = false;
    dev->local          = NULL;
    dev->router         = NULL;
    dev->entity_count   = 0;
    dev->feature_count  = 0;
    dev->uc_entry_count = 0;
    dev->peer_ready_count = 0;
    dev->disc_state     = SPINE_DISC_CREATED;
    dev->nm             = NULL;
    dev->sub_bind       = NULL;
    dev->device_id[0]   = '\0';
    dev->ski[0]         = '\0';
    dev->device_type[0] = '\0';
    dev->session_id[0]  = '\0';
    dev->msg_counter    = 1;
    for (uint8_t i = 0; i < SPINE_PENDING_MAX; i++)
        dev->pending[i].in_use = false;
    for (uint8_t i = 0; i < SPINE_CACHE_SLOT_MAX; i++) {
        dev->cache[i].data = NULL;
        dev->cache[i].destroy = NULL;
    }
    if (eebus_mutex_create(&dev->mutex) != EEBUS_OK) {
        EEBUS_FREE(dev);
        return NULL;
    }
    return dev;
}

spine_err_t spine_device_create_local(spine_device_t** out,
                                       const char*      device_id,
                                       const char*      ski,
                                       const char*      device_type) {
    if (!out) return SPINE_ERR_INVALID;

    spine_device_t* dev = alloc_device();
    if (!dev) return SPINE_ERR_NO_MEMORY;

    dev->is_remote = false;
    dev->local     = NULL;   /* local device's own local pointer is NULL */

    snprintf(dev->device_id,   sizeof(dev->device_id),   "%s",
             device_id   ? device_id   : "");
    snprintf(dev->ski,         sizeof(dev->ski),          "%s",
             ski         ? ski         : "");
    snprintf(dev->device_type, sizeof(dev->device_type),  "%s",
             device_type ? device_type : "Generic");

    /* Auto-register entity [0] "DeviceInformation" (SPINE §7.1.2) */
    {
        uint8_t e0 = 0;
        spine_device_add_entity(dev, &e0, 1, "DeviceInformation");
    }

    /* sub_bind: owned by local device */
    if (spine_sub_bind_create(&dev->sub_bind) != SPINE_OK) {
        eebus_mutex_destroy(dev->mutex);
        EEBUS_FREE(dev);
        return SPINE_ERR_NO_MEMORY;
    }

    /* nm: register nm_handler into local->features[] (is_remote=false) */
    if (spine_node_mgmt_create(&dev->nm, dev) != SPINE_OK) {
        spine_sub_bind_destroy(dev->sub_bind);
        eebus_mutex_destroy(dev->mutex);
        EEBUS_FREE(dev);
        return SPINE_ERR_NO_MEMORY;
    }

    *out = dev;
    return SPINE_OK;
}

spine_err_t spine_device_create_remote(spine_device_t** out,
                                        const char*      ski,
                                        spine_router_t*  router,
                                        spine_device_t*  local,
                                        const char*      session_id) {
    if (!out || !ski || !router || !local) return SPINE_ERR_INVALID;

    spine_device_t* dev = alloc_device();
    if (!dev) return SPINE_ERR_NO_MEMORY;

    dev->is_remote = true;
    dev->local     = local;
    dev->router    = router;

    snprintf(dev->ski,        sizeof(dev->ski),        "%s", ski);
    snprintf(dev->session_id, sizeof(dev->session_id), "%s",
             session_id ? session_id : "");

    /* sub_bind: each remote owns its own subscription/binding relationships */
    if (spine_sub_bind_create(&dev->sub_bind) != SPINE_OK) {
        eebus_mutex_destroy(dev->mutex);
        EEBUS_FREE(dev);
        return SPINE_ERR_NO_MEMORY;
    }

    /* nm: is_remote=true, do not register handler, only initialize state machine */
    if (spine_node_mgmt_create(&dev->nm, dev) != SPINE_OK) {
        spine_sub_bind_destroy(dev->sub_bind);
        eebus_mutex_destroy(dev->mutex);
        EEBUS_FREE(dev);
        return SPINE_ERR_NO_MEMORY;
    }

    *out = dev;
    return SPINE_OK;
}

void spine_device_destroy(spine_device_t* dev) {
    if (!dev) return;
    for (uint8_t i = 0; i < SPINE_CACHE_SLOT_MAX; i++) {
        if (dev->cache[i].data && dev->cache[i].destroy)
            dev->cache[i].destroy(dev->cache[i].data);
    }
    if (dev->nm)       spine_node_mgmt_destroy(dev->nm);
    if (dev->sub_bind) spine_sub_bind_destroy(dev->sub_bind);
    eebus_mutex_destroy(dev->mutex);
    EEBUS_FREE(dev);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Entity registration
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_add_entity(spine_device_t*  dev,
                                     const uint8_t*   entity_path,
                                     uint8_t          entity_depth,
                                     const char*      entity_type) {
    if (!dev || !entity_path || entity_depth == 0 || !entity_type)
        return SPINE_ERR_INVALID;
    if (dev->entity_count >= SPINE_ENTITY_MAX)
        return SPINE_ERR_OVERFLOW;

    /* Check for duplicate entity path */
    for (uint8_t i = 0; i < dev->entity_count; i++) {
        spine_entity_info_t* e = &dev->entities[i];
        if (e->entity_depth == entity_depth) {
            bool match = true;
            for (uint8_t j = 0; j < entity_depth; j++) {
                if (e->entity[j] != entity_path[j]) { match = false; break; }
            }
            if (match) return SPINE_OK; /* already registered */
        }
    }

    spine_entity_info_t* e = &dev->entities[dev->entity_count++];
    memset(e, 0, sizeof(*e));
    uint8_t d = (entity_depth < SPINE_ENTITY_DEPTH_MAX)
                ? entity_depth : SPINE_ENTITY_DEPTH_MAX;
    for (uint8_t i = 0; i < d; i++) e->entity[i] = entity_path[i];
    e->entity_depth = d;
    snprintf(e->entity_type, sizeof(e->entity_type), "%s", entity_type);
    return SPINE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Feature registration
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_add_feature(spine_device_t*         dev,
                                      const spine_addr_t*     addr,
                                      const char*             feature_type,
                                      const char*             role,
                                      spine_feature_handler_t handler,
                                      void*                   userdata) {
    if (!dev || !addr || !feature_type || !role) return SPINE_ERR_INVALID;

    /* Idempotent: check for duplicate feature address */
    for (uint8_t i = 0; i < dev->feature_count; i++) {
        if (spine_addr_equal(&dev->features[i].info.addr, addr))
            return SPINE_OK;
    }

    if (dev->feature_count >= SPINE_FEATURE_MAX)  return SPINE_ERR_OVERFLOW;

    feature_slot_t* s = &dev->features[dev->feature_count++];
    memset(s, 0, sizeof(*s));
    s->info.addr = *addr;
    snprintf(s->info.feature_type, sizeof(s->info.feature_type),
             "%s", feature_type);
    snprintf(s->info.role, sizeof(s->info.role), "%s", role);
    s->info.supported_fn_count = 0;
    s->handler  = handler;
    s->userdata = userdata;
    return SPINE_OK;
}

spine_err_t spine_device_add_feature_function(spine_device_t*      dev,
                                               const spine_addr_t*  feature_addr,
                                               const char*          function_name,
                                               bool read_present,
                                               bool read_partial,
                                               bool write_present,
                                               bool write_partial) {
    if (!dev || !feature_addr || !function_name) return SPINE_ERR_INVALID;

    /* Find the feature slot */
    for (uint8_t i = 0; i < dev->feature_count; i++) {
        feature_slot_t* s = &dev->features[i];
        if (spine_addr_equal(&s->info.addr, feature_addr)) {
            /* Idempotent: check for duplicate function name */
            for (uint8_t j = 0; j < s->info.supported_fn_count; j++) {
                if (strcmp(s->info.supported_fns[j].function_name,
                           function_name) == 0)
                    return SPINE_OK;
            }
            if (s->info.supported_fn_count >= SPINE_SUPPORTED_FN_MAX)
                return SPINE_ERR_OVERFLOW;
            spine_supported_fn_t* fn =
                &s->info.supported_fns[s->info.supported_fn_count++];
            snprintf(fn->function_name, sizeof(fn->function_name),
                     "%s", function_name);
            fn->read_present  = read_present;
            fn->read_partial  = read_partial;
            fn->write_present = write_present;
            fn->write_partial = write_partial;
            return SPINE_OK;
        }
    }
    return SPINE_ERR_NOT_FOUND;
}

const spine_feature_info_t* spine_device_find_feature(
        const spine_device_t* dev, const spine_addr_t* addr) {
    if (!dev || !addr) return NULL;
    for (uint8_t i = 0; i < dev->feature_count; i++)
        if (spine_addr_equal(&dev->features[i].info.addr, addr))
            return &dev->features[i].info;
    return NULL;
}

const spine_feature_info_t* spine_device_get_feature(
        const spine_device_t* dev, uint8_t idx) {
    if (!dev || idx >= dev->feature_count) return NULL;
    return &dev->features[idx].info;
}

uint8_t spine_device_feature_count(const spine_device_t* dev) {
    return dev ? dev->feature_count : 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Use Case metadata
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_add_usecase(spine_device_t*            dev,
                                      const spine_nm_uc_entry_t* entry) {
    if (!dev || !entry) return SPINE_ERR_INVALID;

    /* Idempotent: check for duplicate use case (same name + actor) */
    for (uint8_t i = 0; i < dev->uc_entry_count; i++) {
        if (strcmp(dev->uc_entries[i].use_case_name, entry->use_case_name) == 0 &&
            strcmp(dev->uc_entries[i].actor, entry->actor) == 0)
            return SPINE_OK;
    }

    if (dev->uc_entry_count >= SPINE_NM_UC_MAX) return SPINE_ERR_OVERFLOW;
    dev->uc_entries[dev->uc_entry_count++] = *entry;
    return SPINE_OK;
}

const spine_nm_uc_entry_t* spine_device_get_usecases(
        const spine_device_t* dev, uint8_t* count_out) {
    if (!dev) { if (count_out) *count_out = 0; return NULL; }
    if (count_out) *count_out = dev->uc_entry_count;
    return dev->uc_entries;
}

/* ════════════════════════════════════════════════════════════════════════════
 * peer_ready callback (registered on local device)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_add_peer_ready_cb(spine_device_t*    dev,
                                             spine_peer_ready_fn fn,
                                             void*               ud) {
    if (!dev || !fn) return SPINE_ERR_INVALID;
    if (dev->peer_ready_count >= SPINE_PEER_READY_MAX) return SPINE_ERR_OVERFLOW;
    dev->peer_ready_cbs[dev->peer_ready_count].fn = fn;
    dev->peer_ready_cbs[dev->peer_ready_count].ud = ud;
    dev->peer_ready_count++;
    return SPINE_OK;
}

spine_err_t spine_device_add_peer_changed_cb(spine_device_t*      dev,
                                               spine_peer_changed_fn fn,
                                               void*                 ud) {
    if (!dev || !fn) return SPINE_ERR_INVALID;
    if (dev->peer_changed_count >= SPINE_PEER_READY_MAX) return SPINE_ERR_OVERFLOW;
    dev->peer_changed_cbs[dev->peer_changed_count].fn = fn;
    dev->peer_changed_cbs[dev->peer_changed_count].ud = ud;
    dev->peer_changed_count++;
    return SPINE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * pending operations (internal, declared in internal.h)
 * ════════════════════════════════════════════════════════════════════════════ */

uint64_t spine_device_pending_add(spine_device_t*      dev,
                                   spine_pending_type_t type) {
    if (!dev) return 0;
    eebus_mutex_lock(dev->mutex);
    uint64_t counter = dev->msg_counter++;
    uint64_t now = monotonic_ms();
    for (uint8_t i = 0; i < SPINE_PENDING_MAX; i++) {
        /* Expire stale entries inline (SPINE §5.2.5.3) */
        if (dev->pending[i].in_use &&
            (now - dev->pending[i].created_ms) > SPINE_RESPONSE_TIMEOUT_MS) {
            EEBUS_LOG_WARN(LOG_TAG, "%s pending #%llu timed out",
                           dev->session_id,
                           (unsigned long long)dev->pending[i].counter);
            dev->pending[i].in_use = false;
        }
        if (!dev->pending[i].in_use) {
            dev->pending[i].in_use      = true;
            dev->pending[i].counter     = counter;
            dev->pending[i].type        = type;
            dev->pending[i].created_ms  = now;
            eebus_mutex_unlock(dev->mutex);
            return counter;
        }
    }
    eebus_mutex_unlock(dev->mutex);
    EEBUS_LOG_WARN(LOG_TAG, "pending table full");
    return counter;
}

uint64_t spine_device_pending_add_sb(spine_device_t*      dev,
                                      spine_pending_type_t type,
                                      const spine_addr_t*  client,
                                      const spine_addr_t*  server) {
    if (!dev) return 0;
    eebus_mutex_lock(dev->mutex);
    uint64_t counter = dev->msg_counter++;
    uint64_t now = monotonic_ms();
    for (uint8_t i = 0; i < SPINE_PENDING_MAX; i++) {
        /* Expire stale entries inline (SPINE §5.2.5.3) */
        if (dev->pending[i].in_use &&
            (now - dev->pending[i].created_ms) > SPINE_RESPONSE_TIMEOUT_MS) {
            EEBUS_LOG_WARN(LOG_TAG, "%s pending #%llu timed out",
                           dev->session_id,
                           (unsigned long long)dev->pending[i].counter);
            dev->pending[i].in_use = false;
        }
        if (!dev->pending[i].in_use) {
            dev->pending[i].in_use      = true;
            dev->pending[i].counter     = counter;
            dev->pending[i].type        = type;
            dev->pending[i].created_ms  = now;
            if (client) dev->pending[i].client_addr = *client;
            if (server) dev->pending[i].server_addr = *server;
            eebus_mutex_unlock(dev->mutex);
            return counter;
        }
    }
    eebus_mutex_unlock(dev->mutex);
    EEBUS_LOG_WARN(LOG_TAG, "pending table full");
    return counter;
}

spine_pending_type_t spine_device_pending_consume(spine_device_t* dev,
                                                   uint64_t        counter,
                                                   spine_addr_t*   client_out,
                                                   spine_addr_t*   server_out) {
    if (!dev) return SPINE_PENDING_NONE;
    eebus_mutex_lock(dev->mutex);
    for (uint8_t i = 0; i < SPINE_PENDING_MAX; i++) {
        if (dev->pending[i].in_use && dev->pending[i].counter == counter) {
            spine_pending_type_t t = dev->pending[i].type;
            if (client_out) *client_out = dev->pending[i].client_addr;
            if (server_out) *server_out = dev->pending[i].server_addr;
            dev->pending[i].in_use = false;
            eebus_mutex_unlock(dev->mutex);
            return t;
        }
    }
    eebus_mutex_unlock(dev->mutex);
    return SPINE_PENDING_NONE;
}

/* ════════════════════════════════════════════════════════════════════════════
 * router internal interface
 * ════════════════════════════════════════════════════════════════════════════ */

void spine_device_set_router(spine_device_t* dev, spine_router_t* router) {
    if (dev) dev->router = router;
}

/* ── Binding exclusivity check ───────────────────────────────────────────── */

static void bound_check_cb(spine_device_t* remote, void* ud) {
    void** args = (void**)ud;
    const spine_addr_t* server = (const spine_addr_t*)args[0];
    spine_device_t* exclude    = (spine_device_t*)args[1];
    bool* found                = (bool*)args[2];
    if (*found || remote == exclude || !remote->sub_bind) return;
    uint16_t count = spine_sub_bind_binding_count(remote->sub_bind);
    for (uint16_t i = 0; i < count; i++) {
        const spine_binding_t* b = spine_sub_bind_get_binding(remote->sub_bind, i);
        if (b && spine_addr_equal(&b->server_addr, server)) {
            *found = true;
            return;
        }
    }
}

bool spine_device_is_server_bound_by_other(spine_device_t*     dev,
                                            const spine_addr_t* server_addr) {
    if (!dev || !dev->router || !server_addr) return false;
    bool found = false;
    void* args[3] = { (void*)server_addr, dev, &found };
    spine_router_foreach_remote(dev->router, bound_check_cb, args);
    return found;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Accessor
 * ════════════════════════════════════════════════════════════════════════════ */

const char* spine_device_get_id(const spine_device_t* dev) {
    return dev ? dev->device_id : "";
}

const char* spine_device_get_ski(const spine_device_t* dev) {
    return dev ? dev->ski : "";
}

bool spine_device_is_remote(const spine_device_t* dev) {
    return dev ? dev->is_remote : false;
}

bool spine_device_has_binding(const spine_device_t* dev,
                               const spine_addr_t*   client,
                               const spine_addr_t*   server) {
    if (!dev || !dev->sub_bind) return false;
    return spine_sub_bind_has_binding(dev->sub_bind, client, server);
}

bool spine_device_has_binding_to_server(const spine_device_t* dev,
                                         const spine_addr_t*   server) {
    if (!dev || !dev->sub_bind) return false;
    return spine_sub_bind_has_binding_to_server(dev->sub_bind, server);
}

bool spine_device_has_subscription(const spine_device_t* dev,
                                    const spine_addr_t*   client,
                                    const spine_addr_t*   server) {
    if (!dev || !dev->sub_bind) return false;
    return spine_sub_bind_has_subscription(dev->sub_bind, client, server);
}

uint64_t spine_device_next_msg_counter(spine_device_t* dev) {
    if (!dev) return 0;
    eebus_mutex_lock(dev->mutex);
    uint64_t c = dev->msg_counter++;
    eebus_mutex_unlock(dev->mutex);
    return c;
}

void spine_device_set_session_id(spine_device_t* dev, const char* sid) {
    if (!dev) return;
    snprintf(dev->session_id, sizeof(dev->session_id), "%s", sid ? sid : "");
}

const char* spine_device_get_session_id(const spine_device_t* dev) {
    return dev ? dev->session_id : "";
}

spine_device_t* spine_device_find_remote(const spine_device_t* dev,
                                          const char* session_id) {
    if (!dev || !dev->router || !session_id) return NULL;
    return spine_router_find_remote_by_session(dev->router, session_id);
}

const char* spine_device_get_entity_type(const spine_device_t* dev,
                                          const spine_addr_t*   entity_addr) {
    if (!dev || !entity_addr) return "";
    for (uint8_t i = 0; i < dev->entity_count; i++) {
        const spine_entity_info_t* e = &dev->entities[i];
        if (e->entity_depth != entity_addr->entity_depth) continue;
        bool match = true;
        for (uint8_t j = 0; j < e->entity_depth; j++) {
            if (e->entity[j] != entity_addr->entity[j]) { match = false; break; }
        }
        if (match) return e->entity_type;
    }
    return "";
}

/* ════════════════════════════════════════════════════════════════════════════
 * Dispatch
 *
 * Look up get_local(dev)->features[] to find the handler matching the dst address,
 * passing dev itself as the device parameter to the handler.
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_dispatch(spine_device_t* dev, const char* json) {
    if (!dev || !json) return SPINE_ERR_INVALID;

    cJSON* root = cJSON_Parse(json);
    if (!root) {
        EEBUS_LOG_WARN(LOG_TAG, "%s dispatch: JSON parse failed", dev->session_id);
        return SPINE_ERR_PARSE;
    }

    spine_msg_t msg = {0};
    if (spine_datagram_decode(root, &msg) != SPINE_OK) {
        EEBUS_LOG_WARN(LOG_TAG, "%s dispatch: decode failed", dev->session_id);
        cJSON_Delete(root);
        return SPINE_ERR_PARSE;
    }

    /* SPINE §6.1 Simple Mode: empty destination device means "this device" */
    if (msg.header.dst.device[0] == '\0') {
        snprintf(msg.header.dst.device, sizeof(msg.header.dst.device),
                 "%s", get_local(dev)->device_id);
    }

    /* Uniformly look up get_local(dev)->features[] */
    spine_device_t* loc = get_local(dev);
    feature_slot_t* slot = NULL;
    for (uint8_t i = 0; i < loc->feature_count; i++) {
        if (spine_addr_equal(&loc->features[i].info.addr, &msg.header.dst)) {
            slot = &loc->features[i];
            break;
        }
    }

    if (!slot) {
        char buf[SPINE_ADDR_STR_MAX];
        spine_addr_to_str(&msg.header.dst, buf, sizeof(buf));
        EEBUS_LOG_WARN(LOG_TAG, "%s dispatch: no feature at %s",
                       dev->session_id, buf);
        /* §3.11: the destination feature does not exist — tell the caller
         * (errorNumber 4), but only for requests that expect a response. */
        if (msg.header.cmd == SPINE_CMD_READ ||
            msg.header.cmd == SPINE_CMD_WRITE ||
            msg.header.cmd == SPINE_CMD_CALL ||
            ((msg.header.cmd == SPINE_CMD_NOTIFY ||
              msg.header.cmd == SPINE_CMD_REPLY) && msg.header.ack_request)) {
            spine_device_send_result(dev, &msg.header,
                SPINE_RESULT_DEST_UNKNOWN, "destination feature unknown");
        }
        cJSON_Delete(root);
        return SPINE_ERR_NOT_FOUND;
    }

    /* §3.11: reject read/write/call for a function this feature does not
     * expose (errorNumber 6). The spec has no notion of an empty function
     * name, so an empty fn_name matches nothing and is rejected too. */
    if (msg.header.cmd == SPINE_CMD_READ ||
        msg.header.cmd == SPINE_CMD_WRITE ||
        msg.header.cmd == SPINE_CMD_CALL) {
        bool known = false;
        for (uint8_t i = 0; i < slot->info.supported_fn_count; i++) {
            if (strcmp(slot->info.supported_fns[i].function_name,
                       msg.payload.fn_name) == 0) { known = true; break; }
        }
        if (!known) {
            EEBUS_LOG_WARN(LOG_TAG,
                "%s %s rejected: function '%s' not supported by %s",
                dev->session_id, spine_cmd_str(msg.header.cmd),
                msg.payload.fn_name, slot->info.feature_type);
            spine_device_send_result(dev, &msg.header,
                SPINE_RESULT_CMD_NOT_SUPPORTED, "function not supported");
            cJSON_Delete(root);
            return SPINE_OK;
        }
    }

    /* §7.3: WRITE requires a binding from client to server */
    if (msg.header.cmd == SPINE_CMD_WRITE) {
        if (!spine_device_has_binding(dev, &msg.header.src, &msg.header.dst)) {
            EEBUS_LOG_WARN(LOG_TAG, "%s write rejected: no binding for %s",
                           dev->session_id, slot->info.feature_type);
            spine_device_send_result(dev, &msg.header,
                                      SPINE_RESULT_BINDING_REQUIRED,
                                      "binding required for write");
            cJSON_Delete(root);
            return SPINE_OK;
        }
    }

    EEBUS_LOG_DEBUG(LOG_TAG, "%s %s %s → %s",
                    dev->session_id, spine_cmd_str(msg.header.cmd),
                    msg.payload.fn_name, slot->info.feature_type);

    /* handler is called with dev itself (the caller) */
    if (slot->handler)
        slot->handler(dev, &msg, slot->userdata);

    /* Auto-ack: NOTIFY / REPLY with ackRequest expect a confirmation result
     * (SPINE §5.2.4 — both SHALL evaluate the acknowledgement request). */
    if ((msg.header.cmd == SPINE_CMD_NOTIFY ||
         msg.header.cmd == SPINE_CMD_REPLY) && msg.header.ack_request)
        spine_device_send_result(dev, &msg.header, SPINE_RESULT_NO_ERROR, "");

    cJSON_Delete(root);
    return SPINE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Send (routed via dev->ski, dev is a remote instance)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_send(spine_device_t* dev, const spine_msg_t* msg) {
    if (!dev || !msg || msg->payload.fn_name[0] == '\0') return SPINE_ERR_INVALID;
    if (!dev->is_remote) {
        EEBUS_LOG_ERROR(LOG_TAG, "%s send called on local device – rejected",
                        dev->session_id);
        return SPINE_ERR_INVALID;
    }

    cJSON* root = spine_datagram_encode(msg);
    if (!root) return SPINE_ERR_NO_MEMORY;

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return SPINE_ERR_NO_MEMORY;

    if (dev->router)
        spine_router_send(dev->router, dev, json);
    else
        EEBUS_LOG_WARN(LOG_TAG, "%s send: no router", dev->session_id);

    EEBUS_FREE(json);
    return SPINE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Send result
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_send_result(spine_device_t*       dev,
                                      const spine_header_t* req_header,
                                      uint32_t              error_number,
                                      const char*           description) {
    if (!dev || !req_header) return SPINE_ERR_INVALID;

    spine_result_data_t res;
    memset(&res, 0, sizeof(res));
    res.error_number = error_number;
    if (description && description[0] != '\0') {
        snprintf(res.description, sizeof(res.description), "%s", description);
        res.description_present = true;
    }

    cJSON* fn_data = spine_result_data_encode(&res);
    if (!fn_data) return SPINE_ERR_NO_MEMORY;

    spine_msg_t reply;
    spine_make_msg_res(&reply, req_header, SPINE_CMD_RESULT,
                       spine_device_next_msg_counter(dev),
                       "resultData", fn_data);
    spine_err_t rc = spine_device_send(dev, &reply);
    cJSON_Delete(fn_data);
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Notify (iterate all remotes, check each remote's sub_bind for subscribers)
 * ════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const spine_addr_t*        server;
    const spine_msg_payload_t* payload;
} notify_ctx_t;

/* Called for each subscriber within a single remote's sub_bind */
static void notify_sub_cb(const spine_addr_t* client, void* ud) {
    void** args = (void**)ud;
    spine_device_t* remote = (spine_device_t*)args[0];
    notify_ctx_t*   ctx    = (notify_ctx_t*)args[1];

    spine_msg_t msg;
    spine_make_msg_req(&msg, ctx->server, client,
                       SPINE_CMD_NOTIFY,
                       spine_device_next_msg_counter(remote),
                       false, ctx->payload->fn_name, ctx->payload->fn_data);
    /* Copy RFE filter fields from the provided payload */
    msg.payload.partial = ctx->payload->partial;
    msg.payload.delete_ = ctx->payload->delete_;
    spine_device_send(remote, &msg);

    char s_str[SPINE_ADDR_STR_MAX], c_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(ctx->server, s_str, sizeof(s_str));
    spine_addr_to_str(client, c_str, sizeof(c_str));
    EEBUS_LOG_INFO(LOG_TAG, "%s NOTIFY sent: fn=%s server=%s client=%s%s",
                   remote->session_id, ctx->payload->fn_name, s_str, c_str,
                   ctx->payload->partial.present ? " (partial)" : "");
}

/* Called for each connected remote device */
static void notify_remote_cb(spine_device_t* remote, void* ud) {
    notify_ctx_t* ctx = (notify_ctx_t*)ud;
    if (!remote->sub_bind) return;

    void* args[2] = { remote, ctx };
    spine_sub_bind_foreach_subscriber(remote->sub_bind, ctx->server,
                                       notify_sub_cb, args);
}

spine_err_t spine_device_notify(spine_device_t*     dev,
                                 const spine_addr_t* server_addr,
                                 const spine_msg_t*  msg) {
    if (!dev || !server_addr || !msg || !msg->payload.fn_name[0])
        return SPINE_ERR_INVALID;

    if (!dev->router) return SPINE_OK;

    notify_ctx_t ctx = { server_addr, &msg->payload };
    spine_router_foreach_remote(dev->router, notify_remote_cb, &ctx);
    return SPINE_OK;
}

spine_err_t spine_device_notify_to(spine_device_t*     remote_dev,
                                    const spine_addr_t* server_addr,
                                    const spine_msg_t*  msg) {
    if (!remote_dev || !server_addr || !msg || !msg->payload.fn_name[0])
        return SPINE_ERR_INVALID;
    if (!remote_dev->sub_bind) return SPINE_OK;

    notify_ctx_t ctx = { server_addr, &msg->payload };
    void* args[2] = { remote_dev, &ctx };
    spine_sub_bind_foreach_subscriber(remote_dev->sub_bind, server_addr,
                                       notify_sub_cb, args);
    return SPINE_OK;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Discovery
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_start_discovery(spine_device_t* dev) {
    if (!dev || !dev->nm) return SPINE_ERR_INVALID;
    return spine_node_mgmt_start_discovery(dev->nm);
}

spine_err_t spine_device_set_usecase_available(spine_device_t* dev,
                                                const char*     use_case_name,
                                                bool            available) {
    if (!dev || !use_case_name) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(dev);
    if (!loc->nm) return SPINE_ERR_INVALID;
    return spine_node_mgmt_set_usecase_available(loc->nm, use_case_name, available);
}

spine_err_t spine_device_notify_discovery(spine_device_t* dev) {
    if (!dev) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(dev);
    if (!loc->nm) return SPINE_ERR_INVALID;
    return spine_node_mgmt_notify_discovery(loc->nm);
}

spine_err_t spine_device_notify_usecases(spine_device_t* dev) {
    if (!dev) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(dev);
    if (!loc->nm) return SPINE_ERR_INVALID;
    return spine_node_mgmt_notify_usecases(loc->nm);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Per-device cache slots
 * ════════════════════════════════════════════════════════════════════════════ */

void* spine_device_get_cache(const spine_device_t* dev, uint8_t slot) {
    if (!dev || slot >= SPINE_CACHE_SLOT_MAX) return NULL;
    return dev->cache[slot].data;
}

void spine_device_set_cache(spine_device_t* dev, uint8_t slot,
                             void* data, void (*destroy)(void*)) {
    if (!dev || slot >= SPINE_CACHE_SLOT_MAX) return;
    if (dev->cache[slot].data && dev->cache[slot].destroy)
        dev->cache[slot].destroy(dev->cache[slot].data);
    dev->cache[slot].data = data;
    dev->cache[slot].destroy = destroy;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Subscribe / Bind (delegated to nm; nm uses pending_add_sb to record addresses)
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_device_subscribe(spine_device_t*     dev,
                                    const spine_addr_t* client_addr,
                                    const spine_addr_t* server_addr) {
    if (!dev || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    return spine_node_mgmt_send_subscribe(dev->nm, client_addr, server_addr);
}

spine_err_t spine_device_bind(spine_device_t*     dev,
                               const spine_addr_t* client_addr,
                               const spine_addr_t* server_addr) {
    if (!dev || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    return spine_node_mgmt_send_bind(dev->nm, client_addr, server_addr);
}

spine_err_t spine_device_unsubscribe(spine_device_t*     dev,
                                      const spine_addr_t* client_addr,
                                      const spine_addr_t* server_addr) {
    if (!dev || !dev->nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    return spine_node_mgmt_send_unsubscribe(dev->nm, client_addr, server_addr);
}

spine_err_t spine_device_unbind(spine_device_t*     dev,
                                 const spine_addr_t* client_addr,
                                 const spine_addr_t* server_addr) {
    if (!dev || !dev->nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    return spine_node_mgmt_send_unbind(dev->nm, client_addr, server_addr);
}
