// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_node.h
 * @brief SHIP node — top-level manager for a local SHIP endpoint.
 *
 * ship_node_t is the main entry point for the SHIP layer.  It integrates:
 *
 *   - TLS credential management (ship_tls_t)
 *   - mDNS advertisement and discovery (ship_mdns_t)
 *   - WebSocket server accepting inbound connections (ship_ws_t)
 *   - WebSocket client connections to discovered peers (ship_ws_t)
 *   - Per-connection SHIP handshake state machines (ship_conn_t)
 *   - SKI whitelist: only trusted peers reach Data Exchange
 *
 * Key difference from openeebus
 * ------------------------------
 * openeebus manages one connection at a time.  ship_node_t supports
 * concurrent connections — one ship_conn_t per active wsi — fulfilling
 * a common requirement for HEMS nodes that talk to multiple devices.
 *
 * ## Typical usage
 * @code
 *   ship_node_config_t cfg = {
 *     .instance_name  = "MyHEMS-001",
 *     .port           = 7711,
 *     .register_flag  = true,
 *     .cert_path      = "/etc/eebus/cert.pem",
 *     .key_path       = "/etc/eebus/key.pem",
 *   };
 *   cfg.callbacks.on_spine_data = my_spine_handler;
 *   cfg.callbacks.userdata      = my_app;
 *
 *   ship_node_t* node = NULL;
 *   ship_node_create(&node, &cfg);
 *   ship_node_start(node);
 *
 *   // Trust a peer discovered over mDNS:
 *   ship_node_trust_ski(node, "a1b2c3....");
 *
 *   // Send SPINE payload on a specific connection (session_id obtained
 *   // from the on_connected callback):
 *   ship_node_send_spine(node, session_id, spine_json);
 *
 *   ship_node_stop(node);
 *   ship_node_destroy(node);
 * @endcode
 */

#ifndef EEBUS_SRC_SHIP_SHIP_NODE_H_
#define EEBUS_SRC_SHIP_SHIP_NODE_H_

#include <stdbool.h>
#include <stddef.h>

#include "../common/eebus_error.h"
#include "connection/ship_connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of concurrent SHIP connections. */
#define SHIP_NODE_MAX_CONNECTIONS (16U)

/** Maximum number of trusted SKIs in the whitelist. */
#define SHIP_NODE_MAX_TRUSTED_SKIS (64U)

/* ── Event callbacks ─────────────────────────────────────────────────────── */

/**
 * @brief Called when a new peer completes the SHIP handshake.
 *
 * @param session_id Unique session identifier "[localSKI16↔peerSKI16]".
 * @param peer_ski  Authenticated SKI of the connected peer (40 hex chars).
 * @param userdata  Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_connected_cb)(const char* session_id,
                                       const char* peer_ski, void* userdata);

/**
 * @brief Called when a SPINE JSON payload arrives from a peer.
 *
 * @param session_id  Unique session identifier "[localSKI8↔peerSKI8]".
 * @param peer_ski    SKI of the sending peer.
 * @param spine_json  SPINE JSON string (valid only for the duration of this call).
 * @param len         Length of @p spine_json.
 * @param userdata    Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_spine_data_cb)(const char* session_id,
                                        const char* peer_ski,
                                        const char* spine_json,
                                        size_t      len,
                                        void*       userdata);

/**
 * @brief Called when a peer disconnects.
 *
 * @param session_id   Unique session identifier "[localSKI8↔peerSKI8]".
 * @param peer_ski     SKI of the disconnected peer.
 * @param reason       SHIP close reason (SHIP §13.4.7.1.1) for graceful
 *                     closes, or SHIP_CLOSE_REASON_NONE for unexpected
 *                     transport drops (network failure, peer crash, …).
 * @param userdata     Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_disconnected_cb)(const char*         session_id,
                                          const char*         peer_ski,
                                          ship_close_reason_t reason,
                                          void*               userdata);

/**
 * @brief Called when an outbound connection attempt fails before the SHIP
 *        handshake completes.
 *
 * Fires only for connections initiated via ship_node_connect_peer() (or
 * auto-triggered from mDNS / reconnect) where the handshake never reached
 * DATA_EXCHANGE.  Inbound connection failures are silent (the peer
 * initiated them; there is no caller to notify).
 *
 * Not paired with on_connected.  If on_connected has fired, a subsequent
 * drop fires on_disconnected instead.
 *
 * Parameters match ship_node_connect_peer() so the application can
 * correlate the failure with the original call.
 *
 * @param host      Peer host/IP that was dialled.
 * @param port      Peer port that was dialled.
 * @param path      WebSocket path.
 * @param hostname  mDNS hostname for TLS SNI (may be empty).
 * @param err       Error code describing the failure cause.
 * @param userdata  Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_connect_error_cb)(const char* host,
                                            uint16_t    port,
                                            const char* path,
                                            const char* hostname,
                                            eebus_err_t err,
                                            void*       userdata);

/**
 * @brief Complete information of a discovered peer (value type, safe to cache and pass around).
 *
 * Used as the unified parameter type for the on_peer_discovered callback and trust_prompt_fn.
 * All string fields are NUL-terminated char arrays, safe to use across callback boundaries.
 */
