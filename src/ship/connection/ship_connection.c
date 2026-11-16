// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_connection.c
 * @brief SHIP 1.0.1 handshake state machine implementation.
 *
 * This file implements the complete SHIP handshake as defined in
 * EEBus_SHIP_TS_Specification_v1.0.1, chapter 13.4:
 *
 *  §13.4.3  CMI   – Connection Mode Initialisation
 *  §13.4.4.1 Hello – mutual agreement to proceed
 *  §13.4.4.2 Protocol Handshake – agree on JSON-UTF8
 *  §13.4.5  PIN   – PIN verification (we only support "none")
 *  §13.4.6  Access Methods – exchange connection identifiers
 *  §13.4.7  Close – graceful termination
 *
 * Threading model
 * ---------------
 * All state transitions are serialised through a mutex. The libwebsockets
 * callbacks (receive, writeable) arrive on the lws service thread; the
 * application may call ship_conn_send_spine() / ship_conn_close() from any
 * thread. Both paths hold the mutex while mutating state.
 */

#include "ship_connection.h"

#include <inttypes.h>
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "../../log/eebus_log.h"
#include "../../common/eebus_memory.h"
#include "../../platform/eebus_mutex.h"
#include "../../platform/eebus_timer.h"
#include "../model/ship_model.h"

#define LOG_TAG "ship"

/* Hello-phase default timer value – SHIP §13.4.4.1 Table 1: range 60-240s */
#define HELLO_TIMEOUT_MS_DEFAULT (60000U)

/*
 * Hello prolongation constants – SHIP §13.4.4.1 Table 1:
 *   T_hello_prolong_waiting_gap  = 30 s (we send prolongation request this
 *                                        many ms BEFORE the peer's waiting expires)
 *   T_hello_prolong_min          = 1 s  (minimum gap; below this, timer disabled)
 *
 * §13.4.4.1: Send-Prolongation-Request-Timer fires when a PENDING node needs
 * to ask the other side to extend their Wait-For-Ready-Timer.
 */
#define HELLO_PROLONG_GAP_MS      (15000U)  /* T_hello_prolong_waiting_gap = 15 s */
#define HELLO_PROLONG_MIN_MS       (1000U)  /* T_hello_prolong_min         =  1 s */
/*
 * T_hello_prolong_thr_inc = 30 s – SHIP §13.4.4.1 Table 1.
 * Send-Prolongation-Request-Timer is armed ONLY when the peer's "waiting"
 * value is >= this threshold; otherwise it is deactivated.
 */
#define HELLO_PROLONG_THR_INC_MS  (30000U)  /* T_hello_prolong_thr_inc     = 30 s */

/* CMI-phase timeout – SHIP §13.4.3: allowed range 10–30 s; recommended 30 s */
#define CMI_TIMEOUT_MS (15000U)

/*
 * connectionClose maxTime – SHIP §13.4.7.1.2 rule 1b:
 * "maxTime" SHALL be included in every close announce and denotes the latest
 * time (in ms from the moment the announce is sent) at which the initiator
 * will close the connection, even without a confirm from the peer.
 * We use 10 s as a conservative value well within normal operation.
 */
#define CLOSE_MAX_TIME_MS (10000U)

/* ── Internal structure ──────────────────────────────────────────────────── */

struct ship_conn {
  ship_conn_config_t  config;
  ship_conn_state_t   state;
  eebus_mutex_t*      mutex;

  /* The registry owns one reference; in-flight operations own the others. */
  atomic_uint refs;
  atomic_bool closing;

  /* libwebsockets handles (opaque to avoid lws header dependency in .h) */
  struct lws_context* lws_ctx;
  struct lws*         wsi;

  /*
   * Authenticated peer identity.
   *
   * Populated in ship_conn_start() directly from config.tls_peer_ski, which
   * holds the SKI extracted from the peer's TLS certificate during the mTLS
   * handshake.  This is the only authoritative peer identity: it is
   * cryptographically bound to the certificate and cannot be spoofed by
   * application-layer messages.
   *
   * Note: accessMethods.id (§13.4.6) is intentionally NOT stored here.
   * That field is the peer's self-reported "unique ID" used exclusively for
   * reverse reconnection (§13.4.6.2.1) and has no bearing on identity.
   */
  char peer_ski[41];

  /*
   * Peer access methods – stored for reverse reconnection (§13.4.6.2.1).
   *
   * These fields come from the peer's accessMethods JSON message and are
   * persisted so that a DEV-SERVER can later initiate a connection back to
   * the DEV-CLIENT.  They carry NO authentication weight; the authoritative
   * identity is always peer_ski (from TLS).
   *
   * peer_access_id   : accessMethods.id   – peer's self-reported unique ID
   *                    (may be an empty string if the peer has no unique ID)
   * peer_dns_sd_mdns : accessMethods.dnsSd_mDns – peer is reachable via mDNS
   * peer_dns_uri     : accessMethods.dns.uri    – wss:// URI for unicast DNS
   *                    Heap-allocated; freed in ship_conn_destroy().
   */
  char  peer_access_id[41];
  bool  peer_dns_sd_mdns;
  char* peer_dns_uri;

  /**
   * Session identifier – format "[localSKI8↔peerSKI8]".
   * Generated in ship_conn_start() from TLS-derived SKIs; stable for the
   * lifetime of the connection.  All log records carry this prefix so that
   * `grep session_id` extracts the full two-device interaction trace.
   *
   * UTF-8: '[' + 8 + U+2194(3B) + 8 + ']' + NUL = 21 bytes → 24 for safety.
   */
  char session_id[48];

  /* Hello-phase watchdog timer */
  eebus_timer_t* hello_timer;

  /*
   * Send-Prolongation-Request-Timer – SHIP §13.4.4.1.
   * Active only when we are in HELLO_PENDING.  Fires T_hello_prolong_waiting_gap
   * ms before the peer's Wait-For-Ready-Timer would expire, prompting us to
   * send Hello{pending, prolongationRequest=true}.
   */
  eebus_timer_t* prolong_timer;

  /*
   * Prolongation-Request-Reply-Timer – SHIP §13.4.4.1 PENDING_TIMEOUT rule 2b-c.
   * Started after we send a prolongationRequest.  If the peer does not reply
   * before this fires, we execute the common "abort" procedure.
   * Stopped when any Hello message is received from the peer.
   */
  eebus_timer_t* prolong_reply_timer;

  /*
   * Last "waiting" value (in ms) received from the peer in a Hello message.
   * Used to calculate the next Send-Prolongation-Request-Timer interval and
   * to initialize Prolongation-Request-Reply-Timer.
   */
  uint32_t last_peer_waiting_ms;

  /* CMI-phase watchdog timer – SHIP §13.4.3: 10–30 s timeout */
  eebus_timer_t* cmi_timer;

  /*
   * Protocol-handshake Wait-Timer – SHIP §13.4.4.2.3.
   * Started at entry to the handshake phase (both CLIENT and SERVER).
   * Default 10 s. Stopped when the handshake advances to PIN phase or aborts.
   */
  eebus_timer_t* prot_hs_timer;

  /*
   * connectionClose maxTime watchdog – SHIP §13.4.7.1.2 rule 3:
   * Started when we send a close announce.  If the confirm is not received
   * before this fires, we force-close the WebSocket immediately.
   */
  eebus_timer_t* close_timer;

  /*
   * PIN ASK_INIT timeout – SHIP §13.4.4.3.5.2:
   * "The SME User SHALL close the connection if this state (SME_PIN_STATE_ASK_INIT)
   * lasts more than 10 seconds."
   * Started in enter_pin() when we send our pinState.
   * Stopped on receipt of the first pinState message from the peer.
   */
  eebus_timer_t* pin_ask_init_timer;

  /*
   * Access Methods response timeout – SHIP §13.4.6.2.1:
   * "The sender ... CAN close the connection if it does not receive a proper
   * SME 'Access methods' message within 60 seconds."
   * Started in enter_data_exchange() when we send accessMethodsRequest (server).
   * Stopped on receipt of accessMethods message.
   */
  eebus_timer_t* access_methods_request_timer;

  /**
   * Set to true when the peer sends Hello{ready} while we are still in
   * HELLO_PENDING.  When ship_conn_set_ready() is later called, this flag
   * lets us skip READY_LISTEN and jump straight to HELLO_OK (Protocol
   * Handshake).  SHIP §13.4.4.1 PENDING_LISTEN rule 2.
   */
  bool hello_peer_ready;

  /**
   * Reason passed to ship_conn_close() by the local initiator.  Stored here
   * so the confirm branch of handle_close() can pass it back to on_closed
   * instead of hardcoding UNSPECIFIC.
   */
  ship_close_reason_t pending_close_reason;
};

static void ship_conn_free(ship_conn_t* conn) {
  eebus_timer_destroy(conn->cmi_timer);
  eebus_timer_destroy(conn->hello_timer);
  eebus_timer_destroy(conn->prolong_timer);
  eebus_timer_destroy(conn->prolong_reply_timer);
  eebus_timer_destroy(conn->prot_hs_timer);
  eebus_timer_destroy(conn->pin_ask_init_timer);
  eebus_timer_destroy(conn->access_methods_request_timer);
  eebus_timer_destroy(conn->close_timer);
  eebus_mutex_destroy(conn->mutex);
  EEBUS_FREE(conn->peer_dns_uri);
  EEBUS_FREE(conn);
}

/* ── Forward declarations ────────────────────────────────────────────────── */

static void enter_state(ship_conn_t* c, ship_conn_state_t new_state);
static eebus_err_t send_raw(ship_conn_t* c, ship_msg_type_t type, const ship_msg_t* msg);
static void on_hello_timeout(void* userdata);
static void on_cmi_timeout(void* userdata);
static void on_close_timeout(void* userdata);
static void on_prolong_timeout(void* userdata);
static void on_prolong_reply_timeout(void* userdata);
static void on_prot_hs_timeout(void* userdata);
static void on_pin_ask_init_timeout(void* userdata);
static void on_access_methods_request_timeout(void* userdata);
static void close_on_protocol_error(ship_conn_t* c, eebus_err_t err);
static eebus_err_t send_close_announce(ship_conn_t* c, ship_close_reason_t reason);
static void hello_abort(ship_conn_t* c, const char* reason);

/**
 * Validate that buf[0..len-1] is well-formed UTF-8.
 * Returns true if valid, false if any illegal byte sequence is found.
 */
