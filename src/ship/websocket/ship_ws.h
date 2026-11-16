// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_ws.h
 * @brief SHIP WebSocket server and client using libwebsockets.
 *
 * SHIP uses binary WebSocket frames over mTLS (SHIP §13).  This module:
 *
 *   - Exposes a server that accepts inbound SHIP connections.
 *   - Provides a client that connects to a discovered SHIP peer.
 *   - Passes each accepted/connected wsi to ship_conn_t for handshaking.
 *
 * The libwebsockets service loop runs in a dedicated thread owned by
 * ship_ws_t.  Callbacks arrive on that thread; ship_conn_receive() is
 * thread-safe.
 *
 * ## SHIP WebSocket requirements (SHIP §13.2)
 * - Sub-protocol : "ship" (Sec-WebSocket-Protocol header)
 * - Frame type   : binary
 * - Port         : configurable, default 7711
 * - TLS          : mandatory mTLS; server requests client certificate
 */

#ifndef EEBUS_SRC_SHIP_WEBSOCKET_SHIP_WS_H_
#define EEBUS_SRC_SHIP_WEBSOCKET_SHIP_WS_H_

#include <stdbool.h>
#include <stdint.h>

#include "../../common/eebus_error.h"
#include "../tls/ship_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/** SHIP WebSocket sub-protocol name. SHIP §13.2 */
#define SHIP_WS_SUBPROTOCOL "ship"

/** Default SHIP WebSocket port. */
#define SHIP_WS_DEFAULT_PORT (7711U)

/** @brief Opaque WebSocket server/client handle. */
typedef struct ship_ws ship_ws_t;

/**
 * @brief Callback invoked when a new inbound connection is established
 *        (server mode) or the outbound connection succeeds (client mode).
 *
 * The peer SKI is extracted from the TLS peer certificate immediately after
 * the TLS handshake, before any SHIP application-layer messages are exchanged.
 *
 * @param wsi       libwebsockets connection instance (cast to void*).
 * @param peer_ip   Peer IP address string.
 * @param peer_ski  40-char SKI hex string extracted from peer TLS cert,
 *                  or empty string if extraction failed.
 * @param is_server true  = peer connected to us (we are WebSocket server).
 *                  false = we connected to peer (we are WebSocket client).
 *                  Maps directly to SHIP_ROLE_SERVER / SHIP_ROLE_CLIENT.
 * @param ws        The ship_ws_t handle for this connection.  For client
 *                  connections the caller may read ws->remote_host,
 *                  ws->remote_hostname, ws->remote_port, and ws->remote_path
 *                  to obtain the outbound address without relying on any
 *                  shared node-level "pending" state.
 * @param userdata  Forwarded from ship_ws_config_t::userdata.
 */
typedef void (*ship_ws_connected_cb)(void* wsi, const char* peer_ip,
                                     const char* peer_ski, bool is_server,
                                     struct ship_ws* ws,
                                     void* userdata);

/**
 * @brief Callback invoked when a frame arrives on a connection.
 *
 * @param wsi      libwebsockets connection instance.
 * @param data     Raw frame bytes (type byte + JSON payload).
 * @param len      Length of @p data.
 * @param userdata Forwarded from ship_ws_config_t::userdata.
 */
typedef void (*ship_ws_receive_cb)(void* wsi, const uint8_t* data, size_t len, void* userdata);

/**
 * @brief Callback invoked when a connection is closed.
 *
 * @param wsi      libwebsockets connection instance.
 * @param userdata Forwarded from ship_ws_config_t::userdata.
 */
typedef void (*ship_ws_closed_cb)(void* wsi, void* userdata);

/**
 * @brief Configuration for a ship_ws instance.
 */
typedef struct {
  /** TLS credentials (certificate + key).  Must remain valid for the
   *  lifetime of ship_ws_t. */
  const ship_tls_cred_t* tls;

  /** Port to listen on (server mode) or connect from (client mode).
   *  Use SHIP_WS_DEFAULT_PORT (7711) if unsure. */
  uint16_t port;

  /** Callbacks */
  ship_ws_connected_cb on_connected;
  ship_ws_receive_cb   on_receive;
  ship_ws_closed_cb    on_closed;

  /** Opaque pointer forwarded to all callbacks. */
  void* userdata;
} ship_ws_config_t;

