// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_sub_bind.c
 *
 * Pure data container for subscription and binding entries.
 * No protocol logic here — just add / remove / query / iterate.
 */

#include "spine_sub_bind.h"
#include "../common/eebus_memory.h"
#include "../log/eebus_log.h"

#include <string.h>
#include "../platform/eebus_mutex.h"

#define LOG_TAG "spine.sb"

/* ── Internal state ───────────────────────────────────────────────────────── */

struct spine_sub_bind {
    spine_subscription_t subscriptions[SPINE_SUBSCRIPTION_MAX];
    uint16_t             sub_count;
    uint32_t             next_sub_id;

    spine_binding_t      bindings[SPINE_BINDING_MAX];
    uint16_t             bind_count;
    uint32_t             next_bind_id;

    eebus_mutex_t*       mutex;
};

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

spine_err_t spine_sub_bind_create(spine_sub_bind_t** out) {
    if (!out) return SPINE_ERR_INVALID;

    spine_sub_bind_t* sb = EEBUS_NEW(spine_sub_bind_t);
    if (!sb) return SPINE_ERR_NO_MEMORY;

    sb->sub_count  = 0;
    sb->bind_count = 0;
    eebus_mutex_create(&sb->mutex);

    *out = sb;
    return SPINE_OK;
}

void spine_sub_bind_destroy(spine_sub_bind_t* sb) {
    if (!sb) return;
    eebus_mutex_destroy(sb->mutex);
    EEBUS_FREE(sb);
}

/* ── Add ──────────────────────────────────────────────────────────────────── */

spine_err_t spine_sub_bind_add_subscription(spine_sub_bind_t*   sb,
                                             const spine_addr_t* client,
                                             const spine_addr_t* server) {
    if (!sb || !client || !server) return SPINE_ERR_INVALID;

    eebus_mutex_lock(sb->mutex);

    /* Idempotent: check for existing duplicate */
    for (uint16_t i = 0; i < sb->sub_count; i++) {
        if (spine_addr_equal(&sb->subscriptions[i].client_addr, client) &&
            spine_addr_equal(&sb->subscriptions[i].server_addr, server)) {
            eebus_mutex_unlock(sb->mutex);
            return SPINE_OK;
        }
    }

    if (sb->sub_count >= SPINE_SUBSCRIPTION_MAX) {
        eebus_mutex_unlock(sb->mutex);
        return SPINE_ERR_OVERFLOW;
    }
    sb->subscriptions[sb->sub_count].id          = ++sb->next_sub_id;
    sb->subscriptions[sb->sub_count].client_addr = *client;
    sb->subscriptions[sb->sub_count].server_addr = *server;
    sb->sub_count++;
    eebus_mutex_unlock(sb->mutex);

    EEBUS_LOG_INFO(LOG_TAG, "subscription added: client=%s server=%s",
                   client->device, server->device);
    return SPINE_OK;
}

spine_err_t spine_sub_bind_add_binding(spine_sub_bind_t*   sb,
                                        const spine_addr_t* client,
                                        const spine_addr_t* server) {
    if (!sb || !client || !server) return SPINE_ERR_INVALID;

    eebus_mutex_lock(sb->mutex);

    /* Idempotent: check for existing duplicate */
    for (uint16_t i = 0; i < sb->bind_count; i++) {
        if (spine_addr_equal(&sb->bindings[i].client_addr, client) &&
            spine_addr_equal(&sb->bindings[i].server_addr, server)) {
            eebus_mutex_unlock(sb->mutex);
            return SPINE_OK;
        }
    }

    if (sb->bind_count >= SPINE_BINDING_MAX) {
        eebus_mutex_unlock(sb->mutex);
        return SPINE_ERR_OVERFLOW;
    }
    sb->bindings[sb->bind_count].id          = ++sb->next_bind_id;
    sb->bindings[sb->bind_count].client_addr = *client;
    sb->bindings[sb->bind_count].server_addr = *server;
    sb->bind_count++;
    eebus_mutex_unlock(sb->mutex);

    EEBUS_LOG_INFO(LOG_TAG, "binding added: client=%s server=%s",
                   client->device, server->device);
    return SPINE_OK;
}

/* ── Query ────────────────────────────────────────────────────────────────── */

bool spine_sub_bind_has_subscription(const spine_sub_bind_t* sb,
                                      const spine_addr_t*     client,
                                      const spine_addr_t*     server) {
    if (!sb || !client || !server) return false;
    for (uint16_t i = 0; i < sb->sub_count; i++) {
        if (spine_addr_equal(&sb->subscriptions[i].client_addr, client) &&
            spine_addr_equal(&sb->subscriptions[i].server_addr, server))
            return true;
    }
    return false;
}