static bool is_valid_utf8(const uint8_t* buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = buf[i];
        size_t  n;
        if      (b <= 0x7F) { i++; continue; }
        else if ((b & 0xE0) == 0xC0) n = 1;
        else if ((b & 0xF0) == 0xE0) n = 2;
        else if ((b & 0xF8) == 0xF0) n = 3;
        else return false;

        if (i + n >= len) return false;
        for (size_t j = 1; j <= n; j++) {
            if ((buf[i + j] & 0xC0) != 0x80)
                return false;
        }
        i += n + 1;
    }
    return true;
}
static eebus_err_t enter_pin(ship_conn_t* c);
static uint32_t hello_remaining_ms(const ship_conn_t* c);

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char* state_name(ship_conn_state_t s) {
  switch (s) {
    case SHIP_STATE_CMI_INIT:         return "CMI_INIT";
    case SHIP_STATE_CMI_CLIENT_SEND:  return "CMI_CLIENT_SEND";
    case SHIP_STATE_CMI_CLIENT_WAIT:  return "CMI_CLIENT_WAIT";
    case SHIP_STATE_CMI_SERVER_WAIT:  return "CMI_SERVER_WAIT";
    case SHIP_STATE_HELLO_INIT:       return "HELLO_INIT";
    case SHIP_STATE_HELLO_PENDING:    return "HELLO_PENDING";
    case SHIP_STATE_HELLO_READY:      return "HELLO_READY";
    case SHIP_STATE_HELLO_ABORT:      return "HELLO_ABORT";
    case SHIP_STATE_PROT_HS_INIT:     return "PROT_HS_INIT";
    case SHIP_STATE_PROT_HS_ANNOUNCE: return "PROT_HS_ANNOUNCE";
    case SHIP_STATE_PROT_HS_LISTEN:   return "PROT_HS_LISTEN";
    case SHIP_STATE_PROT_HS_SELECT:   return "PROT_HS_SELECT";
    case SHIP_STATE_PIN_INIT:         return "PIN_INIT";
    case SHIP_STATE_PIN_CHECK:        return "PIN_CHECK";
    case SHIP_STATE_ACCESS_INIT:      return "ACCESS_INIT";
    case SHIP_STATE_ACCESS_WAIT:      return "ACCESS_WAIT";
    case SHIP_STATE_DATA_EXCHANGE:    return "DATA_EXCHANGE";
    case SHIP_STATE_CLOSE_ANNOUNCE:   return "CLOSE_ANNOUNCE";
    case SHIP_STATE_CLOSE_WAIT:       return "CLOSE_WAIT";
    case SHIP_STATE_CLOSED:           return "CLOSED";
    case SHIP_STATE_ERROR:            return "ERROR";
    default:                          return "UNKNOWN";
  }
}

/**
 * @brief Pretty-print a JSON string and emit it as a DEBUG log record.
 *
 * Parses @p json with cJSON so the output is consistently indented regardless
 * of whether the input was compact or already formatted.  Falls back to
 * logging the raw string if parsing fails.
 */
static void log_json(const char* session_id,
                     const char* direction, /* ">>" or "<<" */
                     const char* json) {
  EEBUS_LOG_DEBUG(LOG_TAG, "%s SHIP %s %s", session_id, direction, json);
}

/* ── State transition helpers ────────────────────────────────────────────── */

static void enter_state(ship_conn_t* c, ship_conn_state_t new_state) {
  EEBUS_LOG_INFO(LOG_TAG, "%s state: %s → %s",
                 c->session_id, state_name(c->state), state_name(new_state));
  c->state = new_state;
}

/* ── Frame sending ───────────────────────────────────────────────────────── */

/**
 * @brief Serialise @p msg and enqueue it for sending via the transport layer.
 *
 * Calls config.send_frame() with an allocated (LWS_PRE + payload) buffer.
 * Ownership of the buffer transfers to the transport; the transport frees
 * it after the write succeeds.
 */
static eebus_err_t send_raw(ship_conn_t* c, ship_msg_type_t type, const ship_msg_t* msg) {
  char* json = msg ? ship_msg_to_json(msg) : NULL;

  /* Log the outgoing message before encoding into a wire frame */
  if (json) {
    log_json(c->session_id, ">>", json);
  }

  size_t   frame_len;
  uint8_t* frame = ship_frame_encode(type, json, &frame_len);
  EEBUS_FREE(json);

  if (!frame) return EEBUS_ERR_NO_MEMORY;

  /* LWS requires LWS_PRE bytes of padding before the payload.
   * Ownership of this buffer transfers to config.send_frame() which is
   * responsible for freeing it (e.g. after lws_write() in the WRITEABLE
   * callback). */
  uint8_t* lws_buf = EEBUS_MALLOC(LWS_PRE + frame_len);
  if (!lws_buf) { EEBUS_FREE(frame); return EEBUS_ERR_NO_MEMORY; }
  memcpy(lws_buf + LWS_PRE, frame, frame_len);
  EEBUS_FREE(frame);

  if (!c->config.send_frame) {
    EEBUS_LOG_ERROR(LOG_TAG, "%s send_frame callback is NULL – frame dropped!",
                    c->session_id);
    EEBUS_FREE(lws_buf);
    return EEBUS_ERR_INTERNAL;
  }

  /* Enqueue the frame; the transport layer owns lws_buf from this point. */
  c->config.send_frame(c->wsi, lws_buf, frame_len, c->config.userdata);
  return EEBUS_OK;
}

/* ── CMI phase (SHIP §13.4.3) ────────────────────────────────────────────── */

static eebus_err_t handle_cmi_init(ship_conn_t* c) {
  /* CMI message: type=INIT, value=0x00 — SHIP §13.4.3 */

  /*
   * Start the CMI watchdog.  SHIP §13.4.3 timeout procedure: both CLIENT
   * (waiting for server's CMI reply) and SERVER (waiting for client's CMI)
   * must close the connection if no CMI message is received within
   * CmiTimeout (10–30 s; we use the minimum of 10 s).
   */
  if (eebus_timer_create(&c->cmi_timer, on_cmi_timeout, c) == EEBUS_OK) {
    eebus_timer_start(c->cmi_timer, CMI_TIMEOUT_MS, false);
  } else {
    EEBUS_LOG_WARN(LOG_TAG, "%s Failed to create CMI timer", c->session_id);
  }

  if (c->config.role == SHIP_ROLE_CLIENT) {
    /* §13.4.3 §1.1: client sends CMI byte first */
    enter_state(c, SHIP_STATE_CMI_CLIENT_SEND);

    uint8_t cmi_payload[2] = { SHIP_MSG_TYPE_INIT, 0x00 };
    uint8_t* lws_buf = EEBUS_MALLOC(LWS_PRE + 2);
    if (!lws_buf) return EEBUS_ERR_NO_MEMORY;
    memcpy(lws_buf + LWS_PRE, cmi_payload, 2);

    if (c->config.send_frame) {
      c->config.send_frame(c->wsi, lws_buf, 2, c->config.userdata);
    } else {
      EEBUS_FREE(lws_buf);
    }
    enter_state(c, SHIP_STATE_CMI_CLIENT_WAIT);
  } else {
    /* §13.4.3 §2.1: server waits for client CMI */
    enter_state(c, SHIP_STATE_CMI_SERVER_WAIT);
  }
  return EEBUS_OK;
}

static eebus_err_t handle_cmi_frame(ship_conn_t* c, const uint8_t* data, size_t len) {
  /*
   * SHIP §13.4.3 CMI_STATE_SERVER_EVALUATE / CMI_STATE_CLIENT_EVALUATE
   *
   * A valid CMI frame is exactly: MessageType=0x00 (init), CmiHead=0x00.
   * Both fields must be present (len >= 2).
   *
   * SERVER rules (§2.2.x): regardless of whether the received frame is
   *   valid or not, SERVER must ALWAYS send CMI{0} back first.
   *   - §2.2.2 (CmiHead == 0): send CMI{0}, proceed to Hello.
   *   - §2.2.1 (MessageType != 0) or §2.2.3 (CmiHead > 0): send CMI{0},
   *     then close the connection.
   *
   * CLIENT rules (§3.2.x): CLIENT must NOT send anything on error.
   *   - §3.2.2 (CmiHead == 0): proceed to Hello.
   *   - §3.2.1 (MessageType != 0) or §3.2.3 (CmiHead > 0): close immediately.
   */
  bool type_ok  = (len >= 1 && data[0] == SHIP_MSG_TYPE_INIT);
  bool value_ok = (len >= 2 && data[1] == 0x00);
  bool valid    = type_ok && value_ok;

  if (c->config.role == SHIP_ROLE_SERVER) {
    /* SERVER always sends CMI{0} back, whether the received frame is
     * valid or not (§2.2.1, §2.2.2, §2.2.3 all require sending first). */
    uint8_t cmi_payload[2] = { SHIP_MSG_TYPE_INIT, 0x00 };
    uint8_t* lws_buf = EEBUS_MALLOC(LWS_PRE + 2);
    if (lws_buf) {
      memcpy(lws_buf + LWS_PRE, cmi_payload, 2);
      if (c->config.send_frame) {
        c->config.send_frame(c->wsi, lws_buf, 2, c->config.userdata);
      } else {
        EEBUS_FREE(lws_buf);
      }
    }

    if (!valid) {
      EEBUS_LOG_ERROR(LOG_TAG,
                      "%s CMI SERVER: invalid frame (type=0x%02x head=0x%02x) – "
                      "sent CMI{0}, closing",
                      c->session_id,
                      len >= 1 ? data[0] : 0xFF,
                      len >= 2 ? data[1] : 0xFF);
      return EEBUS_ERR_SHIP_HANDSHAKE;
    }

    EEBUS_LOG_DEBUG(LOG_TAG, "%s CMI SERVER: sending CMI response byte", c->session_id);
    return EEBUS_OK;
  }

  /* CLIENT */
  if (!valid) {
    EEBUS_LOG_ERROR(LOG_TAG,
                    "%s CMI CLIENT: invalid frame (type=0x%02x head=0x%02x) – closing",
                    c->session_id,
                    len >= 1 ? data[0] : 0xFF,
                    len >= 2 ? data[1] : 0xFF);
    return EEBUS_ERR_SHIP_HANDSHAKE;
  }

  EEBUS_LOG_DEBUG(LOG_TAG, "%s CMI complete, entering Hello phase", c->session_id);
  return EEBUS_OK;
}

/* ── Hello phase (SHIP §13.4.4.1) ───────────────────────────────────────── */

/**
 * @brief Unified handler for unrecoverable protocol errors.
 *
 * Transitions the state machine to ERROR, closes the WebSocket via
 * on_close_wsi (so we do not wait for the peer to close TCP), and
 * fires the on_error callback.  Caller must hold conn->mutex.
 */
static void close_on_protocol_error(ship_conn_t* c, eebus_err_t err) {
  enter_state(c, SHIP_STATE_ERROR);
  if (c->config.on_close_wsi) {
    c->config.on_close_wsi(c->wsi, c->config.userdata);
  }
  if (c->config.callbacks.on_error) {
    c->config.callbacks.on_error(c, err, c->config.userdata);
  }
}

/** CMI watchdog – fires if neither side sends its CMI byte in time. */
static void on_cmi_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  EEBUS_LOG_ERROR(LOG_TAG, "%s CMI timeout – closing connection", c->session_id);
  close_on_protocol_error(c, EEBUS_ERR_TIMEOUT);

  eebus_mutex_unlock(c->mutex);
}

/**
 * connectionClose maxTime watchdog – SHIP §13.4.7.1.2 rule 3.
 *
 * Fires when we sent a close announce but did not receive a confirm within
 * CLOSE_MAX_TIME_MS.  We force-close the WebSocket immediately.
 */