typedef struct {
  char     instance_name[64];  /**< mDNS instance name                        */
  char     ski[41];            /**< 40-char SKI hex + NUL                     */
  char     id[64];             /**< TXT "id" — device instance unique identifier */
  char     host[256];          /**< TCP connection target (IP address string)  */
  char     hostname[256];      /**< mDNS hostname (for TLS SNI, §9.4)         */
  uint16_t port;               /**< WebSocket port                            */
  char     path[128];          /**< WebSocket path (typically "/ship/")        */
  uint8_t  txtvers;            /**< TXT "txtvers" value (currently always 1)   */
  bool     register_flag;      /**< TXT "register" value                      */
  char     brand[64];          /**< TXT "brand" — manufacturer name (optional) */
  char     model[64];          /**< TXT "model" — device model name (optional) */
  char     type[64];           /**< TXT "type"  — device type (optional)       */
} ship_peer_info_t;

/**
 * @brief Called when a new SHIP peer is discovered via mDNS.
 *
 * The application decides whether to trust the peer by calling
 * ship_node_trust_ski() and/or ship_node_connect_peer().
 *
 * @param peer      Complete peer information from mDNS discovery.
 * @param userdata  Forwarded.
 */
typedef void (*ship_node_peer_discovered_cb)(
    const ship_peer_info_t* peer,
    void*                   userdata
);

/**
 * @brief Called when an inbound connection arrives from an unknown (untrusted)
 *        peer and the node enters Hello PENDING state (SHIP §12.3.1.2).
 *
 * The application MUST inform the user, showing @p peer_ski (40 hex chars) so
 * the user can verify the remote device's identity before accepting.
 *
 * To accept:  call ship_node_trust_ski(node, peer_ski)  – advances the
 *             connection automatically.
 * To reject:  call ship_node_reject_ski(node, peer_ski) – sends Hello{aborted}
 *             immediately without waiting for the Hello timeout.
 * To ignore:  do nothing – connection aborts after the Hello timeout (60 s).
 *
 * @param peer_ski  40-char hex SKI of the connecting peer.
 * @param userdata  Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_pairing_requested_cb)(
    const char* peer_ski,
    void*       userdata
);

/**
 * @brief Called when a previously discovered peer disappears from mDNS.
 *
 * The node automatically:
 *   - Invalidates the peer's discovery cache entry.
 *   - Cancels any pending reconnect timer for this peer (no point retrying
 *     when the peer has explicitly withdrawn its mDNS advertisement).
 *
 * The application should update its UI / state accordingly.  If the peer
 * is currently connected, @p on_disconnected will fire separately when the
 * WebSocket connection drops; this callback is purely a discovery-layer event.
 *
 * @param peer      Snapshot of the peer's last known information (id, ski,
 *                  instance_name, host, …).  Use @p peer->id as the stable
 *                  device identifier — it is guaranteed not to change across
 *                  mDNS re-announcements (SHIP §7.3.2).  The pointer is valid
 *                  only for the duration of the callback.
 * @param userdata  Forwarded from ship_node_callbacks_t::userdata.
 */
typedef void (*ship_node_peer_lost_cb)(
    const ship_peer_info_t* peer,
    void*                   userdata
);

/** Callback table for ship_node_t events. */
typedef struct {
  ship_node_connected_cb         on_connected;
  ship_node_spine_data_cb        on_spine_data;
  ship_node_disconnected_cb      on_disconnected;
  /**
   * Called when an outbound connection attempt fails before the handshake
   * completes.  NULL = no notification.
   */
  ship_node_connect_error_cb     on_connect_error;
  ship_node_peer_discovered_cb   on_peer_discovered;
  /**
   * Called when a previously discovered mDNS peer withdraws its advertisement.
   * NULL = no notification.
   */
  ship_node_peer_lost_cb         on_peer_lost;
  /**
   * Called when an unknown inbound peer enters Hello PENDING (§12.3.1.2).
   * NULL = no notification (connection will wait until Hello timeout).
   */
  ship_node_pairing_requested_cb on_pairing_requested;
  void* userdata; /**< Forwarded to all callbacks. */
} ship_node_callbacks_t;

