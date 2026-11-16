// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_connection.h
 * @brief SHIP connection and handshake state machine.
 *
 * Implements the full SHIP 1.0.1 handshake flow (SHIP §13.4):
 *
 *   CMI        → Hello       → Protocol Handshake
 *   → PIN      → Access Methods → Data Exchange
 *   → Close
 *
 * Each connection is modelled as an opaque handle.  The caller provides
 * send/receive hooks (via libwebsockets) and a set of event callbacks.
 *
 * ## State Machine Overview (SHIP §13.4)
 *
 *  INIT
 *   │
 *   ▼
 *  CMI          (§13.4.3) – exchange 1-byte init frame
 *   │
 *   ▼
 *  HELLO        (§13.4.4.1) – agree to proceed
 *   │
 *   ▼
 *  PROT_HS      (§13.4.4.2) – negotiate JSON-UTF8
 *   │
 *   ▼
 *  PIN          (§13.4.5) – PIN verification (default: "none")
 *   │
 *   ▼
 *  ACCESS       (§13.4.6) – exchange access methods
 *   │
 *   ▼
 *  DATA_EXCHANGE          – SPINE payloads exchanged
 *   │
 *   ▼
 *  CLOSED       (§13.4.7)
 */

#ifndef EEBUS_SRC_SHIP_CONNECTION_SHIP_CONNECTION_H_
#define EEBUS_SRC_SHIP_CONNECTION_SHIP_CONNECTION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/eebus_error.h"
#include "../model/ship_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Connection state enum ───────────────────────────────────────────────── */

/**
 * @brief All possible states of a SHIP connection.
 *
 * Named to match the SHIP specification section numbers.
 */
typedef enum {
  /* ── CMI phase (SHIP §13.4.3) ─────────────────────────────────────────── */
  SHIP_STATE_CMI_INIT        = 0,   /**< Just connected, determining role   */
  SHIP_STATE_CMI_CLIENT_SEND,       /**< Client: sending CMI init byte      */
  SHIP_STATE_CMI_CLIENT_WAIT,       /**< Client: waiting for server CMI     */
  SHIP_STATE_CMI_SERVER_WAIT,       /**< Server: waiting for client CMI     */

  /* ── Hello phase (SHIP §13.4.4.1) ─────────────────────────────────────── */
  SHIP_STATE_HELLO_INIT,            /**< Entering Hello phase               */
  SHIP_STATE_HELLO_PENDING,         /**< Sent/received "pending"            */
  SHIP_STATE_HELLO_READY,           /**< Received "ready" from peer         */
  SHIP_STATE_HELLO_ABORT,           /**< Sending "aborted"                  */

  /* ── Protocol handshake (SHIP §13.4.4.2) ──────────────────────────────── */
  SHIP_STATE_PROT_HS_INIT,          /**< Entering handshake phase           */
  SHIP_STATE_PROT_HS_ANNOUNCE,      /**< Server: sent announceMax           */
  SHIP_STATE_PROT_HS_LISTEN,        /**< Client: waiting for select         */
  SHIP_STATE_PROT_HS_SELECT,        /**< Client: sending select             */

  /* ── PIN phase (SHIP §13.4.5) ─────────────────────────────────────────── */
  SHIP_STATE_PIN_INIT,              /**< Entering PIN phase                 */
  SHIP_STATE_PIN_CHECK,             /**< Evaluating PIN state               */

  /* ── Access Methods phase (SHIP §13.4.6) ──────────────────────────────── */
  SHIP_STATE_ACCESS_INIT,           /**< Sending accessMethodsRequest       */
  SHIP_STATE_ACCESS_WAIT,           /**< Waiting for peer accessMethods     */

  /* ── Operational ────────────────────────────────────────────────────────── */
  SHIP_STATE_DATA_EXCHANGE,         /**< Handshake complete; SPINE active   */

  /* ── Closure (SHIP §13.4.7) ───────────────────────────────────────────── */
  SHIP_STATE_CLOSE_ANNOUNCE,        /**< Sending connectionClose announce   */
  SHIP_STATE_CLOSE_WAIT,            /**< Waiting for close confirm          */
  SHIP_STATE_CLOSED,                /**< Connection fully closed            */

  /* ── Error ──────────────────────────────────────────────────────────────── */
  SHIP_STATE_ERROR,                 /**< Handshake or protocol error        */
} ship_conn_state_t;

/* ── Connection role ─────────────────────────────────────────────────────── */