static void on_close_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  EEBUS_LOG_WARN(LOG_TAG,
                 "%s connectionClose maxTime elapsed without confirm – "
                 "forcing close (SHIP §13.4.7.1.2)",
                 c->session_id);

  enter_state(c, SHIP_STATE_CLOSED);

  if (c->config.on_close_wsi) {
    c->config.on_close_wsi(c->wsi, c->config.userdata);
  }
  if (c->config.callbacks.on_closed) {
    c->config.callbacks.on_closed(c, c->pending_close_reason,
                                  c->config.userdata);
  }

  eebus_mutex_unlock(c->mutex);
}

/* ── Hello prolongation helpers (SHIP §13.4.4.1) ────────────────────────── */

/**
 * @brief (Re-)arm the Send-Prolongation-Request-Timer for a PENDING node.
 *
 * SHIP §13.4.4.1: the timer value is max(peer_waiting - T_hello_prolong_waiting_gap, 0).
 * If the result is less than T_hello_prolong_min, disable the timer.
 *
 * @param c               Connection handle (mutex must be held by caller).
 * @param peer_waiting_ms Most recently received "waiting" value from peer (ms).
 */
static void prolong_timer_rearm(ship_conn_t* c, uint32_t peer_waiting_ms) {
  /*
   * SHIP §13.4.4.1 PENDING_LISTEN rules 2 and 3:
   * "If the received sub-element 'waiting' is >= T_hello_prolong_thr_inc (30 s):
   *  Initialize Send-Prolongation-Request-Timer … Otherwise: Deactivate."
   */
  if (peer_waiting_ms < HELLO_PROLONG_THR_INC_MS) {
    eebus_timer_stop(c->prolong_timer);
    EEBUS_LOG_DEBUG(LOG_TAG,
                    "%s Prolongation: peer_waiting=%u ms < thr_inc=%u ms – "
                    "Send-Prolongation-Request-Timer disabled (§13.4.4.1)",
                    c->session_id, peer_waiting_ms, HELLO_PROLONG_THR_INC_MS);
    return;
  }

  uint32_t interval_ms = peer_waiting_ms - HELLO_PROLONG_GAP_MS;
  if (interval_ms < HELLO_PROLONG_MIN_MS) {
    eebus_timer_stop(c->prolong_timer);
    EEBUS_LOG_DEBUG(LOG_TAG,
                    "%s Prolongation: interval %u ms < min %u ms – timer disabled",
                    c->session_id, interval_ms, HELLO_PROLONG_MIN_MS);
    return;
  }

  if (!c->prolong_timer) {
    eebus_timer_create(&c->prolong_timer, on_prolong_timeout, c);
  }
  if (c->prolong_timer) {
    eebus_timer_start(c->prolong_timer, interval_ms, false);
    EEBUS_LOG_DEBUG(LOG_TAG,
                    "%s Prolongation: Send-Prolongation-Request-Timer armed %u ms",
                    c->session_id, interval_ms);
  }
}

/**
 * Send-Prolongation-Request-Timer callback – SHIP §13.4.4.1 PENDING_TIMEOUT.
 *
 * Fires when a PENDING node must ask the peer to extend its Wait-For-Ready-Timer.
 * Sends Hello{pending, prolongationRequest=true}.
 */
static void on_prolong_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  if (c->state != SHIP_STATE_HELLO_PENDING) {
    eebus_mutex_unlock(c->mutex);
    return;
  }

  EEBUS_LOG_DEBUG(LOG_TAG,
                  "%s Prolongation: Send-Prolongation-Request-Timer fired – "
                  "sending prolongationRequest",
                  c->session_id);

  ship_msg_t req = {
    .kind  = SHIP_MSG_KIND_HELLO,
    .hello = {
      .phase                = SHIP_HELLO_PHASE_PENDING,
      .prolongation_request = true,
      /* waiting and other sub-elements SHALL NOT be set – §13.4.4.1 */
    },
  };
  send_raw(c, SHIP_MSG_TYPE_CONTROL, &req);

  /*
   * §13.4.4.1 PENDING_TIMEOUT rule 2b-c: start Prolongation-Request-Reply-Timer.
   * Value = last received peer "waiting", or 1.1 × current hello_timer remaining
   * if no "waiting" was ever received.
   */
  uint32_t reply_timeout_ms = c->last_peer_waiting_ms > 0
      ? c->last_peer_waiting_ms
      : (uint32_t)(hello_remaining_ms(c) * 11U / 10U);

  if (reply_timeout_ms == 0) reply_timeout_ms = HELLO_TIMEOUT_MS_DEFAULT;

  if (!c->prolong_reply_timer) {
    eebus_timer_create(&c->prolong_reply_timer, on_prolong_reply_timeout, c);
  }
  if (c->prolong_reply_timer) {
    eebus_timer_start(c->prolong_reply_timer, reply_timeout_ms, false);
    EEBUS_LOG_DEBUG(LOG_TAG,
                    "%s Prolongation: Reply-Timer armed %u ms (§13.4.4.1)",
                    c->session_id, reply_timeout_ms);
  }

  eebus_mutex_unlock(c->mutex);
}

/**
 * Prolongation-Request-Reply-Timer callback – SHIP §13.4.4.1 rule 3.
 * Fires when the peer does not reply to our prolongationRequest in time.
 */
static void on_prolong_reply_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  if (c->state != SHIP_STATE_HELLO_PENDING) {
    eebus_mutex_unlock(c->mutex);
    return;
  }

  EEBUS_LOG_WARN(LOG_TAG,
                 "%s Prolongation-Request-Reply-Timer expired – aborting (§13.4.4.1)",
                 c->session_id);
  hello_abort(c, "Prolongation-Request-Reply-Timer expired");

  eebus_mutex_unlock(c->mutex);
}

/**
 * Protocol-handshake Wait-Timer callback – SHIP §13.4.4.2.3 TIMEOUT state.
 * Fires when the peer does not complete its handshake step in 10 s.
 */
static void on_prot_hs_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  EEBUS_LOG_ERROR(LOG_TAG,
                  "%s Protocol handshake Wait-Timer expired – sending error and closing "
                  "(§13.4.4.2.3)",
                  c->session_id);

  /* §13.4.4.2.3 abort procedure: send error then close */
  ship_msg_t err_msg = {
    .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
    .handshake_err = { .error = SHIP_HANDSHAKE_ERR_TIMEOUT },
  };
  send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
  close_on_protocol_error(c, EEBUS_ERR_TIMEOUT);

  eebus_mutex_unlock(c->mutex);
}

static void on_hello_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  EEBUS_LOG_WARN(LOG_TAG, "%s Hello timeout – sending abort", c->session_id);
  /*
   * hello_abort() sends Hello{aborted}, closes the WebSocket via on_close_wsi,
   * and fires on_closed.  This is consistent with the TRUST_REJECT path and
   * avoids the double state transition (HELLO_ABORT → ERROR) that occurred
   * when close_on_protocol_error() was called directly here.
   */
  hello_abort(c, "Hello timeout");

  eebus_mutex_unlock(c->mutex);
}

/**
 * @brief Return the remaining milliseconds on the Hello Wait-For-Ready-Timer.
 *
 * Used to populate the "waiting" sub-element as required by SHIP §13.4.4.1:
 * "Sub-element \'waiting\' set to the current value of Wait-For-Ready-Timer
 * if Wait-For-Ready-Timer is active."
 */
static uint32_t hello_remaining_ms(const ship_conn_t* c) {
  if (!c->hello_timer) return 0;
  return eebus_timer_remaining_ms(c->hello_timer);
}

/**
 * @brief Send Hello{aborted} and transition to HELLO_ABORT.
 * Caller must hold the mutex.
 */
static void hello_abort(ship_conn_t* c, const char* reason) {
  eebus_timer_stop(c->hello_timer);
  eebus_timer_stop(c->prolong_timer);
  eebus_timer_stop(c->prolong_reply_timer);
  enter_state(c, SHIP_STATE_HELLO_ABORT);
  EEBUS_LOG_INFO(LOG_TAG, "%s Hello abort: %s", c->session_id,
                 reason ? reason : "");

  ship_msg_t abort_msg = {
    .kind  = SHIP_MSG_KIND_HELLO,
    .hello = { .phase = SHIP_HELLO_PHASE_ABORTED },
  };
  send_raw(c, SHIP_MSG_TYPE_CONTROL, &abort_msg);

  close_on_protocol_error(c, EEBUS_ERR_SHIP_REJECTED);
}

/**
 * @brief Entry point for the Hello phase (called after CMI completes).
 *
 * SHIP §13.4.4.1: invokes on_trust_check to determine initial basic state:
 *  - SHIP_TRUST_READY   → send Hello{ready},   enter HELLO_READY
 *  - SHIP_TRUST_PENDING → send Hello{pending}, enter HELLO_PENDING
 *  - SHIP_TRUST_REJECT  → send Hello{aborted}, close connection
 */
static eebus_err_t enter_hello(ship_conn_t* c) {
  enter_state(c, SHIP_STATE_HELLO_INIT);

  /* Start Wait-For-Ready-Timer – SHIP §13.4.4.1 Table 1 */
  unsigned int timeout_ms = c->config.hello_timeout_ms > 0
                                ? c->config.hello_timeout_ms
                                : HELLO_TIMEOUT_MS_DEFAULT;
  eebus_err_t rc = eebus_timer_create(&c->hello_timer, on_hello_timeout, c);
  if (rc == EEBUS_OK) {
    /* Start timer 2s early so the aborted message has time to reach
     * the peer before we close the connection.  The "waiting" value
     * sent to the peer still uses the full timeout_ms. */
    eebus_timer_start(c->hello_timer,
                      timeout_ms > 2000 ? timeout_ms - 2000 : timeout_ms,
                      false);
  }

  /* Ask upper layer whether this peer is trusted – §13.4.4.1.2 */
  ship_trust_result_t trust = SHIP_TRUST_PENDING; /* safe default */
  if (c->config.on_trust_check) {
    /* Release mutex during callback to prevent potential deadlock */
    eebus_mutex_unlock(c->mutex);
    trust = c->config.on_trust_check(c, c->config.tls_peer_ski,
                                     c->config.userdata);
    eebus_mutex_lock(c->mutex);

    /*
     * Guard: another thread (e.g. ship_conn_reject / ship_conn_set_ready)
     * may have advanced the state machine while the mutex was dropped.
     * If we are no longer in HELLO_INIT, the transition has already been
     * handled – bail out without issuing a conflicting state change.
     */
    if (c->state != SHIP_STATE_HELLO_INIT) {
      EEBUS_LOG_DEBUG(LOG_TAG,
                      "%s enter_hello: state changed during trust callback "
                      "(now state=%d) – skipping redundant transition",
                      c->session_id, (int)c->state);
      return EEBUS_OK;
    }
  }

  switch (trust) {

    case SHIP_TRUST_READY:
      /* Peer already trusted – enter basic state READY (SME_HELLO_STATE_READY_INIT).
       *
       * SHIP §13.4.4.1 READY_INIT: the timer is started before the update message
       * is sent, so Wait-For-Ready-Timer is active at this point and "waiting" MUST
       * be included (§1621-1622: set "waiting" whenever the timer is active).
       *
       * Use timeout_ms (T_hello_init) directly for the initial "waiting" value
       * instead of hello_remaining_ms() which would return e.g. 59990 due to
       * the few ms elapsed since timer start.
       *
       * The timer must NOT be stopped here – it continues running through
       * READY_LISTEN until the peer's "ready" arrives (HELLO_OK) or it expires
       * (READY_TIMEOUT → abort).  Stopping it early would cause us to send ready
       * without "waiting", which a PENDING peer MUST abort per
       * §13.4.4.1 PENDING_LISTEN rule 1. */
      EEBUS_LOG_INFO(LOG_TAG, "%s Hello: peer trusted, entering READY",
                     c->session_id);
      {
        ship_msg_t ready_msg = {
          .kind  = SHIP_MSG_KIND_HELLO,
          .hello = {
            .phase           = SHIP_HELLO_PHASE_READY,
            .waiting_present = true,
            .waiting         = timeout_ms,
          },
        };
        rc = send_raw(c, SHIP_MSG_TYPE_CONTROL, &ready_msg);
        if (rc == EEBUS_OK) enter_state(c, SHIP_STATE_HELLO_READY);
        return rc;
      }

    case SHIP_TRUST_PENDING:
      /* Not yet trusted – enter basic state PENDING */
      EEBUS_LOG_INFO(LOG_TAG, "%s Hello: peer not yet trusted, entering PENDING",
                     c->session_id);
      {
        ship_msg_t pending_msg = {
          .kind  = SHIP_MSG_KIND_HELLO,
          .hello = {
            .phase           = SHIP_HELLO_PHASE_PENDING,
            .waiting_present = true,
            .waiting         = timeout_ms,
          },
        };
        rc = send_raw(c, SHIP_MSG_TYPE_CONTROL, &pending_msg);
        if (rc == EEBUS_OK) enter_state(c, SHIP_STATE_HELLO_PENDING);
        return rc;
      }

    case SHIP_TRUST_REJECT:
    default:
      EEBUS_LOG_WARN(LOG_TAG, "%s Hello: peer SKI rejected, aborting",
                     c->session_id);
      hello_abort(c, "SKI not trusted");
      return EEBUS_ERR_SHIP_REJECTED;
  }
}