/**
 * @brief Create a SHIP WebSocket server.
 *
 * Listens on all interfaces at config->port.
 * Call ship_ws_start() to begin accepting connections.
 *
 * @param[out] ws      Receives the handle.
 * @param[in]  config  Server configuration.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_ws_server_create(ship_ws_t** ws, const ship_ws_config_t* config);

/**
 * @brief Connect to a remote SHIP node (client mode).
 *
 * The connection attempt is asynchronous; on_connected fires on success.
 *
 * @param[out] ws        Receives the handle.
 * @param[in]  config    Client configuration.
 * @param[in]  host      Remote IP address or hostname (used for TCP connect).
 * @param[in]  port      Remote port.
 * @param[in]  path      WebSocket path (e.g. "/ship/").
 * @param[in]  hostname  mDNS hostname for SNI (§9.4); if NULL, host is used.
 *                       When host is an IP address, pass the mDNS hostname
 *                       here so that the TLS ClientHello carries the correct
 *                       ServerName extension.
 * @return EEBUS_OK or EEBUS_ERR_*.
 */
eebus_err_t ship_ws_client_connect(
    ship_ws_t**             ws,
    const ship_ws_config_t* config,
    const char*             host,
    uint16_t                port,
    const char*             path,
    const char*             hostname
);

/**
 * @brief Start the libwebsockets service loop in a background thread.
 *
 * @param[in] ws  Handle.
 * @return EEBUS_OK or EEBUS_ERR_INTERNAL.
 */
eebus_err_t ship_ws_start(ship_ws_t* ws);

/**
 * @brief Stop the service loop and wait for the background thread to exit.
 *
 * @param[in] ws  Handle.
 */
void ship_ws_stop(ship_ws_t* ws);

/**
 * @brief Destroy the handle and free all resources.
 *
 * ship_ws_stop() must be called first.  NULL is a no-op.
 *
 * @param[in] ws  Handle.
 */
void ship_ws_destroy(ship_ws_t* ws);

/**
 * @brief Send a binary frame on a specific wsi.
 *
 * Must be called from within a LWS_CALLBACK_*_WRITEABLE callback or with
 * lws_callback_on_writable() having been requested first.
 *
 * @param[in] wsi   libwebsockets connection (as void*).
 * @param[in] data  Frame bytes to send (must be preceded by LWS_PRE bytes).
 * @param[in] len   Length of the payload (not counting LWS_PRE prefix).
 * @return EEBUS_OK or EEBUS_ERR_IO.
 */
eebus_err_t ship_ws_send(void* wsi, uint8_t* data, size_t len);

/**
 * @brief Enqueue an outbound frame into the lws per-session TX buffer.
 *
 * This is the correct TX path for the SHIP connection layer.  The caller
 * allocates @p lws_buf with (LWS_PRE + len) bytes, copies the payload at
 * offset LWS_PRE, and passes ownership to this function.
 *
 * Internals: stores the buffer in the wsi's ship_ws_per_session_t (managed
 * by lws) and requests a WRITEABLE callback.  The WRITEABLE callback then
 * calls lws_write() and frees the buffer.  If a previous frame has not yet
 * been flushed it is discarded (single-frame queue; SHIP control messages
 * are small and the connection layer sends one at a time).
 *
 * Thread-safe: calls from the LWS service thread use
 * lws_callback_on_writable(); calls from other threads use
 * lws_cancel_service() to wake the service thread.
 *
 * @param[in] wsi      libwebsockets connection (as void*).
 * @param[in] lws_buf  LWS_PRE + payload buffer (ownership transferred).
 * @param[in] len      Payload length (not counting LWS_PRE prefix).
 */
void ship_ws_queue_frame(void* wsi, uint8_t* lws_buf, size_t len, void* userdata);

/**
 * @brief Close a specific WebSocket connection.
 *
 * Initiates a clean WebSocket close handshake on @p wsi. Safe to call from
 * the lws service thread or another thread; cross-thread calls are marshalled
 * to the lws service thread.
 *
 * Intended for use as ship_conn_config_t::on_close_wsi so that the
 * transport is torn down as soon as the SHIP close handshake completes,
 * rather than waiting for the peer to close TCP.
 *
 * @param[in] wsi      libwebsockets connection (as void*).
 * @param[in] userdata Ignored; present so the function matches the
 *                     ship_conn_config_t::on_close_wsi signature.
 */
void ship_ws_close_wsi(void* wsi, void* userdata);
/**
 * @brief Retrieve the TCP connect target (IP or hostname) for a client ws.
 *
 * Only valid for ship_ws instances created by ship_ws_client_connect().
 */
const char* ship_ws_get_remote_host(const ship_ws_t* ws);

/**
 * @brief Retrieve the mDNS hostname used for SNI on a client ws (§9.4).
 *
 * Only valid for ship_ws instances created by ship_ws_client_connect().
 */
const char* ship_ws_get_remote_hostname(const ship_ws_t* ws);

/**
 * @brief Retrieve the remote port for a client ws.
 */
uint16_t ship_ws_get_remote_port(const ship_ws_t* ws);

/**
 * @brief Retrieve the WebSocket path for a client ws.
 */
const char* ship_ws_get_remote_path(const ship_ws_t* ws);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_WEBSOCKET_SHIP_WS_H_ */