/**
 * @brief Whether this node initiated the WebSocket connection.
 *
 * SHIP §13.4.3: the WebSocket client sends the CMI byte first.
 */
typedef enum {
  SHIP_ROLE_CLIENT = 0, /**< We connected to the peer (WebSocket client) */
  SHIP_ROLE_SERVER = 1, /**< The peer connected to us  (WebSocket server) */
} ship_conn_role_t;

/** @brief Opaque SHIP connection handle (forward declaration for callbacks). */
typedef struct ship_conn ship_conn_t;

/* ── Events ──────────────────────────────────────────────────────────────── */

/** Events delivered to the application via ship_conn_callbacks_t. */
typedef enum {
  SHIP_EVENT_HANDSHAKE_COMPLETE, /**< Handshake finished; SPINE can start    */
  SHIP_EVENT_SPINE_DATA,         /**< Incoming SPINE JSON payload received    */
  SHIP_EVENT_CLOSED,             /**< Connection closed (gracefully or error) */
  SHIP_EVENT_ERROR,              /**< Unrecoverable error occurred            */
} ship_conn_event_t;

/** Data delivered alongside a SHIP_EVENT_SPINE_DATA event. */
typedef struct {
  const char* json;  /**< SPINE payload JSON (valid only during callback) */
  size_t      len;   /**< Length of @p json in bytes                      */
} ship_event_spine_data_t;

/**
 * @brief Event callback table.
 *
 * All callbacks are invoked from the connection's internal thread.
 * Callbacks must not call ship_conn_send() or ship_conn_close() directly
 * from within SHIP_EVENT_CLOSED or SHIP_EVENT_ERROR.
 */
typedef struct {
  /**
   * @brief Called when the SHIP handshake completes successfully.
   *
   * @param conn      The connection that completed the handshake.
   * @param peer_ski  40-char SKI of the authenticated peer.
   * @param userdata  Forwarded from ship_conn_config_t::userdata.
   */
  void (*on_handshake_complete)(ship_conn_t* conn, const char* peer_ski,
                                void* userdata);

  /**
   * @brief Called for each incoming SPINE payload.
   *
   * @param conn      The connection that received the payload.
   * @param data      Payload data (JSON string, valid only during callback).
   * @param userdata  Forwarded from ship_conn_config_t::userdata.
   */
  void (*on_spine_data)(ship_conn_t* conn,
                        const ship_event_spine_data_t* data, void* userdata);

  /**
   * @brief Called when the connection is closed gracefully (SHIP §13.4.7).
   *
   * Only fires for graceful SHIP closes (connectionClose handshake completed
   * or maxTime elapsed after announce).  Unexpected transport drops are
   * surfaced via the ship_node layer's on_disconnected callback instead.
   *
   * @param conn      The connection that closed.
   * @param reason    Reason for the close (SHIP §13.4.7.1.1).
   * @param userdata  Forwarded from ship_conn_config_t::userdata.
   */
  void (*on_closed)(ship_conn_t* conn, ship_close_reason_t reason,
                    void* userdata);

  /**
   * @brief Called when an unrecoverable error occurs.
   *
   * @param conn      The connection that errored.
   * @param err       Error code.
   * @param userdata  Forwarded from ship_conn_config_t::userdata.
   */
  void (*on_error)(ship_conn_t* conn, eebus_err_t err, void* userdata);
} ship_conn_callbacks_t;

/* ── Trust check ─────────────────────────────────────────────────────────── */

/**
 * @brief Result returned by the trust-check callback.
 *
 * Maps directly to the three outcomes the Hello phase state machine can take
 * (SHIP §13.4.4.1): proceed as READY, wait as PENDING, or abort.
 */
typedef enum {
  /** Peer SKI is already trusted – send Hello{ready} immediately. */
  SHIP_TRUST_READY   = 0,
  /**
   * Peer SKI is not yet trusted but may be accepted (e.g. auto_accept window
   * is open) – send Hello{pending} and wait for upper layer to call
   * ship_conn_set_ready() once the user confirms.
   */
  SHIP_TRUST_PENDING = 1,
  /** Peer SKI is not trusted and will not be – send Hello{aborted}. */
  SHIP_TRUST_REJECT  = 2,
} ship_trust_result_t;

/**
 * @brief Trust-check callback invoked at the start of the Hello phase.
 *
 * Called once per connection after CMI completes, before any Hello message
 * is sent.  The callback MUST NOT call ship_conn_set_ready() or
 * ship_conn_close() on the same connection from within this callback.
 *
 * @param conn      The connection being authenticated.
 * @param peer_ski  40-char SKI extracted from the TLS peer certificate.
 * @param userdata  Forwarded from ship_conn_config_t::userdata.
 * @return SHIP_TRUST_READY / SHIP_TRUST_PENDING / SHIP_TRUST_REJECT.
 */