bool spine_sub_bind_has_binding(const spine_sub_bind_t* sb,
                                 const spine_addr_t*     client,
                                 const spine_addr_t*     server) {
    if (!sb || !client || !server) return false;
    for (uint16_t i = 0; i < sb->bind_count; i++) {
        if (spine_addr_equal(&sb->bindings[i].client_addr, client) &&
            spine_addr_equal(&sb->bindings[i].server_addr, server))
            return true;
    }
    return false;
}

bool spine_sub_bind_has_binding_to_server(const spine_sub_bind_t* sb,
                                           const spine_addr_t*     server) {
    if (!sb || !server) return false;
    for (uint16_t i = 0; i < sb->bind_count; i++) {
        if (spine_addr_equal(&sb->bindings[i].server_addr, server))
            return true;
    }
    return false;
}

/* ── Iterate ──────────────────────────────────────────────────────────────── */

void spine_sub_bind_foreach_subscriber(
    const spine_sub_bind_t* sb,
    const spine_addr_t*     server,
    void (*cb)(const spine_addr_t* client, void* ud),
    void*                   ud) {
    if (!sb || !server || !cb) return;
    for (uint16_t i = 0; i < sb->sub_count; i++) {
        if (spine_addr_equal(&sb->subscriptions[i].server_addr, server))
            cb(&sb->subscriptions[i].client_addr, ud);
    }
}

/* ── Remove ───────────────────────────────────────────────────────────────── */

void spine_sub_bind_remove_device(spine_sub_bind_t* sb, const char* device_id) {
    if (!sb || !device_id) return;
    eebus_mutex_lock(sb->mutex);

    uint16_t i = 0;
    while (i < sb->sub_count) {
        if (strcmp(sb->subscriptions[i].client_addr.device, device_id) == 0 ||
            strcmp(sb->subscriptions[i].server_addr.device, device_id) == 0) {
            uint16_t tail = sb->sub_count - i - 1;
            if (tail > 0)
                memmove(&sb->subscriptions[i], &sb->subscriptions[i+1],
                        tail * sizeof(spine_subscription_t));
            sb->sub_count--;
        } else { i++; }
    }

    i = 0;
    while (i < sb->bind_count) {
        if (strcmp(sb->bindings[i].client_addr.device, device_id) == 0 ||
            strcmp(sb->bindings[i].server_addr.device, device_id) == 0) {
            uint16_t tail = sb->bind_count - i - 1;
            if (tail > 0)
                memmove(&sb->bindings[i], &sb->bindings[i+1],
                        tail * sizeof(spine_binding_t));
            sb->bind_count--;
        } else { i++; }
    }

    eebus_mutex_unlock(sb->mutex);
}

void spine_sub_bind_remove_entity(spine_sub_bind_t*   sb,
                                   const spine_addr_t* entity_addr) {
    if (!sb || !entity_addr) return;
    eebus_mutex_lock(sb->mutex);

    uint16_t i = 0;
    while (i < sb->sub_count) {
        if (spine_addr_entity_is_prefix(entity_addr, &sb->subscriptions[i].client_addr) ||
            spine_addr_entity_is_prefix(entity_addr, &sb->subscriptions[i].server_addr)) {
            uint16_t tail = sb->sub_count - i - 1;
            if (tail > 0)
                memmove(&sb->subscriptions[i], &sb->subscriptions[i+1],
                        tail * sizeof(spine_subscription_t));
            sb->sub_count--;
        } else { i++; }
    }

    i = 0;
    while (i < sb->bind_count) {
        if (spine_addr_entity_is_prefix(entity_addr, &sb->bindings[i].client_addr) ||
            spine_addr_entity_is_prefix(entity_addr, &sb->bindings[i].server_addr)) {
            uint16_t tail = sb->bind_count - i - 1;
            if (tail > 0)
                memmove(&sb->bindings[i], &sb->bindings[i+1],
                        tail * sizeof(spine_binding_t));
            sb->bind_count--;
        } else { i++; }
    }

    eebus_mutex_unlock(sb->mutex);
}

void spine_sub_bind_remove_feature(spine_sub_bind_t*   sb,
                                    const spine_addr_t* feature_addr) {
    if (!sb || !feature_addr) return;
    eebus_mutex_lock(sb->mutex);

    uint16_t i = 0;
    while (i < sb->sub_count) {
        if (spine_addr_equal(&sb->subscriptions[i].client_addr, feature_addr) ||
            spine_addr_equal(&sb->subscriptions[i].server_addr, feature_addr)) {
            uint16_t tail = sb->sub_count - i - 1;
            if (tail > 0)
                memmove(&sb->subscriptions[i], &sb->subscriptions[i+1],
                        tail * sizeof(spine_subscription_t));
            sb->sub_count--;
        } else { i++; }
    }

    i = 0;
    while (i < sb->bind_count) {
        if (spine_addr_equal(&sb->bindings[i].client_addr, feature_addr) ||
            spine_addr_equal(&sb->bindings[i].server_addr, feature_addr)) {
            uint16_t tail = sb->bind_count - i - 1;
            if (tail > 0)
                memmove(&sb->bindings[i], &sb->bindings[i+1],
                        tail * sizeof(spine_binding_t));
            sb->bind_count--;
        } else { i++; }
    }

    eebus_mutex_unlock(sb->mutex);
}