/* ── Configuration ───────────────────────────────────────────────────────── */

/** Configuration for a SHIP node. */
typedef struct {
  /**
   * @brief mDNS instance name for this node.
   *
   * Identifies the device on the local network (e.g. "HeatPump-SN12345").
   * Must be unique on the subnet.
   */
  char instance_name[64];

  /**
   * @brief SHIP device instance identifier (§7.3.2 "id" TXT field).
   *
   * Unique string identifying this device instance across its lifetime,
   * e.g. a serial number or UUID.  Used in:
   *   - mDNS TXT record "id" field (§7.3.2) — lets peers find us via mDNS
   *     for reverse reconnection.
   *   - accessMethods.id response (§13.4.6 Table 24) — same purpose in-band.
   *
   * The id SHOULD remain stable across certificate rotation and network
   * changes, which is why it is separate from instance_name (display name)
   * and from the SKI (certificate-bound).
   *
   * If empty, instance_name is used as fallback for backward compatibility.
   */
  char ship_id[64];

  /** WebSocket port to listen on.  Default: SHIP_WS_DEFAULT_PORT (7711). */
  uint16_t port;

  /**
   * @brief mDNS "register" flag – initial value at node start.
   *
   * Per SHIP §7.3.2 and §12.3.1.1 this field advertises whether the auto_accept
   * window is currently open.  It is set dynamically by
   * ship_node_start_auto_accept() / ship_node_stop_auto_accept().
   *
   * Set to false here (the default).  Use ship_node_start_auto_accept() to
   * open a timed window when the user requests pairing.
   */
  bool register_flag;

  /**
   * @brief Path to the PEM certificate file.
   *
   * If both cert_path and key_path are NULL, a new self-signed certificate
   * is generated automatically and NOT persisted.
   */
  const char* cert_path;

  /** Path to the PEM private key file. */
  const char* key_path;

  /** Event callbacks.  Individual pointers may be NULL. */
  ship_node_callbacks_t callbacks;

  /**
   * @brief Hello-phase timeout in ms. 0 = use default (60 000 ms).
   *
   * SHIP §13.4.4.1 Table 1: allowed range 60 000 – 240 000 ms.
   */
  unsigned int hello_timeout_ms;

  /** Device brand / manufacturer. Advertised in mDNS TXT "brand" field. */
  char brand[64];

  /** Device type string, e.g. "HeatPump". Advertised in mDNS TXT "type". */
  char type[64];

  /** Device model name. Advertised in mDNS TXT "model" field. */
  char model[64];
} ship_node_config_t;

/** @brief Opaque SHIP node handle. */
typedef struct ship_node ship_node_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * @brief Create a SHIP node.
 *
 * Loads or generates TLS credentials but does not start listening.
 *
 * @param[out] node    Receives the handle.
 * @param[in]  config  Node configuration.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_node_create(ship_node_t** node, const ship_node_config_t* config);

/**
 * @brief Start the node: begin advertising, browsing, and accepting connections.
 *
 * @param[in] node  Handle.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_node_start(ship_node_t* node);

/**
 * @brief Stop all activity and close all connections.
 *
 * Blocks until all background threads have exited.
 *
 * @param[in] node  Handle.
 */
void ship_node_stop(ship_node_t* node);

/**
 * @brief Destroy the node and free all resources.
 *
 * ship_node_stop() must be called first.  NULL is a no-op.
 */
void ship_node_destroy(ship_node_t* node);

/* ── Trust management ────────────────────────────────────────────────────── */

/**
 * @brief Open the auto_accept pairing window (SHIP §12.3.1.1).
 *
 * While open, the first unknown peer SKI received in the Hello phase is
 * automatically trusted (user trust level = 8) and the window closes.
 * The mDNS "register" TXT field is set to "true" while the window is open.
 *
 * @param[in] node         Handle.
 * @param[in] duration_ms  Window duration in ms (0 = default 60 000 ms;
 *                         clamped to spec maximum of 120 000 ms).
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_node_start_auto_accept(ship_node_t* node, unsigned int duration_ms);

/**
 * @brief Close the auto_accept window early.
 *
 * No-op if the window is not open.  Called automatically after the first
 * unknown SKI is accepted and when the window timer expires.
 *
 * @param[in] node  Handle.
 */