static eebus_err_t handle_hello(ship_conn_t* c, const ship_hello_t* h) {
  /* SHIP §13.4.4.1: state machine for received Hello messages */

  /*
   * §13.4.4.1 PENDING_TIMEOUT rule 3: stop Prolongation-Request-Reply-Timer
   * on ANY received Hello message (the peer has replied).
   */
  eebus_timer_stop(c->prolong_reply_timer);

  if (h->phase == SHIP_HELLO_PHASE_ABORTED) {
    EEBUS_LOG_INFO(LOG_TAG, "%s Hello: peer aborted", c->session_id);
    eebus_timer_stop(c->hello_timer);
    /*
     * §13.4.4.1 Common abort: send aborted back before closing.
     * The peer is likely already closing, but the spec requires both
     * sides to send aborted.
     */
    ship_msg_t abort_reply = {
      .kind  = SHIP_MSG_KIND_HELLO,
      .hello = { .phase = SHIP_HELLO_PHASE_ABORTED },
    };
    send_raw(c, SHIP_MSG_TYPE_CONTROL, &abort_reply);
    /*
     * Use close_on_protocol_error() which:
     *   - enters SHIP_STATE_ERROR (not HELLO_ABORT, which would confuse the
     *     bottom-of-receive fallback into firing on_error a second time)
     *   - closes the WebSocket via on_close_wsi
     *   - fires on_error
     * We deliberately do NOT fire on_closed here: on_closed is reserved for
     * graceful connectionClose handshakes (§13.4.7).
     * Return EEBUS_OK so the bottom-of-receive fallback (err != EEBUS_OK)
     * does not fire on_error a second time.
     */
    close_on_protocol_error(c, EEBUS_ERR_SHIP_REJECTED);
    return EEBUS_OK;
  }

  if (h->phase == SHIP_HELLO_PHASE_READY) {
    if (c->state == SHIP_STATE_HELLO_PENDING) {
      /*
       * §13.4.4.1 PENDING_LISTEN rule 2: peer sent ready while we are still
       * PENDING.
       *
       * Sub-case: peer sent ready WITH no waiting → §13.4.4.1 says abort.
       * Sub-case: peer sent ready WITH waiting    → deactivate Wait-For-Ready-Timer
       *           and (re-)arm Send-Prolongation-Request-Timer based on waiting.
       */
      if (!h->waiting_present) {
        /* PENDING_LISTEN rule 2a: ready without waiting → abort */
        EEBUS_LOG_WARN(LOG_TAG,
                       "%s Hello: peer sent READY without waiting while we are "
                       "PENDING – aborting (§13.4.4.1)",
                       c->session_id);
        hello_abort(c, "READY without waiting in PENDING state");
        return EEBUS_OK;
      }

      /* Deactivate Wait-For-Ready-Timer; arm Send-Prolongation-Request-Timer */
      eebus_timer_stop(c->hello_timer);
      c->last_peer_waiting_ms = h->waiting;
      prolong_timer_rearm(c, h->waiting);

      EEBUS_LOG_INFO(LOG_TAG,
                     "%s Hello: peer ready (waiting=%u ms) while we are PENDING – "
                     "will advance to HELLO_OK once we become READY",
                     c->session_id, h->waiting);
      c->hello_peer_ready = true;
      return EEBUS_OK;
    }

    /*
     * We are in HELLO_READY (we already sent ready) → both sides ready →
     * HELLO_OK: stop timer, stay in HELLO_READY.  Caller advances to
     * Protocol Handshake.
     */
    EEBUS_LOG_INFO(LOG_TAG, "%s Hello: both sides READY – HELLO_OK",
                   c->session_id);
    eebus_timer_stop(c->hello_timer);
    c->hello_peer_ready = true;
    return EEBUS_OK;
  }

  /* phase == PENDING */
  if (h->prolongation_request) {
    /*
     * §13.4.4.1: "An SME User SHALL accept at least two prolongation
     * requests." → always restart the timer.
     */
    EEBUS_LOG_DEBUG(LOG_TAG, "%s Hello: prolongation requested – restarting timer",
                    c->session_id);
    eebus_timer_restart(c->hello_timer);

    /* Send hello update message with our current state and remaining time */
    ship_msg_t upd = {
      .kind  = SHIP_MSG_KIND_HELLO,
      .hello = {
        .phase           = (c->state == SHIP_STATE_HELLO_READY)
                             ? SHIP_HELLO_PHASE_READY
                             : SHIP_HELLO_PHASE_PENDING,
        .waiting_present = true,
        .waiting         = eebus_timer_last_ms(c->hello_timer) + 2000,
      },
    };
    send_raw(c, SHIP_MSG_TYPE_CONTROL, &upd);
    return EEBUS_OK;
  }

  /* phase == PENDING, no prolongationRequest: check waiting sub-element */
  if (h->waiting_present && c->state == SHIP_STATE_HELLO_PENDING) {
    /*
     * §13.4.4.1 PENDING_LISTEN rule 3 (and READY_LISTEN rule 3):
     * Peer sent pending+waiting → (re-)arm our Send-Prolongation-Request-Timer.
     */
    c->last_peer_waiting_ms = h->waiting;
    prolong_timer_rearm(c, h->waiting);
  }

  return EEBUS_OK;
}


/* ── Protocol handshake (SHIP §13.4.4.2) ────────────────────────────────── */

/* Protocol-handshake Wait-Timer duration – SHIP §13.4.4.2.3: 10 s */
#define PROT_HS_WAIT_TIMER_MS (10000U)

static eebus_err_t enter_protocol_handshake(ship_conn_t* c) {
  enter_state(c, SHIP_STATE_PROT_HS_INIT);

  /*
   * SHIP §13.4.4.2.3: both CLIENT and SERVER start the Wait-Timer (10 s)
   * immediately upon entering the handshake phase.
   *   - SERVER: waiting for CLIENT announceMax  (SME_PROT_H_STATE_SERVER_INIT)
   *   - CLIENT: waiting for SERVER select       (SME_PROT_H_STATE_CLIENT_INIT)
   */
  if (!c->prot_hs_timer) {
    eebus_timer_create(&c->prot_hs_timer, on_prot_hs_timeout, c);
  }
  if (c->prot_hs_timer) {
    eebus_timer_start(c->prot_hs_timer, PROT_HS_WAIT_TIMER_MS, false);
  } else {
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s Failed to create protocol handshake Wait-Timer",
                   c->session_id);
  }

  /*
   * SHIP §13.4.4.2.3 Protocol Handshake Process:
   *   SME_PROT_H_STATE_CLIENT_INIT: CLIENT sends announceMax, then waits.
   *   SME_PROT_H_STATE_SERVER_INIT: SERVER starts timer, waits for client's
   *                                  announceMax.
   */
  if (c->config.role == SHIP_ROLE_CLIENT) {
    /* CLIENT sends announceMax with its maximum supported version */
    ship_msg_t hs_msg = {
      .kind      = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE,
      .handshake = {
        .handshake_type = SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX,
        .version        = { .major = SHIP_MAX_MAJOR_VERSION,
                            .minor = SHIP_MAX_MINOR_VERSION },
        .formats        = { SHIP_FORMAT_JSON_UTF8 },
        .formats_count  = 1,
      },
    };
    enter_state(c, SHIP_STATE_PROT_HS_ANNOUNCE);   /* waiting for server SELECT */
    return send_raw(c, SHIP_MSG_TYPE_CONTROL, &hs_msg);
  } else {
    /* SERVER waits for client's announceMax */
    enter_state(c, SHIP_STATE_PROT_HS_LISTEN);
    return EEBUS_OK;
  }
}