typedef ship_trust_result_t (*ship_conn_trust_fn)(ship_conn_t* conn,
                                                   const char*  peer_ski,
                                                   void*        userdata);

/* ── Configuration ───────────────────────────────────────────────────────── */

/**
 * @brief Configuration for a single SHIP connection.
 */
typedef struct {
  /** Connection role: CLIENT if we initiated, SERVER if peer initiated. */
  ship_conn_role_t role;

  /**
   * @brief Peer SKI extracted from the TLS certificate (40 hex chars + NUL).
   *
   * Always populated from the TLS peer cert immediately after WebSocket
   * establishment, before any SHIP application-layer messages.  Used to
   * build the session_id at connection start.
   */
  char tls_peer_ski[41];

  /**
   * @brief Our own SKI (40 hex chars + NUL).
   *
   * Sent to the peer in the accessMethods message.
   */
  char local_ski[41];

  /**
   * @brief Our mDNS service instance name (NUL-terminated).
   *
   * SHIP §13.4.6 Table 24: accessMethods.id SHALL be the unique identifier
   * that allows the peer to find us via mDNS for reverse-connection.
   * This should be the mDNS instance name (e.g. "MyEnergyManager-ABC123").
   * Used for session logging and as a fallback for accessMethods.id when
   * ship_id is empty.
   */
  char instance_name[64];

  /**
   * @brief SHIP device instance identifier used in accessMethods.id (§13.4.6).
   *
   * SHALL match the mDNS TXT "id" field (§7.3.2) so the peer can locate us
   * via mDNS for reverse reconnection.  Must be stable across certificate
   * rotation (unlike the SKI) and across network changes (unlike instance_name).
   *
   * Set by ship_node from ship_node_config_t::ship_id.  If empty,
   * instance_name is used as fallback; if that is also empty, local_ski is
   * used as last resort (non-standard, emits a warning).
   */
  char ship_id[64];

  /**
   * @brief Trust-check callback – invoked at Hello-phase entry.
   *
   * SHIP §13.4.4.1: "READY MUST ONLY be entered if the communication
   * partner is already trusted."  This callback lets the upper layer
   * make that determination per connection.  May be NULL (treated as
   * SHIP_TRUST_PENDING for every peer).
   */
  ship_conn_trust_fn on_trust_check;

  /**
   * @brief Callback invoked when the SHIP close handshake completes.
   *
   * Called from within handle_close() immediately after the state machine
   * enters SHIP_STATE_CLOSED, before the on_closed event callback.  The
   * implementation should close the underlying WebSocket connection (e.g.
   * lws_close_free_wsi()) so that the transport layer is torn down cleanly
   * instead of waiting for the peer to close TCP.
   *
   * Always called from the lws service thread, so lws APIs are safe to use
   * directly.  May be NULL if the caller does not need this notification.
   *
   * @param wsi      The wsi pointer passed to ship_conn_start().
   * @param userdata Forwarded from ship_conn_config_t::userdata.
   */
  void (*on_close_wsi)(void* wsi, void* userdata);

  /**
   * @brief Transport send callback – enqueue an outbound frame for writing.
   *
   * Called by the connection state machine whenever it needs to send a frame.
   * The implementation MUST:
   *   1. Store @p lws_buf (which starts with LWS_PRE bytes of padding)
   *      in the transport layer's per-wsi TX slot.
   *   2. Schedule a WRITEABLE callback (e.g. lws_callback_on_writable()).
   *   3. Transfer ownership of @p lws_buf; the transport layer frees it
   *      after lws_write() succeeds.
   *
   * Wire this to ship_ws_queue_frame() for the libwebsockets backend.
   * Must NOT be NULL; ship_conn_create() will log an error if it is.
   *
   * @param wsi      The wsi pointer passed to ship_conn_start().
   * @param lws_buf  (LWS_PRE + frame) buffer; caller transfers ownership.
   * @param len      Frame payload length (not counting LWS_PRE prefix).
   * @param userdata Forwarded from ship_conn_config_t::userdata.
   */
  void (*send_frame)(void* wsi, uint8_t* lws_buf, size_t len, void* userdata);

  /** Event callbacks.  All function pointers may be NULL. */
  ship_conn_callbacks_t callbacks;

  /** Opaque pointer forwarded to all callbacks and on_trust_check. */
  void* userdata;

  /**
   * @brief Hello-phase wait timeout in milliseconds.
   *
   * SHIP §13.4.4.1 Table 1: T_hello_init default = 60 s.
   * Range: 60 000 – 240 000 ms.  Use 0 for the default (60 000 ms).
   */
  unsigned int hello_timeout_ms;
} ship_conn_config_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * @brief Create a new SHIP connection state machine.
 *
 * The connection is inactive until ship_conn_start() is called.
 *
 * @param[out] conn    Receives the handle.
 * @param[in]  config  Connection configuration.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_conn_create(ship_conn_t** conn, const ship_conn_config_t* config);

