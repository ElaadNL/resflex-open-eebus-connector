// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_node_mgmt.c
 *
 * NodeManagement feature handler.
 * nm only holds a dev pointer; all data is accessed via get_local(dev) and dev->xxx.
 *
 * Local data (features, uc_entries, device_type, sub_bind, peer_ready_cbs)
 *     -> get_local(dev)->xxx
 * Remote data (device_id, uc_entries on remote, disc_state, pending)
 *     -> dev->xxx (when dev is remote)
 */

#include "spine_node_mgmt.h"
#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"

#include <string.h>
#include <stdio.h>

#define LOG_TAG "spine.nm"

/* ── struct ───────────────────────────────────────────────────────────────── */

struct spine_node_mgmt {
    spine_device_t* dev;   /* only field; everything else accessed via dev->xxx */
};

/* ════════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ════════════════════════════════════════════════════════════════════════════ */

static nm_addr_t to_nm_addr(const spine_addr_t* a) {
    nm_addr_t nm;
    memset(&nm, 0, sizeof(nm));
    snprintf(nm.device, sizeof(nm.device), "%s", a->device);
    uint8_t d = (a->entity_depth < NM_ENTITY_MAX_DEPTH)
                ? a->entity_depth : NM_ENTITY_MAX_DEPTH;
    for (uint8_t i = 0; i < d; i++) nm.entity[i] = a->entity[i];
    nm.entity_depth    = d;
    nm.feature         = a->feature;
    nm.feature_present = true;
    return nm;
}

/* Local NM feature address (entity 0, feature 0)
 * Uniformly uses get_local(dev) to get the local device_id */
static spine_addr_t local_nm_addr(spine_device_t* dev) {
    spine_addr_t a;
    spine_addr_make(&a, get_local(dev)->device_id, 0, 0);
    return a;
}

/* Look up the featureType of a remote feature by address (from discovery data) */
static const char* find_remote_feature_type(spine_device_t*     dev,
                                             const spine_addr_t* addr) {
    for (uint8_t i = 0; i < dev->feature_count; i++) {
        if (spine_addr_equal(&dev->features[i].info.addr, addr))
            return dev->features[i].info.feature_type;
    }
    return NULL;
}

/* ── Delete matching helpers (§7.3.4 / §7.4.4) ──────────────────────────── */

typedef struct {
    nm_addr_t client;
    nm_addr_t server;
} sb_delete_criteria_t;

/* Check if a spine_addr_t entry matches an nm_addr_t criteria per §7.3.4:
 *   device:  exact match (skip if criteria empty)
 *   entity:  prefix match (criteria [1] matches [1], [1,0], [1,0,3])
 *            criteria entity_depth==0 → match all (device-level delete)
 *   feature: exact match only if criteria feature_present; else match all */
static bool sb_delete_addr_match(const spine_addr_t* entry,
                                  const nm_addr_t*    criteria) {
    if (criteria->device[0] != '\0' &&
        strcmp(entry->device, criteria->device) != 0)
        return false;
    if (criteria->entity_depth > 0) {
        if (entry->entity_depth < criteria->entity_depth)
            return false;
        for (uint8_t i = 0; i < criteria->entity_depth; i++)
            if (entry->entity[i] != criteria->entity[i])
                return false;
    }
    if (criteria->feature_present &&
        entry->feature != criteria->feature)
        return false;
    return true;
}