static eebus_err_t handle_protocol_handshake(ship_conn_t* c, const ship_protocol_handshake_t* h) {
  /*
   * SHIP §13.4.4.2.3 – three-message protocol handshake:
   *
   *  Step 1 (CLIENT → SERVER): CLIENT sends announceMax.
   *          SERVER receives it here in state PROT_HS_LISTEN.
   *          → SERVER sends select, enters PROT_HS_SELECT (awaiting confirm).
   *
   *  Step 2 (SERVER → CLIENT): SERVER sends select.
   *          CLIENT receives it here in state PROT_HS_ANNOUNCE.
   *          → CLIENT validates, echoes select back, calls enter_pin().
   *
   *  Step 3 (CLIENT → SERVER): CLIENT sends echo-select as confirm.
   *          SERVER receives it here in state PROT_HS_SELECT.
   *          → SERVER validates identical to what it sent, calls enter_pin().
   */

  /* ── Step 1: SERVER receives CLIENT's announceMax ── */
  if (c->config.role == SHIP_ROLE_SERVER
      && c->state == SHIP_STATE_PROT_HS_LISTEN
      && h->handshake_type == SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX) {

    EEBUS_LOG_DEBUG(LOG_TAG, "ProtHS SERVER: received client announceMax v%u.%u",
                    h->version.major, h->version.minor);

    /* Validate: client's max major must be compatible with ours */
    if (h->version.major > SHIP_MAX_MAJOR_VERSION) {
      EEBUS_LOG_ERROR(LOG_TAG, "ProtHS SERVER: client major version %u incompatible",
                      h->version.major);
      ship_msg_t err_msg = {
        .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
        .handshake_err = { .error = SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH },
      };
      send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
      close_on_protocol_error(c, EEBUS_ERR_SHIP_VERSION);
      return EEBUS_OK;
    }

    /* Validate: client must offer JSON-UTF8 */
    bool format_ok = false;
    for (uint8_t fi = 0; fi < h->formats_count; fi++) {
      if (h->formats[fi] == SHIP_FORMAT_JSON_UTF8) { format_ok = true; break; }
    }
    if (!format_ok) {
      EEBUS_LOG_ERROR(LOG_TAG, "ProtHS SERVER: client does not offer JSON-UTF8");
      ship_msg_t err_msg = {
        .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
        .handshake_err = { .error = SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH },
      };
      send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
      close_on_protocol_error(c, EEBUS_ERR_SHIP_VERSION);
      return EEBUS_OK;
    }

    /* Send select: maximum version both support, JSON-UTF8 format */
    ship_msg_t sel_msg = {
      .kind      = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE,
      .handshake = {
        .handshake_type = SHIP_HANDSHAKE_TYPE_SELECT,
        .version        = { .major = SHIP_MAX_MAJOR_VERSION,
                            .minor = SHIP_MAX_MINOR_VERSION },
        .formats        = { SHIP_FORMAT_JSON_UTF8 },
        .formats_count  = 1,
      },
    };
    enter_state(c, SHIP_STATE_PROT_HS_SELECT);   /* waiting for client confirm */

    /* §13.4.4.2.3: re-initialise Wait-Timer for the confirm step */
    if (c->prot_hs_timer) {
      eebus_timer_start(c->prot_hs_timer, PROT_HS_WAIT_TIMER_MS, false);
    }

    return send_raw(c, SHIP_MSG_TYPE_CONTROL, &sel_msg);
  }

  /* ── Step 2: CLIENT receives SERVER's select ── */
  if (c->config.role == SHIP_ROLE_CLIENT
      && c->state == SHIP_STATE_PROT_HS_ANNOUNCE
      && h->handshake_type == SHIP_HANDSHAKE_TYPE_SELECT) {

    EEBUS_LOG_DEBUG(LOG_TAG, "ProtHS CLIENT: received server select v%u.%u",
                    h->version.major, h->version.minor);

    /* Validate version */
    if (h->version.major > SHIP_MAX_MAJOR_VERSION) {
      EEBUS_LOG_ERROR(LOG_TAG, "ProtHS CLIENT: server selected incompatible version %u",
                      h->version.major);
      ship_msg_t err_msg = {
        .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
        .handshake_err = { .error = SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH },
      };
      send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
      close_on_protocol_error(c, EEBUS_ERR_SHIP_VERSION);
      return EEBUS_OK;
    }

    /* Validate: exactly one format, must be JSON-UTF8 */
    if (h->formats_count != 1 || h->formats[0] != SHIP_FORMAT_JSON_UTF8) {
      EEBUS_LOG_ERROR(LOG_TAG, "ProtHS CLIENT: server selected unsupported format");
      ship_msg_t err_msg = {
        .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
        .handshake_err = { .error = SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH },
      };
      send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
      close_on_protocol_error(c, EEBUS_ERR_SHIP_VERSION);
      return EEBUS_OK;
    }

    /*
     * Echo the server's select message back verbatim as confirm
     * (§13.4.4.2.3 SME_PROT_H_STATE_CLIENT_LISTEN_CHOICE step e).
     */
    ship_msg_t echo = {
      .kind      = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE,
      .handshake = {
        .handshake_type = SHIP_HANDSHAKE_TYPE_SELECT,
        .version        = { .major = h->version.major, .minor = h->version.minor },
        .formats        = { h->formats[0] },
        .formats_count  = 1,
      },
    };
    eebus_err_t rc = send_raw(c, SHIP_MSG_TYPE_CONTROL, &echo);
    if (rc != EEBUS_OK) return rc;

    EEBUS_LOG_INFO(LOG_TAG, "ProtHS CLIENT: handshake complete (v%u.%u, JSON-UTF8)",
                   h->version.major, h->version.minor);
    return enter_pin(c);
  }

  /* ── Step 3: SERVER receives CLIENT's echo confirm ── */
  if (c->config.role == SHIP_ROLE_SERVER
      && c->state == SHIP_STATE_PROT_HS_SELECT
      && h->handshake_type == SHIP_HANDSHAKE_TYPE_SELECT) {

    EEBUS_LOG_DEBUG(LOG_TAG, "ProtHS SERVER: received client confirm v%u.%u",
                    h->version.major, h->version.minor);

    /*
     * §13.4.4.2.3 SME_PROT_H_STATE_SERVER_LISTEN_CONFIRM:
     * Verify the confirm is identical to the select we sent (v1.0, JSON-UTF8).
     */
    bool version_ok = (h->version.major == SHIP_MAX_MAJOR_VERSION
                       && h->version.minor == SHIP_MAX_MINOR_VERSION);
    bool format_ok  = (h->formats_count == 1
                       && h->formats[0] == SHIP_FORMAT_JSON_UTF8);

    if (!version_ok || !format_ok) {
      EEBUS_LOG_ERROR(LOG_TAG,
          "ProtHS SERVER: confirm mismatch – version=%u.%u ok=%d, format ok=%d",
          h->version.major, h->version.minor, version_ok, format_ok);
      ship_msg_t err_msg = {
        .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
        .handshake_err = { .error = SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH },
      };
      send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
      close_on_protocol_error(c, EEBUS_ERR_SHIP_VERSION);
      return EEBUS_OK;
    }

    EEBUS_LOG_INFO(LOG_TAG, "ProtHS SERVER: handshake complete (v%u.%u, JSON-UTF8)",
                   h->version.major, h->version.minor);
    return enter_pin(c);
  }

  /* Unexpected message for current state/role */
  EEBUS_LOG_ERROR(LOG_TAG,
      "ProtHS: unexpected message handshakeType=%d in state %s (role=%s)",
      h->handshake_type, state_name(c->state),
      c->config.role == SHIP_ROLE_CLIENT ? "CLIENT" : "SERVER");
  eebus_timer_stop(c->prot_hs_timer);
  {
    ship_msg_t err_msg = {
      .kind          = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR,
      .handshake_err = { .error = SHIP_HANDSHAKE_ERR_UNEXPECTED_MESSAGE },
    };
    send_raw(c, SHIP_MSG_TYPE_CONTROL, &err_msg);
  }
  close_on_protocol_error(c, EEBUS_ERR_SHIP_HANDSHAKE);
  return EEBUS_OK;
}

/* ── PIN phase (SHIP §13.4.5) ────────────────────────────────────────────── */

static eebus_err_t enter_pin(ship_conn_t* c) {
  /* Handshake complete – stop the Wait-Timer (§13.4.4.2.3) */
  eebus_timer_stop(c->prot_hs_timer);

  /* SHIP §13.4.5: we only support "none" PIN state.
   * Send pinState = "none" and wait for the peer's pinState before advancing. */
  enter_state(c, SHIP_STATE_PIN_INIT);

  ship_msg_t pin_msg = {
    .kind      = SHIP_MSG_KIND_PIN_STATE,
    .pin_state = { .pin_state = SHIP_PIN_STATE_NONE },
  };
  eebus_err_t rc = send_raw(c, SHIP_MSG_TYPE_CONTROL, &pin_msg);
  if (rc != EEBUS_OK) return rc;

  enter_state(c, SHIP_STATE_PIN_CHECK);

  /*
   * SHIP §13.4.4.3.5.2 SME_PIN_STATE_ASK_INIT:
   * "The SME User SHALL close the connection if this state lasts more than
   * 10 seconds."  Start a watchdog; stopped when peer's pinState arrives.
   */
  if (!c->pin_ask_init_timer) {
    eebus_timer_create(&c->pin_ask_init_timer, on_pin_ask_init_timeout, c);
  }
  if (c->pin_ask_init_timer) {
    eebus_timer_start(c->pin_ask_init_timer, 10000U, false);
  }

  return EEBUS_OK;
}

static eebus_err_t handle_pin_state(ship_conn_t* c, const ship_pin_state_msg_t* ps) {
  /*
   * First pinState received from peer – stop the ASK_INIT 10-second watchdog
   * (§13.4.4.3.5.2: the timer guards against the peer never sending pinState).
   */
  eebus_timer_stop(c->pin_ask_init_timer);

  /*
   * SHIP §13.4.4.3.5.1: inputPermission field dependency rules.
   *
   * "If the value of pinState is pinOk or none, the sub-element inputPermission
   *  MUST NOT be present."
   * "If the value of pinState is required or optional, the sub-element
   *  inputPermission MUST be present."
   *
   * Violating these rules means the peer is non-conformant → close connection.
   */
  bool needs_perm = (ps->pin_state == SHIP_PIN_STATE_REQUIRED ||
                     ps->pin_state == SHIP_PIN_STATE_OPTIONAL);

  if (needs_perm && !ps->input_permission_present) {
    EEBUS_LOG_ERROR(LOG_TAG,
                    "%s PIN: peer pinState=%d requires inputPermission but field "
                    "is absent – closing (§13.4.4.3.5.1)",
                    c->session_id, (int)ps->pin_state);
    close_on_protocol_error(c, EEBUS_ERR_SHIP_HANDSHAKE);
    return EEBUS_OK;
  }

  if (!needs_perm && ps->input_permission_present) {
    EEBUS_LOG_ERROR(LOG_TAG,
                    "%s PIN: peer pinState=%d MUST NOT include inputPermission "
                    "but field is present – closing (§13.4.4.3.5.1)",
                    c->session_id, (int)ps->pin_state);
    close_on_protocol_error(c, EEBUS_ERR_SHIP_HANDSHAKE);
    return EEBUS_OK;
  }

  /*
   * SHIP §13.4.4.3 "Common procedure to Enable the State Connection data exchange":
   * Data exchange is allowed when peer's pinState is NOT "required".
   *
   * Mapping:
   *   none     → proceed unconditionally (no PIN involved)
   *   optional → proceed with limited exchange (we have no PIN to send);
   *              §13.4.4.3.5.2 ASK_PROCESS rule 8b.i → ASK_RESTRICTED_OK
   *   pinOk    → proceed unconditionally (peer already verified its PIN)
   *   required → peer needs us to send its PIN; we have none → close
   */
  switch (ps->pin_state) {
    case SHIP_PIN_STATE_NONE:
    case SHIP_PIN_STATE_PIN_OK:
      EEBUS_LOG_DEBUG(LOG_TAG, "%s PIN: peer pinState=%d – proceeding to data exchange",
                      c->session_id, (int)ps->pin_state);
      return EEBUS_OK;

    case SHIP_PIN_STATE_OPTIONAL:
      /*
       * Peer has an optional PIN but does not require it from us.
       * We enter ASK_RESTRICTED_OK (§13.4.4.3.5.2): proceed without PIN,
       * accepting restricted data exchange.  The peer will continue listening
       * for a late PIN, but we will never send one.
       */
      EEBUS_LOG_INFO(LOG_TAG,
                     "%s PIN: peer pinState=optional – entering ASK_RESTRICTED_OK, "
                     "proceeding without PIN (restricted data exchange accepted, "
                     "§13.4.4.3.5.2 rule 8b.i)",
                     c->session_id);
      return EEBUS_OK;

    case SHIP_PIN_STATE_REQUIRED:
    default:
      /*
       * Peer requires us to send its PIN, which we do not have.
       * §13.4.4.3.5.2 ASK_PROCESS rule 8a.ii → close the connection.
       */
      EEBUS_LOG_WARN(LOG_TAG,
                     "%s PIN: peer requires PIN but we have none – closing "
                     "(§13.4.4.3.5.2 rule 8a.ii)",
                     c->session_id);
      close_on_protocol_error(c, EEBUS_ERR_UNSUPPORTED);
      return EEBUS_OK;
  }
}