/**
 * @brief Start the SHIP handshake for an already-established WebSocket.
 *
 * Begins with the CMI phase and drives the state machine until Data Exchange.
 *
 * @param[in] conn   Handle from ship_conn_create().
 * @param[in] ws_ctx Libwebsockets context pointer (cast to void* for portability).
 * @param[in] wsi    Libwebsockets wsi pointer (cast to void*).
 * @return EEBUS_OK, or EEBUS_ERR_STATE if already started.
 */
eebus_err_t ship_conn_start(ship_conn_t* conn, void* ws_ctx, void* wsi);

/**
 * @brief Feed a received WebSocket frame into the state machine.
 *
 * Call this from the libwebsockets LWS_CALLBACK_RECEIVE handler.
 *
 * @param[in] conn    Handle from ship_conn_create().
 * @param[in] data    Raw frame bytes (including the 1-byte type prefix).
 * @param[in] len     Length of @p data.
 * @return EEBUS_OK, or EEBUS_ERR_* on protocol error.
 */
eebus_err_t ship_conn_receive(ship_conn_t* conn, const uint8_t* data, size_t len);

/**
 * @brief Send a SPINE JSON payload over the connection.
 *
 * May only be called after SHIP_EVENT_HANDSHAKE_COMPLETE.
 *
 * @param[in] conn        Handle.
 * @param[in] spine_json  SPINE JSON string (NUL-terminated).
 * @return EEBUS_OK, or EEBUS_ERR_STATE if not yet in Data Exchange.
 */
eebus_err_t ship_conn_send_spine(ship_conn_t* conn, const char* spine_json);

/**
 * @brief Initiate a graceful SHIP connection close. SHIP §13.4.7
 *
 * Sends a connectionClose announce message with the given reason and waits
 * for the peer's confirm (or for maxTime to elapse).
 *
 * @param[in] conn    Handle.
 * @param[in] reason  Close reason (SHIP §13.4.7.1.1).
 *                    Use SHIP_CLOSE_REASON_UNSPECIFIC for a temporary
 *                    disconnection (reboot, etc.) and
 *                    SHIP_CLOSE_REASON_REMOVED_CONNECTION when the peer
 *                    is being removed from the trusted list.
 *                    The peer derives its reconnection intent from this field:
 *                    unspecific → reconnect expected; removedConnection → do not.
 * @return EEBUS_OK, or EEBUS_ERR_STATE if not in DATA_EXCHANGE.
 */
eebus_err_t ship_conn_close(ship_conn_t* conn,
                             ship_close_reason_t reason);

/**
 * @brief Mark the connection for destruction and release its owner reference.
 *
 * Active operations may retain the connection temporarily.  Resources are
 * freed after the last retained reference is released.
 *
 * @param[in] conn  Handle. NULL is a no-op.
 */
void ship_conn_destroy(ship_conn_t* conn);

/**
 * @brief Retain a connection while it is used outside the connection registry.
 *
 * The caller must hold the registry lock that serialises this operation with
 * ship_conn_destroy().  The retained reference must be released with
 * ship_conn_release().  Returns false when the connection is closing.
 */
bool ship_conn_retain(ship_conn_t* conn);

/**
 * @brief Release a temporary connection reference.
 */
void ship_conn_release(ship_conn_t* conn);

/**
 * @brief Query the current state of the connection.
 *
 * @param[in] conn  Handle.
 * @return Current state enum value.
 */
ship_conn_state_t ship_conn_get_state(const ship_conn_t* conn);

/**
 * @brief Replace the userdata pointer forwarded to all callbacks.
 *
 * Use this to swap in a per-connection context after the connection slot
 * address is stable (i.e. after allocation but before ship_conn_start()).
 *
 * @param[in] conn      Handle.
 * @param[in] userdata  New userdata pointer.
 */