spine_err_t spine_sub_bind_remove_subscription(spine_sub_bind_t*   sb,
                                                const spine_addr_t* client,
                                                const spine_addr_t* server) {
    if (!sb || !client || !server) return SPINE_ERR_INVALID;
    eebus_mutex_lock(sb->mutex);
    for (uint8_t i = 0; i < sb->sub_count; i++) {
        if (spine_addr_equal(&sb->subscriptions[i].client_addr, client) &&
            spine_addr_equal(&sb->subscriptions[i].server_addr, server)) {
            uint8_t tail = sb->sub_count - i - 1;
            if (tail > 0)
                memmove(&sb->subscriptions[i], &sb->subscriptions[i + 1],
                        tail * sizeof(spine_subscription_t));
            sb->sub_count--;
            eebus_mutex_unlock(sb->mutex);
            return SPINE_OK;
        }
    }
    eebus_mutex_unlock(sb->mutex);
    return SPINE_ERR_NOT_FOUND;
}

spine_err_t spine_sub_bind_remove_binding(spine_sub_bind_t*   sb,
                                           const spine_addr_t* client,
                                           const spine_addr_t* server) {
    if (!sb || !client || !server) return SPINE_ERR_INVALID;
    eebus_mutex_lock(sb->mutex);
    for (uint8_t i = 0; i < sb->bind_count; i++) {
        if (spine_addr_equal(&sb->bindings[i].client_addr, client) &&
            spine_addr_equal(&sb->bindings[i].server_addr, server)) {
            uint8_t tail = sb->bind_count - i - 1;
            if (tail > 0)
                memmove(&sb->bindings[i], &sb->bindings[i + 1],
                        tail * sizeof(spine_binding_t));
            sb->bind_count--;
            eebus_mutex_unlock(sb->mutex);
            return SPINE_OK;
        }
    }
    eebus_mutex_unlock(sb->mutex);
    return SPINE_ERR_NOT_FOUND;
}

uint16_t spine_sub_bind_subscription_count(const spine_sub_bind_t* sb) {
    return sb ? sb->sub_count : 0;
}

uint16_t spine_sub_bind_binding_count(const spine_sub_bind_t* sb) {
    return sb ? sb->bind_count : 0;
}

const spine_subscription_t* spine_sub_bind_get_subscription(
    const spine_sub_bind_t* sb, uint16_t idx) {
    if (!sb || idx >= sb->sub_count) return NULL;
    return &sb->subscriptions[idx];
}

const spine_binding_t* spine_sub_bind_get_binding(
    const spine_sub_bind_t* sb, uint16_t idx) {
    if (!sb || idx >= sb->bind_count) return NULL;
    return &sb->bindings[idx];
}

/* ── Conditional remove ──────────────────────────────────────────────────── */

void spine_sub_bind_remove_subscriptions_if(spine_sub_bind_t*      sb,
                                              spine_sub_bind_match_fn fn,
                                              void*                   ud) {
    if (!sb || !fn) return;
    eebus_mutex_lock(sb->mutex);
    for (int i = (int)sb->sub_count - 1; i >= 0; i--) {
        if (fn(sb->subscriptions[i].id,
               &sb->subscriptions[i].client_addr,
               &sb->subscriptions[i].server_addr, ud)) {
            uint16_t tail = sb->sub_count - (uint16_t)i - 1;
            if (tail > 0)
                memmove(&sb->subscriptions[i], &sb->subscriptions[i + 1],
                        tail * sizeof(spine_subscription_t));
            sb->sub_count--;
        }
    }
    eebus_mutex_unlock(sb->mutex);
}

void spine_sub_bind_remove_bindings_if(spine_sub_bind_t*      sb,
                                         spine_sub_bind_match_fn fn,
                                         void*                   ud) {
    if (!sb || !fn) return;
    eebus_mutex_lock(sb->mutex);
    for (int i = (int)sb->bind_count - 1; i >= 0; i--) {
        if (fn(sb->bindings[i].id,
               &sb->bindings[i].client_addr,
               &sb->bindings[i].server_addr, ud)) {
            uint16_t tail = sb->bind_count - (uint16_t)i - 1;
            if (tail > 0)
                memmove(&sb->bindings[i], &sb->bindings[i + 1],
                        tail * sizeof(spine_binding_t));
            sb->bind_count--;
        }
    }
    eebus_mutex_unlock(sb->mutex);
}