/* ── Access Methods phase (SHIP §13.4.6) ────────────────────────────────── */

/*
 * enter_data_exchange – called when PIN phase is complete.
 *
 * SHIP §13.4.6.2: "This state MUST NOT be entered before connection data
 * exchange is entered."  Access Methods Identification therefore runs *in
 * parallel* to data exchange, not as a prerequisite for it.
 *
 * Flow:
 *   PIN done → enter DATA_EXCHANGE → fire on_handshake_complete
 *            → (server role only, optional) send accessMethodsRequest
 *              and await accessMethods response in-band.
 */
static eebus_err_t enter_data_exchange(ship_conn_t* c) {
  enter_state(c, SHIP_STATE_DATA_EXCHANGE);
  EEBUS_LOG_INFO(LOG_TAG, "%s SHIP handshake complete (tls_ski=%s)",
                 c->session_id, c->peer_ski);

  /*
   * §13.4.6.2.1: Send accessMethodsRequest so the peer can reply with its
   * access methods information.  If sending fails, the transport is broken —
   * return the error so the caller's fallback closes the connection via
   * close_on_protocol_error().
   */
  {
    ship_msg_t req_msg = { .kind = SHIP_MSG_KIND_ACCESS_METHODS_REQUEST };
    eebus_err_t rc = send_raw(c, SHIP_MSG_TYPE_CONTROL, &req_msg);
    if (rc != EEBUS_OK) {
      EEBUS_LOG_ERROR(LOG_TAG,
                      "%s Failed to send accessMethodsRequest (§13.4.6.2): %d"
                      " — closing connection",
                      c->session_id, rc);
      return rc;
    }
    /*
     * §13.4.6.2.1: The sender CAN close the connection if it does not receive
     * a proper accessMethods message within 60 seconds.
     * Start a watchdog; stopped in handle_access_methods().
     */
    if (!c->access_methods_request_timer) {
      eebus_timer_create(&c->access_methods_request_timer,
                         on_access_methods_request_timeout, c);
    }
    if (c->access_methods_request_timer) {
      eebus_timer_start(c->access_methods_request_timer, 60000U, false);
    }
  }

  return EEBUS_OK;
}

/**
 * handle_access_methods_request – SHIP §13.4.6.2
 *
 * Peer requests our access methods.  Build and send an accessMethods response
 * per §13.4.6 Table 24 rules:
 *   - id SHALL be set to our unique ID (mDNS instance name), or empty string.
 *   - dnsSd_mDns MUST NOT be set when id is empty.
 *
 * Extracted from ship_conn_receive() for readability.
 */
static eebus_err_t handle_access_methods_request(ship_conn_t* c) {
  /*
   * §13.4.6 Table 24 rules for accessMethods.id:
   *   - "SHALL be set to the unique ID if the originator has one"
   *   - "SHALL be set to an empty string if no unique ID"
   *   - If dnsSd_mDns is present, id MUST be non-empty (peer must be able
   *     to find us via mDNS using this id).
   *
   * Priority order for our_id:
   *   1. ship_id  — the application-supplied SHIP device id (§7.3.2 "id"),
   *                 stable across cert rotation and network changes.
   *   2. instance_name — fallback for backward compatibility.
   *   3. local_ski     — last resort, non-standard (emits warning).
   *   4. ""            — no id available; omit dnsSd_mDns.
   */
  const char* our_id;
  if (c->config.ship_id[0] != '\0') {
    our_id = c->config.ship_id;
  } else if (c->config.instance_name[0] != '\0') {
    our_id = c->config.instance_name;
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s Access Methods: ship_id not set, "
                   "falling back to instance_name as id",
                   c->session_id);
  } else if (c->config.local_ski[0] != '\0') {
    our_id = c->config.local_ski;
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s Access Methods: ship_id and instance_name not set, "
                   "falling back to SKI as id (configure ship_id "
                   "in ship_node_config_t for spec compliance)",
                   c->session_id);
  } else {
    our_id = "";
  }
  bool advertise_mdns = (our_id[0] != '\0');  /* MUST NOT set if id="" */

  if (!advertise_mdns) {
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s Access Methods reply: id empty, omitting dnsSd_mDns "
                   "(§13.4.6 Table 24)",
                   c->session_id);
  }

  ship_msg_t reply = {
    .kind           = SHIP_MSG_KIND_ACCESS_METHODS,
    .access_methods = {
      .id          = our_id,
      .dns_sd_mdns = advertise_mdns,
    },
  };
  return send_raw(c, SHIP_MSG_TYPE_CONTROL, &reply);
}

static eebus_err_t handle_access_methods(ship_conn_t* c, const ship_access_methods_t* am) {
  /*
   * SHIP §13.4.6 – Access Methods Identification (reverse reconnection info).
   * Stop the 60-second response watchdog (§13.4.6.2.1).
   */
  eebus_timer_stop(c->access_methods_request_timer);

  /*
   * accessMethods.id is the peer's self-reported "unique ID" (§13.4.6 Table 24).
   * Its sole purpose is to let us later initiate a reverse connection back to
   * the peer (§13.4.6.2.1: "store the received information persistently for
   * cases where it needs to initiate a connection to the originator of the
   * message").
   *
   * IMPORTANT: accessMethods.id is NOT an authentication token.  The peer
   * identity for all security decisions is c->peer_ski, which is derived from
   * the TLS peer certificate during the mTLS handshake and is cryptographically
   * bound to the certificate.  accessMethods.id is purely informational
   * application-layer data that cannot be verified and MUST NOT overwrite
   * c->peer_ski.
   *
   * The spec explicitly permits an empty string for accessMethods.id when the
   * peer has no unique ID (§13.4.6 Table 24).
   */

  /* Store peer_access_id (for reverse reconnect) – separate from peer_ski */
  memset(c->peer_access_id, 0, sizeof(c->peer_access_id));
  if (am->id) {
    snprintf(c->peer_access_id, sizeof(c->peer_access_id), "%s", am->id);
  }

  /* Store access-method flags */
  c->peer_dns_sd_mdns = am->dns_sd_mdns;

  if (am->dns_present && am->dns.uri) {
    EEBUS_FREE(c->peer_dns_uri);
    c->peer_dns_uri = eebus_strdup(am->dns.uri);
  }

  EEBUS_LOG_INFO(LOG_TAG,
                 "%s access methods stored: access_id=\"%s\" dns_sd_mdns=%d uri=%s  "
                 "(authoritative peer tls_ski=%s)",
                 c->session_id,
                 c->peer_access_id,
                 c->peer_dns_sd_mdns,
                 (am->dns_present && am->dns.uri) ? am->dns.uri : "-",
                 c->peer_ski);

  /*
   * Access Methods exchange completes the handshake (§13.4.6.2).
   * The state machine is already in DATA_EXCHANGE (entered immediately after
   * PIN phase per §13.4.6.2.1).  Fire on_handshake_complete now that we have
   * the peer's access methods information, including peer_access_id.
   */
  if (c->state == SHIP_STATE_DATA_EXCHANGE) {
    EEBUS_LOG_INFO(LOG_TAG,
                   "%s SHIP handshake complete (peer_access_id=\"%s\" tls_ski=%s)",
                   c->session_id, c->peer_access_id, c->peer_ski);
    if (c->config.callbacks.on_handshake_complete) {
      c->config.callbacks.on_handshake_complete(c, c->peer_ski,
                                                c->config.userdata);
    }
  }

  return EEBUS_OK;
}

/**
 * PIN ASK_INIT timeout callback – SHIP §13.4.4.3.5.2.
 * Fires if we do not receive the peer's first pinState message within 10 s.
 */
static void on_pin_ask_init_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  /* Only relevant while we are waiting for the first peer pinState */
  if (c->state != SHIP_STATE_PIN_CHECK) {
    eebus_mutex_unlock(c->mutex);
    return;
  }

  EEBUS_LOG_ERROR(LOG_TAG,
                  "%s PIN ASK_INIT timeout (10 s) – peer did not send pinState, "
                  "closing connection (§13.4.4.3.5.2)",
                  c->session_id);
  close_on_protocol_error(c, EEBUS_ERR_TIMEOUT);

  eebus_mutex_unlock(c->mutex);
}

/**
 * Access Methods request response timeout – SHIP §13.4.6.2.1.
 * Fires 60 s after we sent accessMethodsRequest without receiving accessMethods.
 * The spec says we CAN close; we treat this as a non-fatal soft close to avoid
 * disrupting data exchange that is already running.
 */
static void on_access_methods_request_timeout(void* userdata) {
  ship_conn_t* c = (ship_conn_t*)userdata;
  eebus_mutex_lock(c->mutex);

  EEBUS_LOG_WARN(LOG_TAG,
                 "%s Access Methods response timeout (60 s) – peer did not reply "
                 "to accessMethodsRequest; closing per §13.4.6.2.1",
                 c->session_id);

  /*
   * §13.4.6.2.1: The spec says the sender "CAN close the connection".
   * We initiate a graceful close with reason=unspecific (temporary disconnect)
   * only if we are still in DATA_EXCHANGE (the normal path).
   */
  if (c->state == SHIP_STATE_DATA_EXCHANGE) {
    send_close_announce(c, SHIP_CLOSE_REASON_UNSPECIFIC);
  }

  eebus_mutex_unlock(c->mutex);
}

static eebus_err_t send_close_announce(ship_conn_t* c,
                                        ship_close_reason_t reason) {
  /*
   * SHIP §13.4.7.1.1: reconnection intent is communicated via "reason":
   *   unspecific         → temporary disconnect; reconnect expected.
   *   removedConnection  → peer will be treated as unknown on reconnect.
   * "doReconnect" is NOT a SHIP v1.0.1 wire field (absent from Table 25)
   * and is therefore not included.
   */
  ship_msg_t msg = {
    .kind  = SHIP_MSG_KIND_CLOSE,
    .close = {
      .phase            = SHIP_CLOSE_PHASE_ANNOUNCE,
      /* reason is Optional per Table 25 but SHOULD be set in announce */
      .reason_present   = true,
      .reason           = reason,
      /* SHIP §13.4.7.1.2 rule 1b: maxTime SHALL be set */
      .max_time_present = true,
      .max_time         = CLOSE_MAX_TIME_MS,
    },
  };

  /* Brief transitional state while frame is queued */
  enter_state(c, SHIP_STATE_CLOSE_ANNOUNCE);

  eebus_err_t rc = send_raw(c, SHIP_MSG_TYPE_END, &msg);
  if (rc != EEBUS_OK) return rc;

  /* Waiting for confirm -- SHIP §13.4.7.1.2 rule 2 */
  enter_state(c, SHIP_STATE_CLOSE_WAIT);

  /* Start maxTime watchdog; stopped in confirm branch or simultaneous-close
   * handling.  SHIP §13.4.7.1.2 rule 3.
   * Destroy any pre-existing timer first (guards against double invocation). */
  if (c->close_timer) {
    eebus_timer_stop(c->close_timer);
    eebus_timer_destroy(c->close_timer);
    c->close_timer = NULL;
  }
  if (eebus_timer_create(&c->close_timer, on_close_timeout, c) == EEBUS_OK) {
    eebus_timer_start(c->close_timer, CLOSE_MAX_TIME_MS, false);
  } else {
    EEBUS_LOG_WARN(LOG_TAG, "%s Failed to create close timer – "
                   "will not enforce maxTime", c->session_id);
  }

  return EEBUS_OK;
}