void ship_conn_set_userdata(ship_conn_t* conn, void* userdata);

/**
 * @brief Promote a PENDING Hello connection to READY.
 *
 * Called by the upper layer (e.g. ship_node) after the user has confirmed
 * trust for the peer SKI, or after auto_accept has accepted the peer.
 *
 * SHIP §13.4.4.1: switching from PENDING → READY sends Hello{ready} and,
 * if the peer has already sent Hello{ready}, immediately advances to the
 * Protocol Handshake phase (HELLO_OK).
 *
 * Has no effect if the connection is not currently in HELLO_PENDING state
 * (e.g. already advanced or already closed).
 *
 * @param[in] conn  Handle.
 */
void ship_conn_set_ready(ship_conn_t* conn);

/**
 * @brief Reject a connection that is waiting in Hello PENDING state.
 *
 * Sends Hello{aborted} immediately and closes the WebSocket, without
 * waiting for the Hello timeout (SHIP §13.4.4.1).
 *
 * No-op if the connection is not currently in HELLO_PENDING.
 *
 * @param conn  Connection handle.
 */
void ship_conn_reject(ship_conn_t* conn);

/**
 * @brief Return the authenticated peer SKI.
 *
 * This is the SKI derived from the peer's TLS certificate, set at connection
 * start and guaranteed to be cryptographically authentic.  It is the single
 * source of truth for peer identity and is available from the moment the
 * WebSocket is established (ship_conn_start()).
 *
 * Note: this is distinct from ship_conn_get_peer_access_id(), which returns
 * the self-reported accessMethods.id (§13.4.6) used only for reverse
 * reconnection and has NO authentication value.
 *
 * @param[in] conn  Handle.
 * @return 40-char SKI hex string, or empty string if not yet available.
 */
const char* ship_conn_get_peer_ski(const ship_conn_t* conn);

/**
 * @brief Return the peer's self-reported access-methods ID (§13.4.6).
 *
 * This is the value of accessMethods.id received from the peer during the
 * Access Methods Identification exchange.  Its purpose is exclusively for
 * reverse reconnection (§13.4.6.2.1) so that a DEV-SERVER can later initiate
 * a connection back to the DEV-CLIENT.
 *
 * WARNING: This value is NOT authenticated.  Do NOT use it for identity
 * checks or trust decisions.  Use ship_conn_get_peer_ski() for those.
 *
 * May be an empty string if:
 *   - The access methods exchange has not yet completed, OR
 *   - The peer sent an empty accessMethods.id (permitted by §13.4.6 Table 24).
 *
 * @param[in] conn  Handle.
 * @return Access-methods ID string (never NULL; may be empty).
 */
const char* ship_conn_get_peer_access_id(const ship_conn_t* conn);

/**
 * @brief Return the TLS peer SKI (alias for ship_conn_get_peer_ski).
 *
 * Both functions now return the same value: the SKI derived from the peer's
 * TLS certificate, set at ship_conn_start().  This alias is retained so that
 * call-sites that explicitly want "the TLS cert SKI before handshake
 * completes" remain self-documenting.
 *
 * @param[in] conn  Handle.
 * @return 40-char SKI string, or empty string if not available.
 */
const char* ship_conn_get_tls_peer_ski(const ship_conn_t* conn);

/**
 * @brief Return the session identifier for this connection.
 *
 * Format: "[localSKI8↔peerSKI8]"  (generated at connection start from
 * the TLS-derived SKIs, before any SHIP handshake messages are exchanged).
 * Stable for the lifetime of the connection; safe to use as a grep key.
 *
 * @param[in] conn  Handle.
 * @return Session ID string, or "[????????↔????????]" if not yet set.
 */
const char* ship_conn_get_session_id(const ship_conn_t* conn);

/**
 * @brief Return the pending close reason recorded on this connection.
 *
 * SHIP_CLOSE_REASON_NONE means no graceful close has been initiated
 * (local or remote).  Any other value means a connectionClose was either
 * initiated locally via ship_conn_close() or received from the peer.
 *
 * Used by ship_node to decide whether an unexpected transport drop should
 * trigger a reconnect: NONE → unexpected → reconnect; otherwise the close
 * was intentional.
 *
 * @param[in] conn  Handle.
 * @return Recorded close reason, or SHIP_CLOSE_REASON_NONE if none.
 */
ship_close_reason_t ship_conn_get_pending_close_reason(const ship_conn_t* conn);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_CONNECTION_SHIP_CONNECTION_H_ */
