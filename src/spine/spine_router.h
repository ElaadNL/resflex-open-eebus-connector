// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_router.h
 *
 * SPINE message router.
 *
 * Responsibilities:
 *   - Holds local device and remote device collection
 *   - Creates remote device on SHIP connection establishment, triggers discovery state machine
 *   - Destroys remote device on SHIP disconnection
 *   - Routes received SPINE JSON messages to the correct device
 *   - Sends messages from devices to the SHIP layer via send_fn (keyed by session_id)
 *
 * Identity model:
 *   Remote devices are primarily keyed by session_id (the SHIP connection
 *   handle — unique per connection instance).  peer_ski remains stored on
 *   each remote device for logging/information and is available via
 *   spine_router_find_remote_by_ski when needed, but the canonical lookup
 *   used by dispatch, on_disconnect, and send is by session_id.
 *
 * Dispatch routing rules:
 *   Messages arriving with a given session_id are routed to the remote device
 *   created for that session.  spine_device_dispatch handles decoding,
 *   looks up get_local(remote)->features[] to find the handler, and calls
 *   the handler with remote_dev itself as the dev parameter.
 *
 * Send routing rules:
 *   Routes directly using dev->session_id; dev must be a remote device.
 */

#ifndef SPINE_ROUTER_H_
#define SPINE_ROUTER_H_

#include "spine_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

#define SPINE_ROUTER_PEER_MAX  32

/* ── send_fn ──────────────────────────────────────────────────────────────── */

/* Send callback from SPINE to the SHIP layer.
 * session_id is the SHIP connection handle (see ship_conn_get_session_id). */
typedef void (*spine_send_fn_t)(const char* session_id,
                                 const char* json,
                                 void*       userdata);

/* ── Forward declaration ──────────────────────────────────────────────────── */
/* spine_router_t is already forward-declared in spine_device.h */

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Create a spine_router.
 *
 * @param out        Output handle
 * @param local_dev  Local device (all features/usecases already registered)
 * @param send_fn    SHIP send function, parameters are (session_id, json, ud)
 * @param send_ud    send_fn userdata
 */
spine_err_t spine_router_create(spine_router_t** out,
                                  spine_device_t*  local_dev,
                                  spine_send_fn_t  send_fn,
                                  void*            send_ud);

void spine_router_destroy(spine_router_t* router);

/* ── SHIP connection events ───────────────────────────────────────────────── */

/**
 * @brief Called when a SHIP connection is established.
 *
 * Internally creates a remote device (is_remote=true), fills in peer_ski,
 * and triggers the discovery state machine (sends READ nodeManagementDetailedDiscoveryData).
 */
spine_err_t spine_router_on_connect(spine_router_t* router,
                                     const char*     peer_ski,
                                     const char*     session_id);

/**
 * @brief Called when a SHIP connection is disconnected.
 *
 * Destroys the corresponding remote device (identified by session_id) and
 * cleans up subscription/binding relationships owned by that remote.
 */
void spine_router_on_disconnect(spine_router_t* router, const char* session_id);

/* ── Message routing ──────────────────────────────────────────────────────── */

/**
 * @brief Called when a SHIP message is received, routes to the correct device.
 *
 * @param router      Router handle
 * @param session_id  Connection handle of the message source
 * @param json        SPINE JSON string
 */
spine_err_t spine_router_dispatch(spine_router_t* router,
                                   const char*     session_id,
                                   const char*     json);

/* ── Internal send interface (called by spine_device_send) ────────────────
 * Not exposed to the application layer, but needs to be visible in spine_device.c.
 */
void spine_router_send(spine_router_t* router,
                        spine_device_t* dev,
                        const char*     json);

/* ── Query ────────────────────────────────────────────────────────────────── */

/** Find remote device by session_id (primary lookup).  Returns NULL if not found. */
spine_device_t* spine_router_find_remote_by_session(spine_router_t* router,
                                                     const char*     session_id);

/** Find remote device by SKI (first match).  Returns NULL if not found.
 *  NOTE: during §12.2.2 dedup transient or pathological multi-connection
 *  cases, multiple remotes may share the same SKI; this returns the first. */
spine_device_t* spine_router_find_remote_by_ski(spine_router_t* router,
                                                  const char*     peer_ski);

/** Find remote device by SPINE device_id, returns NULL if not found */
spine_device_t* spine_router_find_remote_by_id(spine_router_t* router,
                                                 const char*     device_id);

/** Return the local device */
spine_device_t* spine_router_get_local(spine_router_t* router);

/** Iterate all connected remote devices */
typedef void (*spine_router_foreach_fn)(spine_device_t* remote, void* ud);
void spine_router_foreach_remote(spine_router_t*       router,
                                  spine_router_foreach_fn cb,
                                  void*                   ud);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_ROUTER_H_ */
