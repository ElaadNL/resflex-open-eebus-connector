// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_sub_bind.h
 *
 * Pure data container for SPINE subscription and binding entries.
 * Protocol logic (sending requests, handling incoming requests, notify
 * fan-out) lives in spine_node_mgmt.
 *
 * This header is internal to the spine layer and should not be included
 * by UC or application code.
 */

#ifndef SPINE_SUB_BIND_H_
#define SPINE_SUB_BIND_H_

#include "codec/spine_codec_address.h"
#include "codec/spine_codec_common.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Max entries ──────────────────────────────────────────────────────────── */

#define SPINE_SUBSCRIPTION_MAX  64
#define SPINE_BINDING_MAX       64

/* ── Entry types ──────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t     id;
    spine_addr_t client_addr;
    spine_addr_t server_addr;
} spine_subscription_t;

typedef struct {
    uint32_t     id;
    spine_addr_t client_addr;
    spine_addr_t server_addr;
} spine_binding_t;

/* ── Forward declaration ──────────────────────────────────────────────────── */

typedef struct spine_sub_bind spine_sub_bind_t;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

spine_err_t spine_sub_bind_create(spine_sub_bind_t** out);
void        spine_sub_bind_destroy(spine_sub_bind_t* sb);

/* ── Add ──────────────────────────────────────────────────────────────────── */

spine_err_t spine_sub_bind_add_subscription(spine_sub_bind_t*   sb,
                                             const spine_addr_t* client,
                                             const spine_addr_t* server);

spine_err_t spine_sub_bind_add_binding(spine_sub_bind_t*   sb,
                                        const spine_addr_t* client,
                                        const spine_addr_t* server);

/* ── Query ────────────────────────────────────────────────────────────────── */

bool spine_sub_bind_has_subscription(const spine_sub_bind_t* sb,
                                      const spine_addr_t*     client,
                                      const spine_addr_t*     server);

bool spine_sub_bind_has_binding(const spine_sub_bind_t* sb,
                                 const spine_addr_t*     client,
                                 const spine_addr_t*     server);

/** Check if any binding exists targeting the given server address. */
bool spine_sub_bind_has_binding_to_server(const spine_sub_bind_t* sb,
                                           const spine_addr_t*     server);

/* ── Iterate subscribers of a server feature (for notify fan-out) ─────────── */

void spine_sub_bind_foreach_subscriber(
    const spine_sub_bind_t* sb,
    const spine_addr_t*     server,
    void (*cb)(const spine_addr_t* client, void* ud),
    void*                   ud);

/* ── Remove ───────────────────────────────────────────────────────────────── */

void spine_sub_bind_remove_device(spine_sub_bind_t* sb, const char* device_id);
void spine_sub_bind_remove_entity(spine_sub_bind_t* sb, const spine_addr_t* entity_addr);
void spine_sub_bind_remove_feature(spine_sub_bind_t* sb, const spine_addr_t* feature_addr);

/* ── Count / Get (for encoding subscription/binding data responses) ─────── */

uint16_t spine_sub_bind_subscription_count(const spine_sub_bind_t* sb);
uint16_t spine_sub_bind_binding_count(const spine_sub_bind_t* sb);

const spine_subscription_t* spine_sub_bind_get_subscription(
    const spine_sub_bind_t* sb, uint16_t idx);
const spine_binding_t* spine_sub_bind_get_binding(
    const spine_sub_bind_t* sb, uint16_t idx);

/* ── Remove ───────────────────────────────────────────────────────────────── */

/** Remove a specific subscription by client+server address pair. */
spine_err_t spine_sub_bind_remove_subscription(spine_sub_bind_t*   sb,
                                                const spine_addr_t* client,
                                                const spine_addr_t* server);

/** Remove a specific binding by client+server address pair. */
spine_err_t spine_sub_bind_remove_binding(spine_sub_bind_t*   sb,
                                           const spine_addr_t* client,
                                           const spine_addr_t* server);

/* ── Conditional remove (callback-based filter) ──────────────────────────── */

/**
 * @brief Match callback: return true if the entry should be removed.
 * @param id       subscription/binding ID
 * @param client   entry's client address
 * @param server   entry's server address
 * @param ud       user data from caller
 */
typedef bool (*spine_sub_bind_match_fn)(uint32_t             id,
                                         const spine_addr_t*  client,
                                         const spine_addr_t*  server,
                                         void*                ud);

/** Remove all subscriptions for which match_fn returns true. */
void spine_sub_bind_remove_subscriptions_if(spine_sub_bind_t*      sb,
                                              spine_sub_bind_match_fn fn,
                                              void*                   ud);

/** Remove all bindings for which match_fn returns true. */
void spine_sub_bind_remove_bindings_if(spine_sub_bind_t*      sb,
                                         spine_sub_bind_match_fn fn,
                                         void*                   ud);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_SUB_BIND_H_ */