static bool sb_delete_match(uint32_t             id,
                              const spine_addr_t*  client,
                              const spine_addr_t*  server,
                              void*                ud) {
    (void)id;
    const sb_delete_criteria_t* c = (const sb_delete_criteria_t*)ud;
    return sb_delete_addr_match(client, &c->client) &&
           sb_delete_addr_match(server, &c->server);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Build discovery reply (reading from get_local(dev)->features[])
 * ════════════════════════════════════════════════════════════════════════════ */

static cJSON* build_discovery_reply(spine_device_t* dev) {
    spine_device_t* loc = get_local(dev);

    nm_discovery_data_t disc;
    memset(&disc, 0, sizeof(disc));

    snprintf(disc.spec_versions[0], sizeof(disc.spec_versions[0]),
             "%s", SPINE_SPEC_VERSION);
    disc.spec_version_count = 1;

    snprintf(disc.device_address, sizeof(disc.device_address),
             "%s", loc->device_id);
    snprintf(disc.device_type, sizeof(disc.device_type),
             "%s", loc->device_type[0] ? loc->device_type : "Generic");
    disc.device_last_state_change = NM_STATE_CHANGE_ADDED;

    /* Populate entity information from entity registry (SPINE §7.1.2) */
    for (uint8_t i = 0; i < loc->entity_count && disc.entity_count < NM_ENTITY_INFO_MAX; i++) {
        const spine_entity_info_t* ei = &loc->entities[i];
        nm_entity_info_t* ne = &disc.entities[disc.entity_count++];
        memset(ne, 0, sizeof(*ne));
        uint8_t d = (ei->entity_depth < NM_ENTITY_MAX_DEPTH)
                    ? ei->entity_depth : NM_ENTITY_MAX_DEPTH;
        for (uint8_t j = 0; j < d; j++) ne->entity_addr.entity[j] = ei->entity[j];
        ne->entity_addr.entity_depth = d;
        snprintf(ne->entity_type, sizeof(ne->entity_type), "%s", ei->entity_type);
    }

    /* Populate feature information with real supportedFunction names */
    for (uint8_t i = 0; i < loc->feature_count; i++) {
        if (disc.feature_count >= NM_FEATURE_INFO_MAX) break;
        const spine_feature_info_t* fi = &loc->features[i].info;
        nm_feature_info_t* f = &disc.features[disc.feature_count++];
        memset(f, 0, sizeof(*f));
        f->feature_addr = to_nm_addr(&fi->addr);
        snprintf(f->feature_type, sizeof(f->feature_type), "%s", fi->feature_type);
        snprintf(f->role, sizeof(f->role), "%s", fi->role);

        /* Copy registered supported functions (real spec-defined names) */
        for (uint8_t s = 0; s < fi->supported_fn_count && s < NM_SUPPORTED_FN_MAX; s++) {
            nm_supported_fn_t* nf = &f->supported_fns[f->supported_fn_count++];
            snprintf(nf->function_name, sizeof(nf->function_name),
                     "%s", fi->supported_fns[s].function_name);
            nf->ops.read_present  = fi->supported_fns[s].read_present;
            nf->ops.read_partial  = fi->supported_fns[s].read_partial;
            nf->ops.write_present = fi->supported_fns[s].write_present;
            nf->ops.write_partial = fi->supported_fns[s].write_partial;
        }
    }
    return nm_discovery_data_encode(&disc);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Build UC reply (reading from get_local(dev)->uc_entries[])
 * ════════════════════════════════════════════════════════════════════════════ */

static cJSON* build_uc_reply(spine_device_t* dev) {
    spine_device_t* loc = get_local(dev);

    nm_uc_data_t uc_data;
    memset(&uc_data, 0, sizeof(uc_data));

    /* Group use cases by (address + actor) per XSD semantics:
     * one useCaseInformation entry per (address, actor) with multiple
     * useCaseSupport children listing all UCs supported by that actor. */
    for (uint8_t i = 0; i < loc->uc_entry_count; i++) {
        const spine_nm_uc_entry_t* e = &loc->uc_entries[i];
        nm_addr_t addr = to_nm_addr(&e->address);

        /* Find existing useCaseInformation with same address + actor */
        nm_uc_info_t* info = NULL;
        for (uint8_t j = 0; j < uc_data.count; j++) {
            if (strcmp(uc_data.items[j].actor, e->actor) == 0 &&
                nm_addr_equal(&uc_data.items[j].address, &addr)) {
                info = &uc_data.items[j];
                break;
            }
        }

        /* Allocate new useCaseInformation if none matches */
        if (!info) {
            if (uc_data.count >= NM_UC_INFO_MAX) {
                EEBUS_LOG_WARN(LOG_TAG,
                    "build_uc_reply: NM_UC_INFO_MAX (%d) reached, "
                    "dropping use case '%s'", NM_UC_INFO_MAX, e->use_case_name);
                break;
            }
            info = &uc_data.items[uc_data.count++];
            memset(info, 0, sizeof(*info));
            info->address = addr;
            snprintf(info->actor, sizeof(info->actor), "%s", e->actor);
        }

        /* Add useCaseSupport to the group */
        if (info->uc_support_count >= NM_UC_SUPPORT_MAX) {
            EEBUS_LOG_WARN(LOG_TAG,
                "build_uc_reply: NM_UC_SUPPORT_MAX (%d) reached for actor '%s', "
                "dropping use case '%s'",
                NM_UC_SUPPORT_MAX, e->actor, e->use_case_name);
            continue;
        }
        nm_uc_support_t* sup = &info->uc_supports[info->uc_support_count++];
        snprintf(sup->use_case_name,             sizeof(sup->use_case_name),
                 "%s", e->use_case_name);
        snprintf(sup->use_case_version,          sizeof(sup->use_case_version),
                 "%s", e->use_case_version);
        snprintf(sup->use_case_doc_sub_revision, sizeof(sup->use_case_doc_sub_revision),
                 "%s", e->use_case_doc_sub_revision);
        sup->use_case_available = e->available;
        for (uint8_t s = 0; s < e->scenario_count; s++)
            sup->scenario_support[s] = e->scenario_support[s];
        sup->scenario_count = e->scenario_count;
    }
    return nm_uc_data_encode(&uc_data);
}

/* ════════════════════════════════════════════════════════════════════════════
 * discovery state machine (remote only)
 * ════════════════════════════════════════════════════════════════════════════ */

static void start_uc_discovery(spine_node_mgmt_t* nm) {
    spine_device_t* dev = nm->dev;

    spine_addr_t src = local_nm_addr(dev);   /* local NM address */
    spine_addr_t dst;
    spine_addr_make(&dst, dev->device_id, 0, 0);   /* peer NM address */

    uint64_t counter = spine_device_pending_add(dev, SPINE_PENDING_DISC_UC);

    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, counter, false,
                       "nodeManagementUseCaseData", NULL);
    dev->disc_state = SPINE_DISC_UC_DISCOVERING;
    spine_device_send(dev, &msg);
    EEBUS_LOG_INFO(LOG_TAG, "%s UC discovery started", dev->session_id);
}

static void start_nm_subscribe(spine_node_mgmt_t* nm) {
    spine_device_t* dev = nm->dev;

    /* client = local NM (get_local retrieves the local device_id) */
    spine_addr_t client;
    spine_addr_make(&client, get_local(dev)->device_id, 0, 0);
    /* server = peer NM */
    spine_addr_t server;
    spine_addr_make(&server, dev->device_id, 0, 0);

    /* Use pending_add_sb to record addresses, facilitating sub_bind write on RESULT */
    uint64_t counter = spine_device_pending_add_sb(dev,
                                                    SPINE_PENDING_NM_SUBSCRIBE,
                                                    &client, &server);

    nm_addr_t ca = to_nm_addr(&client);
    nm_addr_t sa = to_nm_addr(&server);
    cJSON* fn_data = spine_sb_encode_subscribe(&ca, &sa, "NodeManagement");
    if (!fn_data) return;

    spine_addr_t src = local_nm_addr(dev);   /* sender: local NM */
    spine_addr_t peer_nm;
    spine_addr_make(&peer_nm, dev->device_id, 0, 0);

    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &peer_nm, SPINE_CMD_CALL, counter, true,
                       "nodeManagementSubscriptionRequestCall", fn_data);
    dev->disc_state = SPINE_DISC_NM_SUBSCRIBING;
    char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(&client, c_str, sizeof(c_str));
    spine_addr_to_str(&server, s_str, sizeof(s_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL sent: fn=nodeManagementSubscriptionRequestCall "
        "counter=%llu client=%s server=%s",
        dev->session_id, (unsigned long long)counter, c_str, s_str);
    spine_device_send(dev, &msg);
    cJSON_Delete(fn_data);
}

static void start_rediscovery(spine_node_mgmt_t* nm) {
    spine_device_t* dev = nm->dev;

    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t dst;
    spine_addr_make(&dst, dev->device_id, 0, 0);

    uint64_t counter = spine_device_pending_add(dev, SPINE_PENDING_DISC_DETAIL);

    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, counter, false,
                       "nodeManagementDetailedDiscoveryData", NULL);
    dev->disc_state = SPINE_DISC_REDISCOVERING;
    spine_device_send(dev, &msg);
    EEBUS_LOG_INFO(LOG_TAG, "%s re-discovery started (post-subscribe)", dev->session_id);
}

/* Fire all peer_ready callbacks (reading the callback list from get_local(dev)) */
static void fire_peer_ready(spine_device_t* dev) {
    spine_device_t* loc = get_local(dev);
    for (uint8_t i = 0; i < loc->peer_ready_count; i++) {
        if (loc->peer_ready_cbs[i].fn)
            loc->peer_ready_cbs[i].fn(dev,
                                       dev->uc_entries,
                                       dev->uc_entry_count,
                                       loc->peer_ready_cbs[i].ud);
    }
}