static eebus_err_t handle_close(ship_conn_t* c, const ship_close_t* cl) {
  if (cl->phase == SHIP_CLOSE_PHASE_ANNOUNCE) {
    /*
     * Peer is initiating close – we are DEV-B.
     * SHIP §13.4.7.1.2 DEV-B rule 1: send confirm with ONLY "phase":"confirm"
     * and no other sub-element (reason_present=false, no maxTime).
     *
     * Simultaneous-close: if we also sent an announce (state == CLOSE_WAIT),
     * we are both DEV-A and DEV-B.  Stop our own maxTime timer before sending
     * confirm.  SHIP §13.4.7.1.2 General rules.
     */
    ship_close_reason_t reason = cl->reason_present
                                     ? cl->reason
                                     : SHIP_CLOSE_REASON_UNSPECIFIC;
    /* Record reason so ship_conn_get_pending_close_reason() reflects that a
     * graceful close has been initiated (by the peer).  Used by ship_node
     * to distinguish graceful closes from unexpected transport drops. */
    c->pending_close_reason = reason;

    EEBUS_LOG_INFO(LOG_TAG, "%s Peer closing: reason=%s",
                   c->session_id, ship_close_reason_str(reason));

    if (c->state == SHIP_STATE_CLOSE_WAIT) {
      /* Simultaneous close – stop our own maxTime watchdog */
      EEBUS_LOG_INFO(LOG_TAG, "%s Simultaneous close – stopping own maxTime timer",
                     c->session_id);
      eebus_timer_stop(c->close_timer);
    }

    /* Send confirm: ONLY phase=confirm (§13.4.7 Table 25, DEV-B rule 1) */
    ship_msg_t confirm = {
      .kind  = SHIP_MSG_KIND_CLOSE,
      .close = { .phase = SHIP_CLOSE_PHASE_CONFIRM },
    };
    send_raw(c, SHIP_MSG_TYPE_END, &confirm);
    enter_state(c, SHIP_STATE_CLOSED);

    if (c->config.on_close_wsi) {
      c->config.on_close_wsi(c->wsi, c->config.userdata);
    }
    if (c->config.callbacks.on_closed) {
      c->config.callbacks.on_closed(c, reason, c->config.userdata);
    }

  } else {
    /* phase == CONFIRM */

    /*
     * Guard: only process a confirm if we are actually waiting for one.
     * An unexpected confirm (e.g. peer bug or duplicate) while in
     * DATA_EXCHANGE would otherwise force-close the connection.
     */
    if (c->state != SHIP_STATE_CLOSE_WAIT) {
      EEBUS_LOG_WARN(LOG_TAG,
                     "%s Received close confirm in unexpected state %s – ignoring",
                     c->session_id, state_name(c->state));
      return EEBUS_OK;
    }

    /* SHIP §13.4.7.1.2 rule 3: close immediately when confirm arrives */
    eebus_timer_stop(c->close_timer);
    enter_state(c, SHIP_STATE_CLOSED);
    EEBUS_LOG_INFO(LOG_TAG, "%s Connection closed (confirm received)", c->session_id);

    if (c->config.on_close_wsi) {
      c->config.on_close_wsi(c->wsi, c->config.userdata);
    }
    if (c->config.callbacks.on_closed) {
      /* We initiated the close – use the reason from ship_conn_close(). */
      c->config.callbacks.on_closed(c, c->pending_close_reason,
                                    c->config.userdata);
    }
  }
  return EEBUS_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_conn_create(ship_conn_t** conn, const ship_conn_config_t* config) {
  if (!conn || !config) return EEBUS_ERR_INVALID;

  ship_conn_t* c = EEBUS_NEW(ship_conn_t);
  if (!c) return EEBUS_ERR_NO_MEMORY;

  memcpy(&c->config, config, sizeof(c->config));
  c->state = SHIP_STATE_CMI_INIT;
  atomic_init(&c->refs, 1U);
  atomic_init(&c->closing, false);

  eebus_err_t rc = eebus_mutex_create(&c->mutex);
  if (rc != EEBUS_OK) {
    EEBUS_FREE(c);
    return rc;
  }

  *conn = c;
  return EEBUS_OK;
}

eebus_err_t ship_conn_start(ship_conn_t* conn, void* ws_ctx, void* wsi) {
  if (!conn || !wsi) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(conn->mutex);

  if (conn->state != SHIP_STATE_CMI_INIT) {
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_STATE;
  }

  conn->lws_ctx = (struct lws_context*)ws_ctx;
  conn->wsi     = (struct lws*)wsi;

  /*
   * Authoritative peer identity: copy the TLS-cert-derived SKI into peer_ski
   * immediately.  This is the only source of truth for who the peer is; it is
   * set once here and never overwritten by application-layer data (e.g.
   * accessMethods.id).
   */
  snprintf(conn->peer_ski, sizeof(conn->peer_ski), "%s", conn->config.tls_peer_ski);

  /* Build session_id as a unique connection handle.  Format:
   *   [peerSKI16#xxxxxxxx]
   * where peerSKI16 is the first 16 chars of the TLS-authenticated peer
   * SKI and xxxxxxxx is the lower 32 bits of this ship_conn_t pointer in
   * hex.  Pointer low bits give per-process uniqueness without any global
   * counter or lock.  Used by the upper layer as the connection handle
   * (see ship_node_send_spine).                                           */
  const char* peer = conn->config.tls_peer_ski[0]
                       ? conn->config.tls_peer_ski
                       : "????????????????";
  uint32_t cid = (uint32_t)(uintptr_t)conn;
  snprintf(conn->session_id, sizeof(conn->session_id),
           "[%.16s#%08" PRIx32 "]", peer, cid);

  EEBUS_LOG_INFO(LOG_TAG, "%s session started (role=%s)",
                 conn->session_id,
                 conn->config.role == SHIP_ROLE_CLIENT ? "CLIENT" : "SERVER");

  eebus_err_t rc = handle_cmi_init(conn);

  eebus_mutex_unlock(conn->mutex);
  return rc;
}

eebus_err_t ship_conn_receive(ship_conn_t* conn, const uint8_t* data, size_t len) {
  if (!conn || !data || len == 0) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(conn->mutex);

  ship_msg_type_t type;
  const char*     json_payload;
  size_t          json_len;

  int rc_frame = ship_frame_decode(data, len, &type, &json_payload, &json_len);
  if (rc_frame != 0) {
    EEBUS_LOG_ERROR(LOG_TAG, "Frame decode failed (len=%zu)", len);
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_SHIP_HANDSHAKE;
  }

  /* ── CMI frames (SHIP §13.4.3) ─────────────────────────────────────────── */
  if (conn->state == SHIP_STATE_CMI_CLIENT_WAIT
      || conn->state == SHIP_STATE_CMI_SERVER_WAIT) {

    /* CMI message received – stop the watchdog regardless of validity */
    eebus_timer_stop(conn->cmi_timer);

    eebus_err_t err = handle_cmi_frame(conn, data, len);
    if (err != EEBUS_OK) {
      /*
       * handle_cmi_frame() already queued CMI{0} for SERVER (§2.2.1/§2.2.3)
       * or did nothing for CLIENT (§3.2.1/§3.2.3).  Either way, close the
       * WebSocket and fire on_error now.
       */
      close_on_protocol_error(conn, err);
      eebus_mutex_unlock(conn->mutex);
      return err;
    }

    /* CMI done – enter Hello phase */
    err = enter_hello(conn);
    eebus_mutex_unlock(conn->mutex);
    return err;
  }

  /* ── For all subsequent frames parse the JSON body ──────────────────────── */
  if (!json_payload || json_len == 0) {
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_OK; /* Ignore empty frames */
  }

  /* Make a NUL-terminated copy for parsing (json_payload is not NUL-terminated) */
  char* json_copy = EEBUS_MALLOC(json_len + 1);
  if (!json_copy) {
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_NO_MEMORY;
  }
  memcpy(json_copy, json_payload, json_len);
  json_copy[json_len] = '\0';

  /* ── DATA frames (SPINE payload) ─────────────────────────────────────────── */
  if (type == SHIP_MSG_TYPE_DATA && conn->state == SHIP_STATE_DATA_EXCHANGE) {
    /* Log the full incoming SHIP DATA frame */
    log_json(conn->session_id, "<<", json_copy);

    /* Parse the data message to extract the SPINE payload */
    ship_msg_t msg;
    if (ship_msg_from_json(json_copy, &msg) == 0 && msg.kind == SHIP_MSG_KIND_DATA) {
      if (conn->config.callbacks.on_spine_data && msg.data.payload_json) {
        ship_event_spine_data_t ev = {
          .json = msg.data.payload_json,
          .len  = strlen(msg.data.payload_json),
        };
        conn->config.callbacks.on_spine_data(conn, &ev,
                                             conn->config.userdata);
      }
      ship_msg_free(&msg);
    }
    EEBUS_FREE(json_copy);
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_OK;
  }

  /* Log incoming control/end frames */
  log_json(conn->session_id, "<<", json_copy);

  /* ── CONTROL and END frames ─────────────────────────────────────────────── */
  /* Hello, ProtocolHandshake, and Close messages are always JSON-UTF8
   * (before and outside of format negotiation). Validate encoding first. */
  if (!is_valid_utf8((const uint8_t*)json_copy, json_len)) {
    EEBUS_LOG_ERROR(LOG_TAG, "%s Non-UTF8 control/end payload – closing",
                    conn->session_id);
    EEBUS_FREE(json_copy);
    close_on_protocol_error(conn, EEBUS_ERR_SHIP_HANDSHAKE);
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_SHIP_HANDSHAKE;
  }

  ship_msg_t msg;
  if (ship_msg_from_json(json_copy, &msg) != 0) {
    EEBUS_LOG_ERROR(LOG_TAG, "%s Failed to parse control message – closing",
                    conn->session_id);
    EEBUS_FREE(json_copy);
    close_on_protocol_error(conn, EEBUS_ERR_SHIP_HANDSHAKE);
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_SHIP_HANDSHAKE;
  }
  EEBUS_FREE(json_copy);

  eebus_err_t err = EEBUS_OK;

  /*
   * SHIP §13.4.4.1 READY_LISTEN / PENDING_LISTEN:
   * "If a received message is not an SME 'hello' message while in this state,
   *  the SME User SHALL execute the common 'abort' procedure."
   */
  bool in_hello = (conn->state == SHIP_STATE_HELLO_READY
                || conn->state == SHIP_STATE_HELLO_PENDING);
  if (in_hello && msg.kind != SHIP_MSG_KIND_HELLO) {
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s Non-Hello message (kind=%d) received during Hello phase "
                   "– executing abort procedure (§13.4.4.1)",
                   conn->session_id, (int)msg.kind);
    ship_msg_free(&msg);
    hello_abort(conn, "unexpected message in Hello phase");
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_OK;
  }

  switch (msg.kind) {
    case SHIP_MSG_KIND_HELLO:
      err = handle_hello(conn, &msg.hello);
      /*
       * Advance to Protocol Handshake only when BOTH sides are ready:
       *   - conn->state == SHIP_STATE_HELLO_READY  (we already sent ready)
       *   - conn->hello_peer_ready == true         (peer sent ready)
       *
       * hello_peer_ready is set in two cases:
       *   a) We were PENDING and received peer's ready → set by handle_hello,
       *      actual handshake advance happens later in ship_conn_set_ready().
       *   b) We were READY and received peer's ready → set by handle_hello,
       *      handshake triggered here immediately.
       *
       * Receiving peer's pending (with or without prolongationRequest) leaves
       * hello_peer_ready false, so this condition is not triggered — correct
       * per READY_LISTEN rule 2 (ignore pending when we are already ready).
       */
      if (err == EEBUS_OK
          && conn->state == SHIP_STATE_HELLO_READY
          && conn->hello_peer_ready) {
        err = enter_protocol_handshake(conn);
      }
      break;

    case SHIP_MSG_KIND_PROTOCOL_HANDSHAKE:
      /*
       * handle_protocol_handshake() calls enter_pin() internally at the
       * end of each completed step (CLIENT step 2, SERVER step 3).
       * No additional action needed here.
       */
      err = handle_protocol_handshake(conn, &msg.handshake);
      break;

    case SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR:
      EEBUS_LOG_ERROR(LOG_TAG, "Protocol handshake error from peer: %d",
                      msg.handshake_err.error);
      close_on_protocol_error(conn, EEBUS_ERR_SHIP_HANDSHAKE);
      err = EEBUS_OK;  /* close_on_protocol_error handled it; skip fallback */
      break;

    case SHIP_MSG_KIND_PIN_STATE:
      err = handle_pin_state(conn, &msg.pin_state);
      if (err == EEBUS_OK) {
        /*
         * PIN phase complete → enter DATA_EXCHANGE immediately.
         * Access Methods exchange (§13.4.6) is initiated in parallel
         * from within enter_data_exchange() for the server role.
         */
        err = enter_data_exchange(conn);
      }
      break;

    case SHIP_MSG_KIND_ACCESS_METHODS_REQUEST:
      err = handle_access_methods_request(conn);
      break;

    case SHIP_MSG_KIND_ACCESS_METHODS:
      err = handle_access_methods(conn, &msg.access_methods);
      break;

    case SHIP_MSG_KIND_CLOSE:
      err = handle_close(conn, &msg.close);
      break;

    default:
      EEBUS_LOG_WARN(LOG_TAG, "Unexpected message kind %d in state %d",
                     (int)msg.kind, (int)conn->state);
      break;
  }

  ship_msg_free(&msg);

  if (err != EEBUS_OK && conn->state != SHIP_STATE_ERROR) {
    /*
     * An unexpected error slipped through without being handled by
     * close_on_protocol_error().  Use it now so the WebSocket is also
     * closed, not just the state machine.
     */
    close_on_protocol_error(conn, err);
    err = EEBUS_OK;
  }

  eebus_mutex_unlock(conn->mutex);
  return err;
}