void ship_node_stop_auto_accept(ship_node_t* node);

/**
 * @brief Add a SKI to the trusted-peer whitelist.
 *
 * Connections from peers whose certificate SKI matches a trusted SKI will
 * be allowed through to Data Exchange.  If a connection for this SKI is
 * currently waiting in the Hello PENDING state, it is activated immediately.
 *
 * Thread-safe; may be called at any time.
 *
 * @param[in] node  Handle.
 * @param[in] ski   40-char hex SKI string to trust.
 * @return EEBUS_OK, EEBUS_ERR_EXISTS (already trusted), or EEBUS_ERR_OVERFLOW
 *         (whitelist full).
 */
eebus_err_t ship_node_trust_ski(ship_node_t* node, const char* ski);

/**
 * @brief Remove a SKI from the trusted-peer whitelist.
 *
 * If the peer is currently connected its connection is closed gracefully.
 *
 * @param[in] node  Handle.
 * @param[in] ski   SKI to remove.
 * @return EEBUS_OK or EEBUS_ERR_NOT_FOUND.
 */
eebus_err_t ship_node_revoke_ski(ship_node_t* node, const char* ski);

/**
 * @brief Reject an inbound pairing request from an unknown peer.
 *
 * Finds any connection in Hello PENDING state whose TLS SKI matches @p ski
 * and immediately sends Hello{aborted}, closing the connection without waiting
 * for the Hello timeout (SHIP §13.4.4.1).
 *
 * Typically called from the on_pairing_requested callback after the user
 * declines the incoming pairing request.
 *
 * @param[in] node  Handle.
 * @param[in] ski   40-char hex SKI of the peer to reject.
 * @return EEBUS_OK if at least one PENDING connection was aborted,
 *         EEBUS_ERR_NOT_FOUND if no matching PENDING connection exists.
 */
eebus_err_t ship_node_reject_ski(ship_node_t* node, const char* ski);

/**
 * @brief Check whether a SKI is in the trusted whitelist.
 *
 * @param[in] node  Handle.
 * @param[in] ski   SKI to check.
 * @return true if trusted.
 */
bool ship_node_is_trusted(const ship_node_t* node, const char* ski);

/* ── Peer management ─────────────────────────────────────────────────────── */

/**
 * @brief Initiate an outbound SHIP connection to a peer discovered via mDNS.
 *
 * The peer's SKI must already be trusted via ship_node_trust_ski(), or the
 * connection will be rejected at the Hello phase.
 *
 * @param[in] node      Handle.
 * @param[in] host      Peer IP address (TCP connect target).
 * @param[in] port      Peer WebSocket port.
 * @param[in] path      WebSocket path (e.g. "/ship/").
 * @param[in] hostname  mDNS hostname for TLS SNI (§9.4); pass NULL to use host.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_node_connect_peer(
    ship_node_t* node,
    const char*  host,
    uint16_t     port,
    const char*  path,
    const char*  hostname
);

/* ── Data exchange ───────────────────────────────────────────────────────── */

/**
 * @brief Send a SPINE JSON payload on a specific SHIP connection.
 *
 * session_id is the connection handle obtained from on_connected (also
 * carried by on_spine_data and on_disconnected).  When multiple connections
 * to the same peer exist (transient during §12.2.2 duplicate-connection
 * dedup, or in pathological cases), session_id unambiguously selects one.
 *
 * @param[in] node        Handle.
 * @param[in] session_id  Connection handle (NUL-terminated).
 * @param[in] spine_json  NUL-terminated SPINE JSON string.
 * @return EEBUS_OK, EEBUS_ERR_NOT_FOUND (no such connection), or
 *         EEBUS_ERR_STATE (not yet in Data Exchange).
 */
eebus_err_t ship_node_send_spine(
    ship_node_t* node,
    const char*  session_id,
    const char*  spine_json
);

/**
 * @brief Return the local node's SKI (from its TLS certificate).
 *
 * @param[in]  node     Handle.
 * @param[out] ski_out  Buffer of at least 41 bytes.
 * @return EEBUS_OK or EEBUS_ERR_STATE if TLS not yet initialised.
 */
eebus_err_t ship_node_get_local_ski(const ship_node_t* node, char ski_out[41]);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_SHIP_NODE_H_ */