/* Fire all peer_changed callbacks */
static void fire_peer_changed(spine_device_t* dev) {
    spine_device_t* loc = get_local(dev);
    for (uint8_t i = 0; i < loc->peer_changed_count; i++) {
        if (loc->peer_changed_cbs[i].fn)
            loc->peer_changed_cbs[i].fn(dev, loc->peer_changed_cbs[i].ud);
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * Build subscription/binding data replies (§7.5)
 *
 * Transform sub_bind state into codec types and delegate encoding to codec.
 * ════════════════════════════════════════════════════════════════════════════ */

static cJSON* build_sub_data_reply(spine_device_t* dev) {
    spine_sub_bind_t* sb = dev->sub_bind;
    if (!sb) return NULL;

    nm_sub_data_t data;
    memset(&data, 0, sizeof(data));

    uint16_t count = spine_sub_bind_subscription_count(sb);
    for (uint16_t i = 0; i < count && data.count < NM_SB_ENTRY_MAX; i++) {
        const spine_subscription_t* s = spine_sub_bind_get_subscription(sb, i);
        if (!s) continue;
        data.items[data.count].id          = s->id;
        data.items[data.count].client_addr = to_nm_addr(&s->client_addr);
        data.items[data.count].server_addr = to_nm_addr(&s->server_addr);
        data.count++;
    }
    return nm_sub_data_encode(&data);
}

static cJSON* build_bind_data_reply(spine_device_t* dev) {
    spine_sub_bind_t* sb = dev->sub_bind;
    if (!sb) return NULL;

    nm_bind_data_t data;
    memset(&data, 0, sizeof(data));

    uint16_t count = spine_sub_bind_binding_count(sb);
    for (uint16_t i = 0; i < count && data.count < NM_SB_ENTRY_MAX; i++) {
        const spine_binding_t* b = spine_sub_bind_get_binding(sb, i);
        if (!b) continue;
        data.items[data.count].id          = b->id;
        data.items[data.count].client_addr = to_nm_addr(&b->client_addr);
        data.items[data.count].server_addr = to_nm_addr(&b->server_addr);
        data.count++;
    }
    return nm_bind_data_encode(&data);
}

/* ════════════════════════════════════════════════════════════════════════════
 * NM handler — split by cmdClassifier
 * ════════════════════════════════════════════════════════════════════════════ */

static void nm_handle_read(spine_device_t* dev, const spine_msg_t* msg) {
    const char* fn = msg->payload.fn_name;
    cJSON* data    = NULL;
    const char* reply_fn = fn;

    if (!fn[0] || strcmp(fn, "nodeManagementDetailedDiscoveryData") == 0) {
        data     = build_discovery_reply(dev);
        reply_fn = "nodeManagementDetailedDiscoveryData";
    } else if (strcmp(fn, "nodeManagementUseCaseData") == 0) {
        data = build_uc_reply(dev);
    } else if (strcmp(fn, "nodeManagementSubscriptionData") == 0) {
        data = build_sub_data_reply(dev);
    } else if (strcmp(fn, "nodeManagementBindingData") == 0) {
        data = build_bind_data_reply(dev);
    }

    if (data) {
        spine_msg_t reply;
        spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                           spine_device_next_msg_counter(dev),
                           reply_fn, data);
        spine_device_send(dev, &reply);
        cJSON_Delete(data);
    } else if (fn[0]) {
        /* Unrecognized function name — reply with error result */
        EEBUS_LOG_WARN(LOG_TAG,
            "nm_handle_read: unsupported function '%s' from %s",
            fn, msg->header.src.device);
        spine_device_send_result(dev, &msg->header,
                                  SPINE_RESULT_CMD_NOT_SUPPORTED,
                                  "unknown NM function");
    }
}

static void nm_handle_reply(spine_device_t* dev, const spine_msg_t* msg) {
    const spine_header_t* hdr = &msg->header;
    if (!hdr->msg_counter_ref_present) return;

    spine_pending_type_t ptype =
        spine_device_pending_consume(dev, hdr->msg_counter_ref, NULL, NULL);
    if (ptype == SPINE_PENDING_NONE) return;

    if (ptype == SPINE_PENDING_DISC_DETAIL &&
        strcmp(msg->payload.fn_name,
               "nodeManagementDetailedDiscoveryData") == 0) {

        nm_discovery_data_t disc;
        if (nm_discovery_data_decode(msg->payload.fn_data, &disc) != SPINE_OK) {
            EEBUS_LOG_WARN(LOG_TAG, "%s REPLY decode failed: fn=%s from=%s",
                           dev->session_id, msg->payload.fn_name, hdr->src.device);
            return;
        }

        const char* src_dev = (disc.device_address[0] != '\0')
                              ? disc.device_address : hdr->src.device;

        /* Always store device_id (needed for subscribe addressing) */
        if (src_dev[0] != '\0')
            snprintf(dev->device_id, sizeof(dev->device_id), "%s", src_dev);

        if (dev->disc_state == SPINE_DISC_DISCOVERING) {
            /* 1st discovery: only device_id needed for subscribe */
            EEBUS_LOG_INFO(LOG_TAG, "%s 1st discovery done: device_id=%s",
                           dev->session_id, dev->device_id);
            start_nm_subscribe(dev->nm);
        } else if (dev->disc_state == SPINE_DISC_REDISCOVERING) {
            /* 2nd discovery (post-subscribe): store authoritative data */

            /* Store device_type */
            if (disc.device_type[0])
                snprintf(dev->device_type, sizeof(dev->device_type),
                         "%s", disc.device_type);

            /* Store remote entity information */
            dev->entity_count = 0;
            for (uint8_t i = 0;
                 i < disc.entity_count && i < SPINE_ENTITY_MAX; i++) {
                spine_entity_info_t* ei = &dev->entities[dev->entity_count];
                const nm_entity_info_t* ne = &disc.entities[i];
                uint8_t d = (ne->entity_addr.entity_depth < SPINE_ENTITY_DEPTH_MAX)
                            ? ne->entity_addr.entity_depth : SPINE_ENTITY_DEPTH_MAX;
                for (uint8_t j = 0; j < d; j++)
                    ei->entity[j] = ne->entity_addr.entity[j];
                ei->entity_depth = d;
                snprintf(ei->entity_type, sizeof(ei->entity_type),
                         "%s", ne->entity_type);
                dev->entity_count++;
            }

            /* Store remote feature information */
            dev->feature_count = 0;
            for (uint8_t i = 0;
                 i < disc.feature_count && i < SPINE_FEATURE_MAX; i++) {
                feature_slot_t* slot = &dev->features[dev->feature_count];
                memset(slot, 0, sizeof(*slot));
                spine_addr_from_nm(&slot->info.addr,
                                   &disc.features[i].feature_addr, src_dev);
                snprintf(slot->info.feature_type,
                         sizeof(slot->info.feature_type),
                         "%s", disc.features[i].feature_type);
                snprintf(slot->info.role, sizeof(slot->info.role),
                         "%s", disc.features[i].role);
                uint8_t fn_count = disc.features[i].supported_fn_count;
                if (fn_count > SPINE_SUPPORTED_FN_MAX)
                    fn_count = SPINE_SUPPORTED_FN_MAX;
                for (uint8_t s = 0; s < fn_count; s++) {
                    const nm_supported_fn_t* nf = &disc.features[i].supported_fns[s];
                    spine_supported_fn_t* sf = &slot->info.supported_fns[s];
                    snprintf(sf->function_name, sizeof(sf->function_name),
                             "%s", nf->function_name);
                    sf->read_present  = nf->ops.read_present;
                    sf->read_partial  = nf->ops.read_partial;
                    sf->write_present = nf->ops.write_present;
                    sf->write_partial = nf->ops.write_partial;
                }
                slot->info.supported_fn_count = fn_count;
                slot->handler  = NULL;
                slot->userdata = NULL;
                dev->feature_count++;
            }

            EEBUS_LOG_INFO(LOG_TAG, "%s rediscovery done: %u entities, %u features",
                           dev->session_id, dev->entity_count, dev->feature_count);
            /* 2nd discovery done (post-subscribe) → read UC data */
            start_uc_discovery(dev->nm);
        }
        return;
    }

    if (ptype == SPINE_PENDING_DISC_UC &&
        strcmp(msg->payload.fn_name, "nodeManagementUseCaseData") == 0) {

        nm_uc_data_t uc_data;
        if (nm_uc_data_decode(msg->payload.fn_data, &uc_data) != SPINE_OK)
            return;

        dev->uc_entry_count = 0;
        for (uint8_t i = 0;
             i < uc_data.count && i < SPINE_NM_UC_MAX; i++) {
            const nm_uc_info_t* info = &uc_data.items[i];
            if (info->uc_support_count == 0) continue;
            for (uint8_t j = 0; j < info->uc_support_count; j++) {
                if (dev->uc_entry_count >= SPINE_NM_UC_MAX) break;
                const nm_uc_support_t* sup = &info->uc_supports[j];
                spine_nm_uc_entry_t* e = &dev->uc_entries[dev->uc_entry_count++];
                memset(e, 0, sizeof(*e));
                snprintf(e->actor, sizeof(e->actor), "%s", info->actor);
                snprintf(e->use_case_name, sizeof(e->use_case_name),
                         "%s", sup->use_case_name);
                snprintf(e->use_case_version, sizeof(e->use_case_version),
                         "%s", sup->use_case_version);
                snprintf(e->use_case_doc_sub_revision,
                         sizeof(e->use_case_doc_sub_revision),
                         "%s", sup->use_case_doc_sub_revision);
                for (uint8_t s = 0; s < sup->scenario_count; s++)
                    e->scenario_support[s] = sup->scenario_support[s];
                e->scenario_count = sup->scenario_count;
                e->available      = sup->use_case_available;
                spine_addr_from_nm(&e->address, &info->address, dev->device_id);
            }
        }

        EEBUS_LOG_INFO(LOG_TAG, "%s UC discovery done: %u usecases",
                       dev->session_id, dev->uc_entry_count);
        dev->disc_state = SPINE_DISC_READY;
        EEBUS_LOG_INFO(LOG_TAG, "%s READY", dev->session_id);
        fire_peer_ready(dev);
    }
}

static void nm_handle_result(spine_device_t* dev, const spine_msg_t* msg) {
    const spine_header_t* hdr = &msg->header;
    if (!hdr->msg_counter_ref_present) return;

    /* Parse result_data up front so all downstream handlers (and the entry
     * log below) can reuse the same decoded result. */
    spine_result_data_t res;
    memset(&res, 0, sizeof(res));
    if (msg->payload.fn_data)
        spine_result_data_decode(msg->payload.fn_data, &res);

    char from_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(&hdr->src, from_str, sizeof(from_str));

    if (res.error_number == SPINE_RESULT_NO_ERROR) {
        EEBUS_LOG_INFO(LOG_TAG,
            "%s CALL result OK: counter_ref=%llu from=%s",
            dev->session_id,
            (unsigned long long)hdr->msg_counter_ref,
            from_str);
    } else {
        EEBUS_LOG_WARN(LOG_TAG,
            "%s CALL result ERROR: counter_ref=%llu err=%u desc='%s' from=%s",
            dev->session_id,
            (unsigned long long)hdr->msg_counter_ref,
            res.error_number,
            res.description_present ? res.description : "",
            from_str);
    }

    spine_addr_t ca, sa;
    spine_pending_type_t ptype =
        spine_device_pending_consume(dev, hdr->msg_counter_ref, &ca, &sa);

    switch (ptype) {
        case SPINE_PENDING_NM_SUBSCRIBE:
            if (res.error_number == SPINE_RESULT_NO_ERROR) {
                spine_sub_bind_add_subscription(dev->sub_bind, &ca, &sa);
                /* Subscription active → re-read discovery to close the gap */
                start_rediscovery(dev->nm);
            } else {
                /* Continue discovery even without NM subscription */
                start_rediscovery(dev->nm);
            }
            break;
        case SPINE_PENDING_SUBSCRIBE:
            if (res.error_number == SPINE_RESULT_NO_ERROR)
                spine_sub_bind_add_subscription(dev->sub_bind, &ca, &sa);
            break;
        case SPINE_PENDING_BIND:
            if (res.error_number == SPINE_RESULT_NO_ERROR)
                spine_sub_bind_add_binding(dev->sub_bind, &ca, &sa);
            break;
        default:
            break;
    }
}

static void nm_handle_call(spine_device_t* dev, const spine_msg_t* msg) {
    const spine_header_t* hdr = &msg->header;
    const char* fn = msg->payload.fn_name;

    char from_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(&hdr->src, from_str, sizeof(from_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL received: fn=%s counter=%llu from=%s",
        dev->session_id, fn,
        (unsigned long long)hdr->msg_counter, from_str);

    /* Request (add) */
    if (strcmp(fn, "nodeManagementSubscriptionRequestCall") == 0 ||
        strcmp(fn, "nodeManagementBindingRequestCall") == 0) {
        bool is_sub = (fn[14] == 'S');

        spine_sb_request_t req;
        if (spine_sb_request_decode(msg->payload.fn_data, &req) != SPINE_OK) {
            EEBUS_LOG_WARN(LOG_TAG,
                "%s CALL decode failed: fn=%s from=%s",
                dev->session_id, fn, from_str);
            spine_device_send_result(dev, hdr, SPINE_RESULT_GENERAL_ERROR,
                                     "malformed request");
            return;
        }

        const char* src   = hdr->src.device;
        const char* local = get_local(dev)->device_id;
        spine_addr_t client_addr, server_addr;
        spine_addr_from_nm(&client_addr, &req.client_addr, src);
        spine_addr_from_nm(&server_addr, &req.server_addr, local);

        char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
        spine_addr_to_str(&client_addr, c_str, sizeof(c_str));
        spine_addr_to_str(&server_addr, s_str, sizeof(s_str));

        /* §7.3.2: validate serverFeatureType if present */
        if (req.server_feature_type_present) {
            const spine_feature_info_t* fi =
                spine_device_find_feature(get_local(dev), &server_addr);
            if (fi && fi->feature_type[0] &&
                strcmp(fi->feature_type, req.server_feature_type) != 0) {
                spine_device_send_result(dev, hdr, SPINE_RESULT_CMD_REJECTED,
                                         "serverFeatureType mismatch");
                EEBUS_LOG_WARN(LOG_TAG, "%s %s rejected: type mismatch "
                               "(expected %s, got %s), client=%s server=%s",
                               dev->session_id,
                               is_sub ? "subscription" : "binding",
                               fi->feature_type, req.server_feature_type,
                               c_str, s_str);
                return;
            }
        }

        /* NodeManagement (entity 0 / feature 0) is a "special" role
         * management feature, not a bindable functional server — reject
         * binding requests targeting it (SPINE §7.3). An NM address may carry
         * the entity either omitted (depth 0) or explicit as [0]. */
        if (!is_sub && server_addr.feature == 0 &&
            (server_addr.entity_depth == 0 ||
             (server_addr.entity_depth == 1 && server_addr.entity[0] == 0))) {
            spine_device_send_result(dev, hdr, SPINE_RESULT_CMD_REJECTED,
                                     "NodeManagement cannot be bound");
            EEBUS_LOG_WARN(LOG_TAG,
                "%s binding rejected: NodeManagement not bindable, "
                "client=%s server=%s", dev->session_id, c_str, s_str);
            return;
        }

        /* Binding exclusivity: reject if another client already bound this server */
        if (!is_sub && spine_device_is_server_bound_by_other(dev, &server_addr)) {
            spine_device_send_result(dev, hdr, SPINE_RESULT_CMD_REJECTED,
                                     "server already bound to another client");
            EEBUS_LOG_WARN(LOG_TAG,
                "%s binding rejected: server already bound, client=%s server=%s",
                dev->session_id, c_str, s_str);
            return;
        }

        spine_err_t rc;
        if (is_sub)
            rc = spine_sub_bind_add_subscription(dev->sub_bind,
                                                  &client_addr, &server_addr);
        else
            rc = spine_sub_bind_add_binding(dev->sub_bind,
                                             &client_addr, &server_addr);

        if (rc == SPINE_ERR_OVERFLOW) {
            spine_device_send_result(dev, hdr, SPINE_RESULT_OVERLOAD, "table full");
            EEBUS_LOG_WARN(LOG_TAG,
                "%s %s rejected: table full, client=%s server=%s",
                dev->session_id,
                is_sub ? "subscription" : "binding",
                c_str, s_str);
        } else {
            spine_device_send_result(dev, hdr, SPINE_RESULT_NO_ERROR, "");
            EEBUS_LOG_INFO(LOG_TAG,
                "%s %s added: client=%s server=%s",
                dev->session_id,
                is_sub ? "subscription" : "binding",
                c_str, s_str);
        }
        return;
    }

    /* Delete (remove) — §7.3.4 / §7.4.4 */
    if (strcmp(fn, "nodeManagementSubscriptionDeleteCall") == 0 ||
        strcmp(fn, "nodeManagementBindingDeleteCall") == 0) {
        spine_sb_delete_t del;
        if (spine_sb_delete_decode(msg->payload.fn_data, &del) != SPINE_OK) {
            EEBUS_LOG_WARN(LOG_TAG,
                "%s CALL decode failed: fn=%s from=%s",
                dev->session_id, fn, from_str);
            spine_device_send_result(dev, hdr, SPINE_RESULT_GENERAL_ERROR,
                                     "failed to decode delete request");
            return;
        }
        bool is_sub = (del.type == SPINE_SB_REQUEST_SUBSCRIBE);

        /* §7.3.4 / §7.4.4: fill in absent device addresses */
        const char* sender = hdr->src.device;
        const char* local  = get_local(dev)->device_id;
        if (del.client_addr.device[0] == '\0' &&
            del.server_addr.device[0] == '\0') {
            snprintf(del.client_addr.device, sizeof(del.client_addr.device),
                     "%s", sender);
            snprintf(del.server_addr.device, sizeof(del.server_addr.device),
                     "%s", local);
        } else if (del.client_addr.device[0] == '\0') {
            snprintf(del.client_addr.device, sizeof(del.client_addr.device),
                     "%s", local);
        } else if (del.server_addr.device[0] == '\0') {
            snprintf(del.server_addr.device, sizeof(del.server_addr.device),
                     "%s", local);
        }

        sb_delete_criteria_t criteria = { del.client_addr, del.server_addr };
        if (is_sub)
            spine_sub_bind_remove_subscriptions_if(dev->sub_bind,
                                                     sb_delete_match, &criteria);
        else
            spine_sub_bind_remove_bindings_if(dev->sub_bind,
                                               sb_delete_match, &criteria);

        /* Convert nm_addr_t to spine_addr_t for consistent address logging */
        spine_addr_t ca, sa;
        spine_addr_from_nm(&ca, &del.client_addr, del.client_addr.device);
        spine_addr_from_nm(&sa, &del.server_addr, del.server_addr.device);
        char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
        spine_addr_to_str(&ca, c_str, sizeof(c_str));
        spine_addr_to_str(&sa, s_str, sizeof(s_str));
        EEBUS_LOG_INFO(LOG_TAG,
            "%s %s delete processed: client=%s server=%s",
            dev->session_id, is_sub ? "subscription" : "binding",
            c_str, s_str);
        spine_device_send_result(dev, hdr, SPINE_RESULT_NO_ERROR, "");
    }
}

/* Update a feature slot's info from a decoded nm_feature_info_t. */
static void feature_slot_update(feature_slot_t*         slot,
                                 const nm_feature_info_t* src) {
    snprintf(slot->info.feature_type, sizeof(slot->info.feature_type),
             "%s", src->feature_type);
    snprintf(slot->info.role, sizeof(slot->info.role),
             "%s", src->role);
    uint8_t fn_count = src->supported_fn_count;
    if (fn_count > SPINE_SUPPORTED_FN_MAX) fn_count = SPINE_SUPPORTED_FN_MAX;
    for (uint8_t s = 0; s < fn_count; s++) {
        const nm_supported_fn_t* nf = &src->supported_fns[s];
        spine_supported_fn_t* sf = &slot->info.supported_fns[s];
        snprintf(sf->function_name, sizeof(sf->function_name),
                 "%s", nf->function_name);
        sf->read_present  = nf->ops.read_present;
        sf->read_partial  = nf->ops.read_partial;
        sf->write_present = nf->ops.write_present;
        sf->write_partial = nf->ops.write_partial;
    }
    slot->info.supported_fn_count = fn_count;
}

/* Match entity_info by exact entity path (ignores device). */
static bool entity_info_match(const spine_entity_info_t* e,
                               const spine_addr_t*        addr) {
    if (e->entity_depth != addr->entity_depth) return false;
    for (uint8_t k = 0; k < addr->entity_depth; k++)
        if (e->entity[k] != addr->entity[k]) return false;
    return true;
}

/* Match entity_info by prefix: true if addr's entity path is a prefix of e's.
 * Used for cascade removal of child entities (§3.2 hierarchy). */
static bool entity_info_prefix(const spine_entity_info_t* e,
                                const spine_addr_t*        addr) {
    if (e->entity_depth < addr->entity_depth) return false;
    for (uint8_t k = 0; k < addr->entity_depth; k++)
        if (e->entity[k] != addr->entity[k]) return false;
    return true;
}

static void nm_handle_notify(spine_device_t* dev, const spine_msg_t* msg) {
    const char* fn = msg->payload.fn_name;

    char from_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(&msg->header.src, from_str, sizeof(from_str));
    EEBUS_LOG_INFO(LOG_TAG, "%s NOTIFY received: fn=%s from=%s",
                   dev->session_id, fn, from_str);

    /* §7.5.4: peer UC availability changed */
    if (strcmp(fn, "nodeManagementUseCaseData") == 0) {
        nm_uc_data_t uc_data;
        if (nm_uc_data_decode(msg->payload.fn_data, &uc_data) != SPINE_OK) {
            EEBUS_LOG_WARN(LOG_TAG, "%s NOTIFY decode failed: fn=%s from=%s",
                           dev->session_id, fn, from_str);
            return;
        }

        dev->uc_entry_count = 0;
        for (uint8_t i = 0; i < uc_data.count && i < SPINE_NM_UC_MAX; i++) {
            const nm_uc_info_t* info = &uc_data.items[i];
            for (uint8_t j = 0; j < info->uc_support_count; j++) {
                if (dev->uc_entry_count >= SPINE_NM_UC_MAX) break;
                const nm_uc_support_t* sup = &info->uc_supports[j];
                spine_nm_uc_entry_t* e = &dev->uc_entries[dev->uc_entry_count++];
                memset(e, 0, sizeof(*e));
                snprintf(e->actor, sizeof(e->actor), "%s", info->actor);
                snprintf(e->use_case_name, sizeof(e->use_case_name),
                         "%s", sup->use_case_name);
                snprintf(e->use_case_version, sizeof(e->use_case_version),
                         "%s", sup->use_case_version);
                snprintf(e->use_case_doc_sub_revision,
                         sizeof(e->use_case_doc_sub_revision),
                         "%s", sup->use_case_doc_sub_revision);
                for (uint8_t s = 0; s < sup->scenario_count; s++)
                    e->scenario_support[s] = sup->scenario_support[s];
                e->scenario_count = sup->scenario_count;
                e->available      = sup->use_case_available;
                spine_addr_from_nm(&e->address, &info->address, dev->device_id);
            }
        }
        EEBUS_LOG_INFO(LOG_TAG, "%s UC notify: %u usecases updated",
                       dev->session_id, dev->uc_entry_count);
        fire_peer_changed(dev);
        return;
    }

    /* §7.1.5: peer discovery data changed */
    if (strcmp(fn, "nodeManagementDetailedDiscoveryData") != 0) {
        EEBUS_LOG_WARN(LOG_TAG, "%s NOTIFY unsupported function: fn=%s from=%s",
                       dev->session_id, fn, from_str);
        return;
    }

    nm_discovery_data_t disc;
    if (nm_discovery_data_decode(msg->payload.fn_data, &disc) != SPINE_OK) {
        EEBUS_LOG_WARN(LOG_TAG, "%s NOTIFY decode failed: fn=%s from=%s",
                       dev->session_id, fn, from_str);
        return;
    }

    const char* src_dev  = msg->header.src.device;
    spine_sub_bind_t* sb = dev->sub_bind;

    /* ── device information (modified only in Simple Mode) ── */
    if (disc.device_last_state_change == NM_STATE_CHANGE_MODIFIED) {
        if (disc.device_address[0])
            snprintf(dev->device_id, sizeof(dev->device_id),
                     "%s", disc.device_address);
        if (disc.device_type[0])
            snprintf(dev->device_type, sizeof(dev->device_type),
                     "%s", disc.device_type);
    }

    /* ── entity changes ── */
    for (uint8_t i = 0; i < disc.entity_count; i++) {
        spine_addr_t addr;
        spine_addr_from_nm(&addr, &disc.entities[i].entity_addr, src_dev);
        nm_state_change_t sc = disc.entities[i].last_state_change;

        if (sc == NM_STATE_CHANGE_REMOVED) {
            /* Remove entity and all child entities from table (§3.2 hierarchy) */
            for (uint8_t j = 0; j < dev->entity_count; ) {
                spine_entity_info_t* e = &dev->entities[j];
                if (entity_info_prefix(e, &addr)) {
                    uint8_t tail = dev->entity_count - j - 1;
                    if (tail > 0)
                        memmove(&dev->entities[j], &dev->entities[j+1],
                                tail * sizeof(spine_entity_info_t));
                    dev->entity_count--;
                } else {
                    j++;
                }
            }
            /* Remove all features under this entity (and child entities) +
             * their sub_bind entries */
            for (uint8_t j = 0; j < dev->feature_count; ) {
                if (spine_addr_entity_is_prefix(&addr, &dev->features[j].info.addr)) {
                    spine_sub_bind_remove_feature(sb, &dev->features[j].info.addr);
                    uint8_t tail = dev->feature_count - j - 1;
                    if (tail > 0)
                        memmove(&dev->features[j], &dev->features[j+1],
                                tail * sizeof(feature_slot_t));
                    dev->feature_count--;
                } else {
                    j++;
                }
            }
            spine_sub_bind_remove_entity(sb, &addr);

        } else if (sc == NM_STATE_CHANGE_ADDED ||
                   sc == NM_STATE_CHANGE_NONE) {
            /* Find existing or allocate new slot */
            spine_entity_info_t* found = NULL;
            for (uint8_t j = 0; j < dev->entity_count; j++) {
                if (entity_info_match(&dev->entities[j], &addr)) {
                    found = &dev->entities[j];
                    break;
                }
            }
            if (!found && dev->entity_count < SPINE_ENTITY_MAX) {
                found = &dev->entities[dev->entity_count];
                memset(found, 0, sizeof(*found));
                uint8_t d = (addr.entity_depth < SPINE_ENTITY_DEPTH_MAX)
                            ? addr.entity_depth : SPINE_ENTITY_DEPTH_MAX;
                for (uint8_t k = 0; k < d; k++) found->entity[k] = addr.entity[k];
                found->entity_depth = d;
                dev->entity_count++;
            }
            if (found) {
                snprintf(found->entity_type, sizeof(found->entity_type),
                         "%s", disc.entities[i].entity_type);
            }

        } else if (sc == NM_STATE_CHANGE_MODIFIED) {
            /* Update entity_type */
            for (uint8_t j = 0; j < dev->entity_count; j++) {
                spine_entity_info_t* e = &dev->entities[j];
                if (entity_info_match(e, &addr)) {
                    snprintf(e->entity_type, sizeof(e->entity_type),
                             "%s", disc.entities[i].entity_type);
                    break;
                }
            }
        }
    }

    /* ── feature changes ── */
    for (uint8_t i = 0; i < disc.feature_count; i++) {
        spine_addr_t addr;
        spine_addr_from_nm(&addr, &disc.features[i].feature_addr, src_dev);
        nm_state_change_t sc = disc.features[i].last_state_change;

        if (sc == NM_STATE_CHANGE_REMOVED) {
            spine_sub_bind_remove_feature(sb, &addr);
            for (uint8_t j = 0; j < dev->feature_count; j++) {
                if (spine_addr_equal(&dev->features[j].info.addr, &addr)) {
                    uint8_t tail = dev->feature_count - j - 1;
                    if (tail > 0)
                        memmove(&dev->features[j], &dev->features[j+1],
                                tail * sizeof(feature_slot_t));
                    dev->feature_count--;
                    break;
                }
            }

        } else if (sc == NM_STATE_CHANGE_ADDED ||
                   sc == NM_STATE_CHANGE_NONE) {
            /* §7.1.5 rule 5b: features of a newly added entity may appear
             * with lastStateChange absent (NONE) — treat as ADDED.
             * If already exists, update in-place (dedup). */
            feature_slot_t* slot = NULL;
            for (uint8_t j = 0; j < dev->feature_count; j++) {
                if (spine_addr_equal(&dev->features[j].info.addr, &addr)) {
                    slot = &dev->features[j];
                    break;
                }
            }
            if (!slot && dev->feature_count < SPINE_FEATURE_MAX) {
                slot = &dev->features[dev->feature_count];
                memset(slot, 0, sizeof(*slot));
                slot->info.addr = addr;
                dev->feature_count++;
            }
            if (slot)
                feature_slot_update(slot, &disc.features[i]);

        } else if (sc == NM_STATE_CHANGE_MODIFIED) {
            /* Update existing feature in-place */
            for (uint8_t j = 0; j < dev->feature_count; j++) {
                if (spine_addr_equal(&dev->features[j].info.addr, &addr)) {
                    feature_slot_update(&dev->features[j], &disc.features[i]);
                    break;
                }
            }
        }
    }

    EEBUS_LOG_INFO(LOG_TAG, "%s discovery notify processed: %u entities, %u features",
                   dev->session_id, dev->entity_count, dev->feature_count);

    /* Fire peer_changed callbacks */
    fire_peer_changed(dev);
}

static void nm_handler(spine_device_t*    dev,
                        const spine_msg_t* msg,
                        void*              ud) {
    (void)ud;
    switch (msg->header.cmd) {
        case SPINE_CMD_READ:   nm_handle_read(dev, msg);   break;
        case SPINE_CMD_REPLY:  nm_handle_reply(dev, msg);  break;
        case SPINE_CMD_RESULT: nm_handle_result(dev, msg); break;
        case SPINE_CMD_CALL:   nm_handle_call(dev, msg);   break;
        case SPINE_CMD_NOTIFY: nm_handle_notify(dev, msg); break;
        default: break;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_node_mgmt_create(spine_node_mgmt_t** out,
                                    spine_device_t*     dev) {
    if (!out || !dev) return SPINE_ERR_INVALID;

    spine_node_mgmt_t* nm = EEBUS_NEW(spine_node_mgmt_t);
    if (!nm) return SPINE_ERR_NO_MEMORY;
    nm->dev = dev;

    if (!dev->is_remote) {
        /* Register NM handler on local device */
        spine_addr_t nm_addr;
        spine_addr_make(&nm_addr, dev->device_id, 0, 0);
        spine_device_add_feature(dev, &nm_addr, "NodeManagement", "special",
                                  nm_handler, NULL);
        /* Register spec-defined supported functions (SPINE §7.1-7.5) */
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementDetailedDiscoveryData", true, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementUseCaseData", true, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementSubscriptionRequestCall", false, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementBindingRequestCall", false, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementSubscriptionDeleteCall", false, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementBindingDeleteCall", false, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementSubscriptionData", true, false, false, false);
        spine_device_add_feature_function(dev, &nm_addr,
            "nodeManagementBindingData", true, false, false, false);
    }
    /* remote device: do not register (handler is already in local->features[], looked up during dispatch) */

    *out = nm;
    return SPINE_OK;
}

void spine_node_mgmt_destroy(spine_node_mgmt_t* nm) {
    if (!nm) return;
    EEBUS_FREE(nm);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Active initiation interface
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t spine_node_mgmt_start_discovery(spine_node_mgmt_t* nm) {
    if (!nm) return SPINE_ERR_INVALID;
    spine_device_t* dev = nm->dev;

    /* src = local NM; dst.device="" (Simple Mode §6.1 compliant) */
    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t dst;
    spine_addr_make(&dst, "", 0, 0);

    uint64_t counter = spine_device_pending_add(dev, SPINE_PENDING_DISC_DETAIL);

    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, counter, false,
                       "nodeManagementDetailedDiscoveryData", NULL);

    dev->disc_state = SPINE_DISC_DISCOVERING;
    EEBUS_LOG_INFO(LOG_TAG, "%s detail discovery started", dev->session_id);
    return spine_device_send(dev, &msg);
}

spine_err_t spine_node_mgmt_send_subscribe(spine_node_mgmt_t*  nm,
                                            const spine_addr_t* client_addr,
                                            const spine_addr_t* server_addr) {
    if (!nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    spine_device_t* dev = nm->dev;

    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t peer_nm;
    spine_addr_make(&peer_nm, dev->device_id, 0, 0);

    nm_addr_t ca = to_nm_addr(client_addr);
    nm_addr_t sa = to_nm_addr(server_addr);
    const char* sft = find_remote_feature_type(dev, server_addr);
    cJSON* fn_data = spine_sb_encode_subscribe(&ca, &sa, sft);
    if (!fn_data) return SPINE_ERR_NO_MEMORY;

    uint64_t counter = spine_device_pending_add_sb(dev, SPINE_PENDING_SUBSCRIBE,
                                                    client_addr, server_addr);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &peer_nm, SPINE_CMD_CALL, counter, true,
                       "nodeManagementSubscriptionRequestCall", fn_data);
    char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(client_addr, c_str, sizeof(c_str));
    spine_addr_to_str(server_addr, s_str, sizeof(s_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL sent: fn=nodeManagementSubscriptionRequestCall "
        "counter=%llu client=%s server=%s",
        dev->session_id, (unsigned long long)counter, c_str, s_str);
    spine_err_t rc = spine_device_send(dev, &msg);
    cJSON_Delete(fn_data);
    return rc;
}

spine_err_t spine_node_mgmt_send_bind(spine_node_mgmt_t*  nm,
                                       const spine_addr_t* client_addr,
                                       const spine_addr_t* server_addr) {
    if (!nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    spine_device_t* dev = nm->dev;

    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t peer_nm;
    spine_addr_make(&peer_nm, dev->device_id, 0, 0);

    nm_addr_t ca = to_nm_addr(client_addr);
    nm_addr_t sa = to_nm_addr(server_addr);
    const char* sft = find_remote_feature_type(dev, server_addr);
    cJSON* fn_data = spine_sb_encode_bind(&ca, &sa, sft);
    if (!fn_data) return SPINE_ERR_NO_MEMORY;

    uint64_t counter = spine_device_pending_add_sb(dev, SPINE_PENDING_BIND,
                                                    client_addr, server_addr);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &peer_nm, SPINE_CMD_CALL, counter, true,
                       "nodeManagementBindingRequestCall", fn_data);
    char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(client_addr, c_str, sizeof(c_str));
    spine_addr_to_str(server_addr, s_str, sizeof(s_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL sent: fn=nodeManagementBindingRequestCall "
        "counter=%llu client=%s server=%s",
        dev->session_id, (unsigned long long)counter, c_str, s_str);
    spine_err_t rc = spine_device_send(dev, &msg);
    cJSON_Delete(fn_data);
    return rc;
}

spine_err_t spine_node_mgmt_send_unsubscribe(spine_node_mgmt_t*  nm,
                                              const spine_addr_t* client_addr,
                                              const spine_addr_t* server_addr) {
    if (!nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    spine_device_t* dev = nm->dev;

    /* Remove from local sub_bind immediately */
    spine_sub_bind_remove_subscription(dev->sub_bind, client_addr, server_addr);

    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t peer_nm;
    spine_addr_make(&peer_nm, dev->device_id, 0, 0);

    nm_addr_t ca = to_nm_addr(client_addr);
    nm_addr_t sa = to_nm_addr(server_addr);
    cJSON* fn_data = spine_sb_encode_delete_subscribe(&ca, &sa);
    if (!fn_data) return SPINE_ERR_NO_MEMORY;

    uint64_t counter = spine_device_next_msg_counter(dev);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &peer_nm, SPINE_CMD_CALL,
                       counter, true,
                       "nodeManagementSubscriptionDeleteCall", fn_data);
    char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(client_addr, c_str, sizeof(c_str));
    spine_addr_to_str(server_addr, s_str, sizeof(s_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL sent: fn=nodeManagementSubscriptionDeleteCall "
        "counter=%llu client=%s server=%s",
        dev->session_id, (unsigned long long)counter, c_str, s_str);
    spine_err_t rc = spine_device_send(dev, &msg);
    cJSON_Delete(fn_data);
    return rc;
}

spine_err_t spine_node_mgmt_send_unbind(spine_node_mgmt_t*  nm,
                                         const spine_addr_t* client_addr,
                                         const spine_addr_t* server_addr) {
    if (!nm || !client_addr || !server_addr) return SPINE_ERR_INVALID;
    spine_device_t* dev = nm->dev;

    /* Remove from local sub_bind immediately */
    spine_sub_bind_remove_binding(dev->sub_bind, client_addr, server_addr);

    spine_addr_t src = local_nm_addr(dev);
    spine_addr_t peer_nm;
    spine_addr_make(&peer_nm, dev->device_id, 0, 0);

    nm_addr_t ca = to_nm_addr(client_addr);
    nm_addr_t sa = to_nm_addr(server_addr);
    cJSON* fn_data = spine_sb_encode_delete_bind(&ca, &sa);
    if (!fn_data) return SPINE_ERR_NO_MEMORY;

    uint64_t counter = spine_device_next_msg_counter(dev);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &peer_nm, SPINE_CMD_CALL,
                       counter, true,
                       "nodeManagementBindingDeleteCall", fn_data);
    char c_str[SPINE_ADDR_STR_MAX], s_str[SPINE_ADDR_STR_MAX];
    spine_addr_to_str(client_addr, c_str, sizeof(c_str));
    spine_addr_to_str(server_addr, s_str, sizeof(s_str));
    EEBUS_LOG_INFO(LOG_TAG,
        "%s CALL sent: fn=nodeManagementBindingDeleteCall "
        "counter=%llu client=%s server=%s",
        dev->session_id, (unsigned long long)counter, c_str, s_str);
    spine_err_t rc = spine_device_send(dev, &msg);
    cJSON_Delete(fn_data);
    return rc;
}

spine_err_t spine_node_mgmt_set_usecase_available(spine_node_mgmt_t* nm,
                                                    const char* use_case_name,
                                                    bool available) {
    if (!nm || !use_case_name) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(nm->dev);

    /* Find the use case entry by name */
    spine_nm_uc_entry_t* found = NULL;
    for (uint8_t i = 0; i < loc->uc_entry_count; i++) {
        if (strcmp(loc->uc_entries[i].use_case_name, use_case_name) == 0) {
            found = &loc->uc_entries[i];
            break;
        }
    }
    if (!found) return SPINE_ERR_NOT_FOUND;
    if (found->available == available) return SPINE_OK;  /* no change */

    found->available = available;
    EEBUS_LOG_INFO(LOG_TAG, "use case '%s' available=%s",
                   use_case_name, available ? "true" : "false");

    return spine_node_mgmt_notify_usecases(nm);
}

spine_err_t spine_node_mgmt_notify_discovery(spine_node_mgmt_t* nm) {
    if (!nm) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(nm->dev);

    cJSON* data = build_discovery_reply(nm->dev);
    if (!data) return SPINE_ERR_NO_MEMORY;

    spine_addr_t nm_addr;
    spine_addr_make(&nm_addr, loc->device_id, 0, 0);
    spine_msg_t ntf = {0};
    snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name),
             "nodeManagementDetailedDiscoveryData");
    ntf.payload.fn_data = data;
    spine_err_t rc = spine_device_notify(nm->dev, &nm_addr, &ntf);
    cJSON_Delete(data);
    return rc;
}

spine_err_t spine_node_mgmt_notify_usecases(spine_node_mgmt_t* nm) {
    if (!nm) return SPINE_ERR_INVALID;
    spine_device_t* loc = get_local(nm->dev);

    cJSON* data = build_uc_reply(nm->dev);
    if (!data) return SPINE_ERR_NO_MEMORY;

    spine_addr_t nm_addr;
    spine_addr_make(&nm_addr, loc->device_id, 0, 0);
    spine_msg_t ntf = {0};
    snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name),
             "nodeManagementUseCaseData");
    ntf.payload.fn_data = data;
    spine_err_t rc = spine_device_notify(nm->dev, &nm_addr, &ntf);
    cJSON_Delete(data);
    return rc;
}