eebus_err_t ship_conn_send_spine(ship_conn_t* conn, const char* spine_json) {
  if (!conn || !spine_json) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(conn->mutex);

  if (conn->state != SHIP_STATE_DATA_EXCHANGE) {
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_STATE;
  }

  ship_msg_t data_msg = {
    .kind = SHIP_MSG_KIND_DATA,
    .data = {
      .payload_json = (char*)spine_json,
    },
  };
  /* Copy protocol ID string; field is NUL-padded by zero-init of data_msg */
  snprintf(data_msg.data.header.protocol_id,
           sizeof(data_msg.data.header.protocol_id), "%s", SHIP_PROTOCOL_ID);

  eebus_err_t rc = send_raw(conn, SHIP_MSG_TYPE_DATA, &data_msg);
  eebus_mutex_unlock(conn->mutex);
  return rc;
}

eebus_err_t ship_conn_close(ship_conn_t* conn,
                             ship_close_reason_t reason) {
  if (!conn) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(conn->mutex);

  /*
   * SHIP §13.4.7.1: "This state MUST NOT be entered before connection data
   * exchange is entered."  Reject the request if the handshake is not yet
   * complete or the connection is already being torn down.
   */
  if (conn->state != SHIP_STATE_DATA_EXCHANGE) {
    EEBUS_LOG_WARN(LOG_TAG,
                   "%s ship_conn_close() called in state %s – only valid in "
                   "DATA_EXCHANGE (SHIP §13.4.7.1); ignoring",
                   conn->session_id, state_name(conn->state));
    eebus_mutex_unlock(conn->mutex);
    return EEBUS_ERR_STATE;
  }

  conn->pending_close_reason = reason;
  eebus_err_t rc = send_close_announce(conn, reason);

  eebus_mutex_unlock(conn->mutex);
  return rc;
}

ship_conn_state_t ship_conn_get_state(const ship_conn_t* conn) {
  if (!conn) return SHIP_STATE_ERROR;
  return conn->state;
}

void ship_conn_set_userdata(ship_conn_t* conn, void* userdata) {
  if (!conn) return;
  conn->config.userdata = userdata;
}

void ship_conn_reject(ship_conn_t* conn) {
  if (!conn) return;

  eebus_mutex_lock(conn->mutex);

  if (conn->state != SHIP_STATE_HELLO_PENDING) {
    eebus_mutex_unlock(conn->mutex);
    return;
  }

  EEBUS_LOG_INFO(LOG_TAG, "%s Hello: PENDING → ABORTED (trust rejected by user)",
                 conn->session_id);
  hello_abort(conn, "rejected by user");

  eebus_mutex_unlock(conn->mutex);
}

void ship_conn_set_ready(ship_conn_t* conn) {
  if (!conn) return;

  eebus_mutex_lock(conn->mutex);

  if (conn->state != SHIP_STATE_HELLO_PENDING) {
    /* No-op: already advanced, closed, or not yet in Hello */
    eebus_mutex_unlock(conn->mutex);
    return;
  }

  EEBUS_LOG_INFO(LOG_TAG, "%s Hello: PENDING → READY (trust confirmed)",
                 conn->session_id);

  /*
   * SHIP §13.4.4.1 Switching PENDING→READY:
   *   a. deactivate Send-Prolongation-Request-Timer and
   *      Prolongation-Request-Reply-Timer  (NOT Wait-For-Ready-Timer)
   *   b. send SME "hello" update message with new state READY
   *   c. enter HELLO_OK if peer already sent ready, else READY_LISTEN
   *
   * Always include "waiting" for maximum interoperability with third-party
   * implementations that may not handle ready-without-waiting gracefully.
   */
  eebus_timer_stop(conn->prolong_timer);        /* §13.4.4.1 step a */
  eebus_timer_stop(conn->prolong_reply_timer);  /* §13.4.4.1 step a */

  ship_msg_t ready_msg;
  memset(&ready_msg, 0, sizeof(ready_msg));
  ready_msg.kind                  = SHIP_MSG_KIND_HELLO;
  ready_msg.hello.phase           = SHIP_HELLO_PHASE_READY;
  ready_msg.hello.waiting_present = true;

  if (conn->hello_peer_ready) {
    /*
     * Peer already sent ready — hello_timer is stopped.
     * Peer is in READY_LISTEN and will enter HELLO_OK regardless of waiting
     * value (READY_LISTEN rule 1).  Use the nominal default as a meaningful
     * value for third-party implementations that inspect waiting even here.
     */
    eebus_timer_stop(conn->hello_timer);
    ready_msg.hello.waiting = HELLO_TIMEOUT_MS_DEFAULT;
  } else {
    /*
     * Peer may still be in PENDING_LISTEN.
     * PENDING_LISTEN rule 1: ready WITHOUT waiting → peer MUST abort.
     * Use the actual timer remaining value so peer can correctly compute
     * its Send-Prolongation-Request-Timer.
     * Do NOT stop hello_timer — keep running until peer sends ready or
     * READY_TIMEOUT fires.
     */
    ready_msg.hello.waiting = hello_remaining_ms(conn);
  }

  send_raw(conn, SHIP_MSG_TYPE_CONTROL, &ready_msg);
  enter_state(conn, SHIP_STATE_HELLO_READY);

  if (conn->hello_peer_ready) {
    EEBUS_LOG_INFO(LOG_TAG, "%s Hello: HELLO_OK (peer was already READY)",
                   conn->session_id);
    enter_protocol_handshake(conn);
  }
  /* else: stay in HELLO_READY_LISTEN, wait for peer's ready message */

  eebus_mutex_unlock(conn->mutex);
}

const char* ship_conn_get_peer_ski(const ship_conn_t* conn) {
  if (!conn) return "";
  return conn->peer_ski;
}

const char* ship_conn_get_peer_access_id(const ship_conn_t* conn) {
  if (!conn) return "";
  return conn->peer_access_id;
}

const char* ship_conn_get_tls_peer_ski(const ship_conn_t* conn) {
  if (!conn) return "";
  return conn->config.tls_peer_ski;
}

const char* ship_conn_get_session_id(const ship_conn_t* conn) {
  if (!conn || conn->session_id[0] == '\0') return "[????????????????#????????]";
  return conn->session_id;
}

ship_close_reason_t ship_conn_get_pending_close_reason(const ship_conn_t* conn) {
  if (!conn) return SHIP_CLOSE_REASON_NONE;
  return conn->pending_close_reason;
}

void ship_conn_destroy(ship_conn_t* conn) {
  if (!conn) return;

  /* The registry releases its owner reference; active operations may remain. */
  if (!atomic_exchange_explicit(&conn->closing, true, memory_order_acq_rel)) {
    ship_conn_release(conn);
  }
}

bool ship_conn_retain(ship_conn_t* conn) {
  if (!conn) return false;

  /* The caller must hold the registry lock while acquiring this reference. */
  if (atomic_load_explicit(&conn->closing, memory_order_acquire)) {
    return false;
  }

  unsigned int refs = atomic_load_explicit(&conn->refs, memory_order_relaxed);
  while (refs != 0U) {
    if (atomic_compare_exchange_weak_explicit(
            &conn->refs, &refs, refs + 1U,
            memory_order_acquire, memory_order_relaxed)) {
      return true;
    }
    if (atomic_load_explicit(&conn->closing, memory_order_acquire)) {
      return false;
    }
  }

  return false;
}

void ship_conn_release(ship_conn_t* conn) {
  if (!conn) return;

  if (atomic_fetch_sub_explicit(&conn->refs, 1U,
                                memory_order_acq_rel) == 1U) {
    ship_conn_free(conn);
  }
}
