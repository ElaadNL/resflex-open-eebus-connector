// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_ws_lws.c
 * @brief libwebsockets backend for SHIP WebSocket communication.
 *
 * SHIP §13.2: binary WebSocket frames, sub-protocol "ship", mandatory mTLS.
 *
 * Design notes
 * ------------
 * - One lws_context per ship_ws_t (server or client).
 * - A single background thread drives lws_service() with a 50 ms timeout.
 * - Per-wsi user data (ship_ws_per_session_t) holds a fixed-depth ring queue
 *   of pending TX frames (TX_QUEUE_DEPTH slots).
 * - The LWS write path: ship_conn calls ship_ws_queue_frame() which enqueues
 *   into the ring and requests a writable callback.  The service thread's
 *   WRITEABLE callback dequeues one frame per invocation and re-requests
 *   WRITEABLE if more frames remain.
 * - An eebus_mutex_t protects the ring between producer threads (timer /
 *   connection state-machine) and the LWS service thread (consumer).
 */

#include "ship_ws.h"

#include <libwebsockets.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../log/eebus_log.h"
#include "../../common/eebus_memory.h"
#include "../../platform/eebus_mutex.h"
#include "../../platform/eebus_thread.h"
#include "../tls/ship_tls.h"

#define LOG_TAG      "ship_ws"
#define LOG_TAG_LWS  "lws"
#define LOG_TAG_TLS  "tls"

/* SHIP does not define a maximum complete WebSocket message size. */
#define SHIP_WS_MAX_MESSAGE_SIZE (64U * 1024U)
#define SHIP_WS_RX_INITIAL_CAPACITY (4096U)

/*
 * SHIP §9.1 requires CBC support and permits the two AEAD suites. Prefer
 * AEAD when both peers support it, while retaining CBC for interoperability.
 */
#define SHIP_TLS12_CIPHER_LIST \
  "ECDHE-ECDSA-AES128-GCM-SHA256:" \
  "ECDHE-ECDSA-AES128-CCM8:" \
  "ECDHE-ECDSA-AES128-SHA256"

/* ── libwebsockets → eebus_log bridge ───────────────────────────────────────
 *
 * lws calls this function for every internal log message.  We map lws log
 * levels to the eebus_log severity levels and re-emit through the unified
 * SDK logging channel so that all TLS/WebSocket diagnostics appear in the
 * same log stream as the SHIP state machine output.
 *
 * Installed once via lws_set_log_level() before the first lws context is
 * created (see ship_ws_init_logging()).
 */
static void lws_log_emit(int lws_level, const char* line) {
  if (!line) return;

  /* Strip the trailing newline that lws appends */
  char buf[512];
  size_t n = strlen(line);
  if (n >= sizeof(buf)) n = sizeof(buf) - 1;
  memcpy(buf, line, n);
  /* Remove trailing whitespace / newline */
  while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
    n--;
  buf[n] = '\0';

  switch (lws_level) {
    case LLL_ERR:    EEBUS_LOG_ERROR(LOG_TAG_LWS, "%s", buf); break;
    case LLL_WARN:   EEBUS_LOG_WARN (LOG_TAG_LWS, "%s", buf); break;
    case LLL_NOTICE: EEBUS_LOG_INFO (LOG_TAG_LWS, "%s", buf); break;
    default:         EEBUS_LOG_DEBUG(LOG_TAG_LWS, "%s", buf); break;
  }
}

/**
 * @brief Initialise lws logging once, bridging it to eebus_log.
 *
 * Enables ERR, WARN, NOTICE (always) and INFO + DEBUG (mapped to our DEBUG
 * level) so that TLS/WebSocket handshake details are visible when the SDK
 * log level is set to DEBUG.  Called before the first lws_create_context().
 */
static void ship_ws_init_logging(void) {
  static int done = 0;
  if (done) return;
  done = 1;

  /*
   * Register only ERR/WARN/NOTICE by default to avoid lws internal state
   * machine noise (SSL handshake transitions, per-frame writes, …) in the
   * production log.  Define EEBUS_DEBUG_LWS at compile time to re-enable
   * the verbose levels for TLS/WebSocket troubleshooting.
   */
  int level = LLL_ERR | LLL_WARN | LLL_NOTICE;
#ifdef EEBUS_DEBUG_LWS
  level |= LLL_INFO | LLL_DEBUG | LLL_CLIENT;
#endif
  lws_set_log_level(level, lws_log_emit);
}

/* ── OpenSSL TLS handshake info callback ─────────────────────────────────────
 *
 * Installed on every SSL_CTX via SSL_CTX_set_info_callback().  Called by
 * OpenSSL at each state transition during the TLS handshake, giving us
 * fine-grained visibility into exactly where a handshake fails.
 *
 * 'where' is a bitmask of SSL_CB_* flags; 'ret' is the return code of the
 * operation that just completed (0 or negative = error).
 */
static void openssl_info_cb(const SSL* ssl, int where, int ret) {
  const char* op;

  if (where & SSL_CB_HANDSHAKE_START) {
    EEBUS_LOG_DEBUG(LOG_TAG_TLS, "TLS handshake started");
    return;
  }
  if (where & SSL_CB_HANDSHAKE_DONE) {
    if (ret == 0) {
      char err_buf[256];
      unsigned long e;
      while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, err_buf, sizeof(err_buf));
        EEBUS_LOG_ERROR(LOG_TAG_TLS, "TLS handshake FAILED: %s", err_buf);
      }
    } else {
      EEBUS_LOG_INFO(LOG_TAG_TLS, "TLS handshake completed OK  cipher=%s",
                     SSL_get_cipher_name(ssl));
    }
    return;
  }

  /*
   * Only trace handshake-phase state transitions (CONNECT/ACCEPT loops and
   * their exits).  SSL_CB_READ and SSL_CB_WRITE fire on every SSL_read() /
   * SSL_write() call during normal data transfer – logging them causes a log
   * flood whenever the connection is busy, and a busy-loop flood when lws
   * repeatedly retries a failing SSL_read on a permanently-readable fd.
   */
  if (where & SSL_CB_CONNECT_LOOP)       op = "connect";
  else if (where & SSL_CB_ACCEPT_LOOP)   op = "accept";
  else if (where & SSL_CB_CONNECT_EXIT)  op = "connect-exit";
  else if (where & SSL_CB_ACCEPT_EXIT)   op = "accept-exit";
  else return; /* SSL_CB_READ, SSL_CB_WRITE, and other data-path flags – skip */

  if (ret <= 0) {
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return;

    char err_buf[256];
    unsigned long e;
    while ((e = ERR_get_error()) != 0) {
      ERR_error_string_n(e, err_buf, sizeof(err_buf));
      EEBUS_LOG_ERROR(LOG_TAG_TLS, "TLS %s error: %s", op, err_buf);
    }
  } else {
    EEBUS_LOG_DEBUG(LOG_TAG_TLS, "TLS %s: %s", op, SSL_state_string_long(ssl));
  }
}

/**
 * @brief Drain and log the entire OpenSSL error queue at ERROR level.
 *
 * Call this whenever a TLS-related failure occurs to surface the exact
 * OpenSSL error string (e.g. "certificate verify failed") in the log.
 */
static void log_openssl_errors(const char* context) {
  char buf[256];
  unsigned long e;
  while ((e = ERR_get_error()) != 0) {
    ERR_error_string_n(e, buf, sizeof(buf));
    EEBUS_LOG_ERROR(LOG_TAG_TLS, "%s: %s", context, buf);
  }
}

/**
 * Depth of the per-wsi TX ring queue.
 *
 * SHIP handshake messages arrive in bursts (e.g. server sends CMI then
 * immediately queues Hello).  DATA_EXCHANGE SPINE frames may also accumulate
 * if the service thread is briefly busy.  64 slots provides ample headroom
 * for any realistic burst while keeping per-connection memory negligible
 * (64 pointers ≈ 512 bytes on a 64-bit system).
 * If the queue overflows a WARNING is logged and the oldest frame is dropped.
 */
#define TX_QUEUE_DEPTH 64

/** Per-connection session data stored by libwebsockets. */
typedef struct {
  /*
   * TX ring queue.
   *
   * Multiple threads may call ship_ws_queue_frame() concurrently (timer
   * callbacks, connection state-machine).  The LWS service thread is the
   * sole consumer (WRITEABLE callback).  An eebus_mutex_t* is used to
   * decouple from the platform threading layer.
   *
   * Invariant: tx_count == number of non-NULL entries in tx_ring.
   *   head = index of the oldest frame (next to send).
   *   tail = index where the next frame will be written.
   *   count == 0 → queue empty; count == TX_QUEUE_DEPTH → queue full.
   */
  eebus_mutex_t* tx_mutex;
  uint8_t* tx_ring[TX_QUEUE_DEPTH]; /**< LWS_PRE + payload buffers          */
  size_t   tx_lens[TX_QUEUE_DEPTH]; /**< Payload lengths (excl. LWS_PRE)    */
  int      tx_head;                 /**< Dequeue index (consumer)            */
  int      tx_tail;                 /**< Enqueue index (producer)            */
  int      tx_count;                /**< Frames currently in queue           */

  /**
   * When true, the WRITEABLE callback returns -1 to close the wsi after
   * the TX queue has been fully drained.  Set by ship_ws_close_wsi().
   */
  bool close_pending;

  /*
   * RX reassembly buffer.
   *
   * A single SHIP message = one WebSocket message, but lws may deliver it
   * across multiple LWS_CALLBACK_RECEIVE calls (WS fragmentation, TCP
   * segmentation, TLS record boundaries).  We accumulate fragments here
   * and deliver to the upper layer only when lws_is_final_fragment() is true.
   */
  uint8_t* rx_buf;
  size_t   rx_len;
  size_t   rx_cap;
} ship_ws_per_session_t;

/* ── TX ring queue helpers (call with tx_mutex held) ────────────────────────
 *
 * session_init()   – called once from LWS_CALLBACK_*_ESTABLISHED
 * tx_enqueue()        – producer: returns false if queue was full (frame dropped)
 * tx_dequeue()        – consumer: returns NULL when queue is empty
 * tx_drain_free()     – discard + free all pending frames (on connection close)
 */

static void session_init(ship_ws_per_session_t* sess) {
  eebus_mutex_create(&sess->tx_mutex);
  memset(sess->tx_ring, 0, sizeof(sess->tx_ring));
  memset(sess->tx_lens, 0, sizeof(sess->tx_lens));
  sess->tx_head  = 0;
  sess->tx_tail  = 0;
  sess->tx_count = 0;
  sess->close_pending = false;
  sess->rx_buf = NULL;
  sess->rx_len = 0;
  sess->rx_cap = 0;
}

/* ── RX reassembly helpers ─────────────────────────────────────────────────
 *
 * rx_append()  – accumulate incoming fragment data
 * rx_reset()   – clear buffer after delivering a complete message (keeps allocation)
 * rx_free()    – release buffer memory on connection close
 */

static eebus_err_t rx_append(ship_ws_per_session_t* s,
                             const uint8_t*         data,
                             size_t                  len) {
  if (!s || (len > 0U && !data)) return EEBUS_ERR_INVALID;

  if (s->rx_len > SHIP_WS_MAX_MESSAGE_SIZE ||
      len > SHIP_WS_MAX_MESSAGE_SIZE - s->rx_len) {
    return EEBUS_ERR_OVERFLOW;
  }

  const size_t required = s->rx_len + len;
  if (required > s->rx_cap) {
    size_t new_cap = s->rx_cap > 0U
                         ? s->rx_cap
                         : SHIP_WS_RX_INITIAL_CAPACITY;
    if (new_cap > SHIP_WS_MAX_MESSAGE_SIZE) {
      new_cap = SHIP_WS_MAX_MESSAGE_SIZE;
    }

    while (new_cap < required) {
      if (new_cap > SHIP_WS_MAX_MESSAGE_SIZE / 2U) {
        new_cap = SHIP_WS_MAX_MESSAGE_SIZE;
        break;
      }
      new_cap *= 2U;
    }

    if (new_cap < required) return EEBUS_ERR_OVERFLOW;

    uint8_t* new_buf = EEBUS_REALLOC(s->rx_buf, new_cap);
    if (!new_buf) return EEBUS_ERR_NO_MEMORY;

    s->rx_buf = new_buf;
    s->rx_cap = new_cap;
  }

  if (len > 0U) {
    memcpy(s->rx_buf + s->rx_len, data, len);
  }
  s->rx_len = required;
  return EEBUS_OK;
}

static void rx_reset(ship_ws_per_session_t* s) {
  s->rx_len = 0;
  /* keep rx_buf allocated for reuse */
}

static void rx_free(ship_ws_per_session_t* s) {
  EEBUS_FREE(s->rx_buf);
  s->rx_buf = NULL;
  s->rx_len = 0;
  s->rx_cap = 0;
}

/** Enqueue a frame.  Returns true on success, false if queue is full.
 *  On overflow the OLDEST frame is dropped to preserve the most recent
 *  state (more useful for debugging than silently dropping the new frame). */
static bool tx_enqueue(ship_ws_per_session_t* sess,
                       uint8_t* lws_buf, size_t len) {
  if (sess->tx_count == TX_QUEUE_DEPTH) {
    /* Queue full – drop oldest frame */
    EEBUS_FREE(sess->tx_ring[sess->tx_head]);
    sess->tx_ring[sess->tx_head] = NULL;
    sess->tx_head = (sess->tx_head + 1) % TX_QUEUE_DEPTH;
    sess->tx_count--;
  }
  sess->tx_ring[sess->tx_tail] = lws_buf;
  sess->tx_lens[sess->tx_tail] = len;
  sess->tx_tail  = (sess->tx_tail + 1) % TX_QUEUE_DEPTH;
  sess->tx_count++;
  return true;
}

/** Dequeue the next frame.  Sets *len_out; returns NULL when empty. */
static uint8_t* tx_dequeue(ship_ws_per_session_t* sess, size_t* len_out) {
  if (sess->tx_count == 0) return NULL;
  uint8_t* buf = sess->tx_ring[sess->tx_head];
  *len_out     = sess->tx_lens[sess->tx_head];
  sess->tx_ring[sess->tx_head] = NULL;
  sess->tx_head  = (sess->tx_head + 1) % TX_QUEUE_DEPTH;
  sess->tx_count--;
  return buf;
}

/** Free all remaining frames in the queue (used on connection teardown). */
static void tx_drain_free(ship_ws_per_session_t* sess) {
  while (sess->tx_count > 0) {
    size_t dummy;
    uint8_t* buf = tx_dequeue(sess, &dummy);
    EEBUS_FREE(buf);
  }
}

/** Mode of a ship_ws instance. */
typedef enum {
  SHIP_WS_MODE_SERVER,
  SHIP_WS_MODE_CLIENT,
} ship_ws_mode_t;

struct ship_ws {
  ship_ws_config_t  config;       /**< Copied from the caller                */
  ship_ws_mode_t    mode;
  struct lws_context* lws_ctx;
  eebus_thread_t*   thread;
  bool              running;
  bool              stop_requested;

  /*
   * When true the service thread destroys the lws_context and frees this
   * struct after the service loop exits.  Set for CLIENT connections because
   * ship_node_connect_peer() does not retain the ship_ws_t* pointer; without
   * self-destruction the thread and lws_context would leak after the
   * connection closes, causing a poll() busy-loop on the dead fd.
   */
  bool              self_destruct;

  /* Client-only: connection info */
  char     remote_host[256];     /**< IP address (TCP connect target)       */
  char     remote_hostname[256]; /**< mDNS hostname (SNI, §9.4)            */
  char     remote_path[128];
  uint16_t remote_port;
};

/**
 * @brief Wake the LWS service thread to process pending writable work.
 *
 * This function may be called from any producer thread or during connection
 * teardown. It must not inspect the detached service-thread wrapper.
 */
static void request_writable(struct lws* wsi) {
  if (!wsi) return;

  struct lws_context* ctx = lws_get_context(wsi);
  if (!ctx) return;

  lws_cancel_service(ctx);
}

/* ── Custom OpenSSL verify callback ─────────────────────────────────────────
 *
 * SHIP §12.1 (lines 857-868): TLS MUST require the peer to present a
 * certificate (mTLS), but PKI chain validation failures MUST NOT be fatal –
 * self-signed certificates are the norm in SHIP. Public-key trust is decided
 * by the SHIP Hello phase after TLS completes.
 *
 * Before allowing the TLS handshake to complete, this callback validates the
 * peer leaf certificate's internal SHIP identity: SubjectKeyIdentifier must
 * equal SHA-1(subjectPublicKey), as required by SHIP §12.2 and RFC 3280
 * §4.2.1.2 method (1). This is distinct from PKI and public-key trust.
 */
static eebus_err_t ship_tls_verify_leaf_ski_binding(X509* certificate) {
  unsigned char* der = NULL;
  int der_len = certificate ? i2d_X509(certificate, &der) : 0;
  char ski[SHIP_SKI_STR_LEN] = {0};
  eebus_err_t result = der_len > 0 && der
      ? ship_tls_compute_ski_from_der((const uint8_t*)der, (size_t)der_len, ski)
      : EEBUS_ERR_CERT;
  OPENSSL_free(der);
  return result;
}

static void ship_tls_log_ski_binding_rejection(eebus_err_t result) {
  EEBUS_LOG_ERROR(LOG_TAG_TLS,
                  "Rejecting peer TLS certificate: SubjectKeyIdentifier must match "
                  "SHA-1(subjectPublicKey) required by SHIP §12.2 / RFC 3280 "
                  "§4.2.1.2 method (1): %s",
                  eebus_err_str(result));
}

static int ship_tls_verify_cb(int preverify_ok, X509_STORE_CTX* ctx) {
  (void)preverify_ok; /* Ignore OpenSSL's PKI verdict. */
  if (!ctx || X509_STORE_CTX_get_error_depth(ctx) != 0) return 1;

  eebus_err_t result = ship_tls_verify_leaf_ski_binding(
      X509_STORE_CTX_get_current_cert(ctx));
  if (result == EEBUS_OK) return 1;

  ship_tls_log_ski_binding_rejection(result);
  X509_STORE_CTX_set_error(ctx, X509_V_ERR_APPLICATION_VERIFICATION);
  return 0;
}

static int ship_tls_client_cert_verify_cb(
    X509_STORE_CTX* ctx, void* userdata) {
  (void)userdata;
  eebus_err_t result = ship_tls_verify_leaf_ski_binding(
      ctx ? X509_STORE_CTX_get0_cert(ctx) : NULL);
  if (result != EEBUS_OK) {
    ship_tls_log_ski_binding_rejection(result);
    if (ctx) {
      X509_STORE_CTX_set_error(ctx, X509_V_ERR_APPLICATION_VERIFICATION);
    }
    return 0;
  }

  X509_STORE_CTX_set_error(ctx, X509_V_OK);
  return 1;
}

static eebus_err_t extract_verified_peer_identity(
    struct lws* wsi, char ski_out[SHIP_SKI_STR_LEN]) {
  if (!wsi || !ski_out) return EEBUS_ERR_INVALID;

  SSL* ssl = lws_get_ssl(wsi);
  if (!ssl) return EEBUS_ERR_TLS;

  X509* peer_cert = SSL_get_peer_certificate(ssl);
  if (!peer_cert) return EEBUS_ERR_CERT;

  unsigned char* der = NULL;
  int der_len = i2d_X509(peer_cert, &der);
  X509_free(peer_cert);
  if (der_len <= 0 || !der) {
    OPENSSL_free(der);
    return EEBUS_ERR_CERT;
  }

  eebus_err_t result = ship_tls_compute_ski_from_der(
      (const uint8_t*)der, (size_t)der_len, ski_out);
  OPENSSL_free(der);
  return result;
}

/* ── LWS callback ──────────────────────────────────────────────────────── */

static int lws_callback(
    struct lws*                 wsi,
    enum lws_callback_reasons   reason,
    void*                       user,
    void*                       in,
    size_t                      len
) {
  /* Retrieve our ship_ws from the protocol's user pointer */
  struct lws_context*    ctx = lws_get_context(wsi);
  ship_ws_t* ws = (ship_ws_t*)lws_context_user(ctx);
  ship_ws_per_session_t* sess = (ship_ws_per_session_t*)user;

  switch (reason) {

    /* ── SSL_CTX setup (server) ─────────────────────────────────────────────
     * Called once during lws_create_context() for the server role.
     * `user` is the SSL_CTX*.  We install our custom verify callback so
     * that self-signed peer certificates are accepted at the TLS layer
     * while still requiring the client to present a certificate (mTLS).
     */
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS: {
      SSL_CTX* ssl_ctx = (SSL_CTX*)user;
      if (ssl_ctx) {
        SSL_CTX_set_verify(ssl_ctx,
                           SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           ship_tls_verify_cb);
        SSL_CTX_set_info_callback(ssl_ctx, openssl_info_cb);

        /*
         * Load our own certificate as a trusted CA so that OpenSSL sends a
         * TLS CertificateRequest during the handshake (mTLS).
         *
         * Background: when no CA is loaded the server's trust store is empty.
         * Some OpenSSL/LWS builds omit CertificateRequest in that case, which
         * means the client never sends its certificate and peer_ski stays empty
         * after the handshake.  Loading our cert (self-signed by definition)
         * as the CA gives OpenSSL a non-empty trust store so it always sends
         * CertificateRequest. The PKI verdict is ignored after the peer
         * certificate passes the SHIP SKI binding check, so self-signed peer
         * certificates are still accepted.
         *
         * ssl_ca_cert_filepath achieves the same but that struct field was
         * added in a later LWS release and is absent in LWS 4.3.  Using
         * SSL_CTX_load_verify_locations() directly is version-independent.
         */
        if (ws && ws->config.tls && ws->config.tls->cert_path[0] != '\0') {
          if (SSL_CTX_load_verify_locations(
                  ssl_ctx, ws->config.tls->cert_path, NULL) != 1) {
            EEBUS_LOG_WARN(LOG_TAG,
                "Server SSL_CTX: SSL_CTX_load_verify_locations(%s) failed – "
                "CertificateRequest may be omitted by some TLS stacks; "
                "peer cert extraction may fail",
                ws->config.tls->cert_path);
          } else {
            EEBUS_LOG_DEBUG(LOG_TAG,
                "Server SSL_CTX: own cert loaded as CA trust anchor (mTLS)");
          }
        }

        /* SHIP §9.1: prefer AEAD and retain the mandatory CBC suite. */
        if (SSL_CTX_set_cipher_list(ssl_ctx, SHIP_TLS12_CIPHER_LIST) != 1) {
          EEBUS_LOG_WARN(LOG_TAG,
              "Server SSL_CTX: failed to configure SHIP TLS 1.2 cipher suites");
        }

        /* SHIP §9.4: TLS 1.2. */
        SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);

        /* SHIP §9.5: TLS renegotiation SHALL NOT be supported */
        /* SHIP §9.3: TLS compression MUST NOT be used          */
        SSL_CTX_set_options(
            ssl_ctx,
            SSL_OP_CIPHER_SERVER_PREFERENCE |
            SSL_OP_NO_RENEGOTIATION |
            SSL_OP_NO_COMPRESSION);

        /* SHIP §9.2: outgoing TLS fragments SHALL NOT exceed 1024 bytes */
        SSL_CTX_set_max_send_fragment(ssl_ctx, 1024);

        /*
         * Depth 0: only the peer's own certificate is inspected.
         * The custom verification callback accepts self-signed certificates
         * after validating their SHIP SKI binding, while chain traversal is
         * skipped. Trust is determined at the SHIP Hello layer via SKI
         * comparison.
         */
        SSL_CTX_set_verify_depth(ssl_ctx, 0);

        /*
         * SHIP §9.6 SHOULD: TLS session resumption speeds up reconnection.
         *
         * Server side: set a non-empty session_id_context so that OpenSSL's
         * built-in server-side session cache (SSL_SESS_CACHE_SERVER, enabled
         * by default) actually accepts session resumption requests.
         * Without this, OpenSSL silently rejects every resumption attempt
         * because the empty default context cannot be matched.
         *
         * The context string identifies this application; it need not be
         * secret – it just disambiguates sessions from different servers
         * sharing the same process.
         */
        static const unsigned char k_sess_ctx[] = "eebus-ship";
        SSL_CTX_set_session_id_context(ssl_ctx,
                                       k_sess_ctx,
                                       sizeof(k_sess_ctx) - 1);
        SSL_CTX_sess_set_cache_size(ssl_ctx, 128);

        EEBUS_LOG_DEBUG(LOG_TAG,
            "Server SSL_CTX: mTLS required, PKI chain check disabled, "
            "TLS1.2 only, renegotiation/compression disabled, "
            "session resumption enabled (SHIP §9.1/§9.4/§9.5/§9.6/§12.1)");
      }
      return 0;
    }

    /* ── SSL_CTX setup (client) ─────────────────────────────────────────────
     * Called once during lws_create_context() for the client role.
     * Install the SHIP certificate verification callback. It accepts
     * self-signed certificates while rejecting a leaf certificate whose
     * SubjectKeyIdentifier does not match its public key.
     */
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
      SSL_CTX* ssl_ctx = (SSL_CTX*)user;
      if (ssl_ctx) {
        SSL_CTX_set_cert_verify_callback(
            ssl_ctx, ship_tls_client_cert_verify_cb, NULL);
        SSL_CTX_set_info_callback(ssl_ctx, openssl_info_cb);

        /* SHIP §9.1: prefer AEAD and retain the mandatory CBC suite. */
        if (SSL_CTX_set_cipher_list(ssl_ctx, SHIP_TLS12_CIPHER_LIST) != 1) {
          EEBUS_LOG_WARN(LOG_TAG,
              "Client SSL_CTX: failed to configure SHIP TLS 1.2 cipher suites");
        }

        /* SHIP §9.4: TLS 1.2. */
        SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);

        /* SHIP §9.5: no renegotiation; SHIP §9.3: no TLS compression */
        SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);

        /* SHIP §9.2: outgoing TLS fragments SHALL NOT exceed 1024 bytes */
        SSL_CTX_set_max_send_fragment(ssl_ctx, 1024);

        /* Same depth/trust policy as server (see server comment above) */
        SSL_CTX_set_verify_depth(ssl_ctx, 0);

        /*
         * SHIP §9.6 SHOULD: TLS session resumption.
         *
         * Client side: explicitly enable client session cache so that
         * OpenSSL stores session tickets received from the server.
         * Session tickets (RFC 5077) require no server-side state and work
         * as long as SSL_OP_NO_TICKET is NOT set (we do not set it).
         *
         * Architecture note: lws creates a new SSL_CTX per connection
         * (SHIP_WS_MODE_CLIENT creates a new lws_context each time).
         * This means cached sessions cannot survive across reconnections
         * at the SSL_CTX level.  Full cross-connection resumption would
         * require persisting SSL_SESSION* objects across SSL_CTX instances,
         * which is not supported by the lws callback API.  The server-side
         * fix (session_id_context) is the more impactful half of §9.6.
         */
        SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_CLIENT);

        EEBUS_LOG_DEBUG(LOG_TAG,
            "Client SSL_CTX: PKI chain check disabled, "
            "TLS1.2 only, renegotiation/compression disabled, "
            "session cache enabled (SHIP §9.1/§9.4/§9.5/§9.6/§12.1)");
      }
      return 0;
    }

    /* ── Client connecting (TCP+TLS in progress) ────────────────────────── */
    case LWS_CALLBACK_CONNECTING: {
      /*
       * Set a per-wsi TLS handshake timeout (30 s).
       *
       * Problem: if a peer accepts the TCP connection but never sends a TLS
       * ServerHello, lws spins in a tight SSL_connect → WANT_READ → poll-ready
       * → SSL_connect loop indefinitely, flooding logs with "SSL_connect says -2".
       * LWS_CALLBACK_CLIENT_ESTABLISHED never fires so the CMI timer is never
       * started – there is no upper-layer watchdog to escape the loop.
       *
       * Solution: lws_set_timeout() arms a per-wsi deadline inside lws's own
       * poll loop.  On expiry lws closes the wsi and fires
       * LWS_CALLBACK_CLIENT_CONNECTION_ERROR, which is already handled below
       * (sets stop_requested → service thread exits).
       *
       * PENDING_TIMEOUT_AWAITING_SERVER_RESPONSE is a general-purpose wsi
       * timeout available in all LWS 4.x releases.  30 s matches the value
       * previously set via connect_timeout_secs (which is absent in LWS 4.3).
       */
      lws_set_timeout(wsi, PENDING_TIMEOUT_AWAITING_SERVER_RESPONSE, 30);
      break;
    }

    /* ── Connection established ─────────────────────────────────────────── */
    case LWS_CALLBACK_ESTABLISHED:
    case LWS_CALLBACK_CLIENT_ESTABLISHED: {
      /* Initialise the TX ring queue for this new connection */
      if (sess) session_init(sess);

      char peer_ip[64] = {0};
      lws_get_peer_simple(wsi, peer_ip, sizeof(peer_ip));

      /* Extract the already TLS-validated peer identity before starting SHIP. */
      char peer_ski[SHIP_SKI_STR_LEN] = {0};
      eebus_err_t identity_result = extract_verified_peer_identity(
          wsi, peer_ski);
      if (identity_result != EEBUS_OK) {
        EEBUS_LOG_ERROR(LOG_TAG,
            "Rejecting peer TLS certificate after handshake: unable to extract "
            "validated identity: %s",
            eebus_err_str(identity_result));
        return -1;
      }

      /* LWS_CALLBACK_ESTABLISHED   → peer connected to us → we are SERVER
       * LWS_CALLBACK_CLIENT_ESTABLISHED → we connected out → we are CLIENT */
      bool is_server = (reason == LWS_CALLBACK_ESTABLISHED);

      EEBUS_LOG_INFO(LOG_TAG, "WebSocket connected: %s  peer_ski=%s  role=%s",
                     peer_ip, peer_ski[0] ? peer_ski : "(unknown)",
                     is_server ? "SERVER" : "CLIENT");
      if (ws->config.on_connected) {
        ws->config.on_connected((void*)wsi, peer_ip, peer_ski, is_server,
                                ws, ws->config.userdata);
      }
      break;
    }

    /* ── Incoming binary frame (with reassembly) ─────────────────────────
     *
     * SHIP §10.3: "Exactly one SHIP message per WebSocket message."
     * lws may deliver a single WS message across multiple RECEIVE callbacks
     * (WS fragmentation, TCP segmentation, TLS record boundaries).
     * Accumulate fragments and deliver only when the full message arrives.
     */
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if (sess) {
        eebus_err_t rx_result = rx_append(sess, (const uint8_t*)in, len);
        if (rx_result != EEBUS_OK) {
          if (rx_result == EEBUS_ERR_OVERFLOW) {
            EEBUS_LOG_WARN(
                LOG_TAG,
                "WebSocket message exceeds the maximum size of %u bytes; "
                "closing the connection",
                SHIP_WS_MAX_MESSAGE_SIZE);
          } else {
            EEBUS_LOG_ERROR(
                LOG_TAG,
                "Failed to aggregate WebSocket message: %s; "
                "closing the connection",
                eebus_err_str(rx_result));
          }
          rx_free(sess);
          return -1;
        }

        if (lws_is_final_fragment(wsi)) {
          EEBUS_LOG_DEBUG(LOG_TAG, "WS message received: %zu bytes", sess->rx_len);
          if (ws->config.on_receive) {
            ws->config.on_receive((void*)wsi, sess->rx_buf, sess->rx_len,
                                  ws->config.userdata);
          }
          rx_reset(sess);
        } else {
          EEBUS_LOG_DEBUG(LOG_TAG, "WS fragment received: %zu bytes (accumulating)",
                         len);
        }
      }
      break;
    }

    /* ── Write-ready: flush one frame from the TX ring queue ────────────── */
    case LWS_CALLBACK_SERVER_WRITEABLE:
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      if (sess) {
        eebus_mutex_lock(sess->tx_mutex);
        size_t   frame_len = 0;
        uint8_t* frame     = tx_dequeue(sess, &frame_len);
        int      more      = sess->tx_count;   /* frames still waiting */
        bool     close_pending = sess->close_pending;
        eebus_mutex_unlock(sess->tx_mutex);

        if (frame) {
          int written = lws_write(wsi, frame + LWS_PRE, frame_len,
                                  LWS_WRITE_BINARY);
          if (written < 0) {
            EEBUS_LOG_ERROR(LOG_TAG, "lws_write failed");
          }
          EEBUS_FREE(frame);

          /* Re-arm WRITEABLE immediately if more frames are waiting */
          if (more > 0) {
            lws_callback_on_writable(wsi);
            break;   /* do NOT check close_pending until queue is drained */
          }
        }

        /*
         * Queue drained.  Now honour close_pending: ship_ws_close_wsi()
         * sets this flag and requests a WRITEABLE callback so that any
         * final SHIP message (e.g. closeCall) is transmitted before the
         * wsi is torn down.  Returning -1 tells lws to close the wsi;
         * LWS_CALLBACK_{CLIENT_,}CLOSED will fire afterwards.
         */
        if (close_pending) {
          EEBUS_LOG_DEBUG(LOG_TAG, "close_pending: closing wsi");
          return -1;
        }
      }
      break;
    }

    /* ── Connection closed ──────────────────────────────────────────────── */
    case LWS_CALLBACK_CLOSED:
    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      if (reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
        const char* detail = (in && len > 0) ? (const char*)in : "(no detail)";
        EEBUS_LOG_ERROR(LOG_TAG, "WebSocket connection error: %s", detail);
        log_openssl_errors("TLS/WebSocket connection failed");
      } else {
        EEBUS_LOG_INFO(LOG_TAG, "WebSocket closed");
      }

      /*
       * For CLIENT connections: signal the service thread to exit after this
       * lws_service() call returns.  ship_node_connect_peer() does not retain
       * the ship_ws_t*, so no external caller will call ship_ws_stop/destroy.
       * Without this, the service thread spins forever on the dead fd.
       *
       * For SERVER connections (LWS_CALLBACK_CLOSED): do NOT set stop_requested
       * – the server lws_context must keep running to accept new connections.
       */
      if (reason == LWS_CALLBACK_CLIENT_CLOSED ||
          reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
        ws->stop_requested = true;
      }

      if (sess) {
        eebus_mutex_lock(sess->tx_mutex);
        tx_drain_free(sess);
        eebus_mutex_unlock(sess->tx_mutex);
        eebus_mutex_destroy(sess->tx_mutex);
        rx_free(sess);
      }
      if (ws->config.on_closed) {
        ws->config.on_closed((void*)wsi, ws->config.userdata);
      }
      break;
    }

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      /* Woken by lws_cancel_service() from a non-service thread.
       * Arm WRITEABLE for all wsi so pending TX frames get drained.
       * WRITEABLE callback checks tx_count; empty queues are no-ops. */
      lws_callback_on_writable_all_protocol(
          lws_get_context(wsi), lws_get_protocol(wsi));
      break;

    default:
      break;
  }

  return 0;
}

/* ── Protocol table ──────────────────────────────────────────────────────── */

static struct lws_protocols s_protocols[] = {
  {
    .name                  = SHIP_WS_SUBPROTOCOL,
    .callback              = lws_callback,
    .per_session_data_size = sizeof(ship_ws_per_session_t),
    .rx_buffer_size        = SHIP_WS_MAX_MESSAGE_SIZE,
  },
  LWS_PROTOCOL_LIST_TERM
};

/* ── Background service thread ───────────────────────────────────────────── */

static void ws_service_thread(void* arg) {
  ship_ws_t* ws = (ship_ws_t*)arg;
  EEBUS_LOG_DEBUG(LOG_TAG, "WebSocket service thread started");

  while (!ws->stop_requested) {
    lws_service(ws->lws_ctx, 50 /* ms timeout */);
  }

  EEBUS_LOG_DEBUG(LOG_TAG, "WebSocket service thread exiting");

  /*
   * Self-destruct path for CLIENT connections.
   *
   * ship_node_connect_peer() does not retain the ship_ws_t* it creates, so
   * no external caller will ever call ship_ws_stop() / ship_ws_destroy() on
   * it.  We destroy the lws_context and free the struct here, from within
   * the service thread, after the loop has exited cleanly.
   *
   * The thread was detached in ship_ws_start() so eebus_thread_join() will
   * not be called by anyone.
   */
  if (ws->self_destruct) {
    struct lws_context* ctx = ws->lws_ctx;
    ws->lws_ctx = NULL;
    ws->running = false;
    EEBUS_FREE(ws);            /* ws pointer is invalid after this point */
    lws_context_destroy(ctx);  /* must be called after free to avoid use-after-free */
  }

}

/* ── lws context creation helpers ────────────────────────────────────────── */

/**
 * @brief Build a common lws_context_creation_info with mTLS credentials.
 *
 * TLS credentials are supplied via filesystem paths
 * (ssl_cert_filepath / ssl_private_key_filepath for the server role,
 *  client_ssl_cert_filepath / client_ssl_private_key_filepath for the
 *  client role).  The caller must ensure that cred->cert_path and
 *  cred->key_path are non-empty before calling this function — i.e. either
 *  ship_tls_load() or ship_tls_save() must have been called first.
 *
 * @return EEBUS_OK on success, EEBUS_ERR_INVALID if paths are missing.
 */
static eebus_err_t fill_creation_info(
    struct lws_context_creation_info* info,
    ship_ws_t*                        ws,
    int                               port /* CONTEXT_PORT_NO_LISTEN for client */
) {
  memset(info, 0, sizeof(*info));

  info->port      = port;
  info->protocols = s_protocols;
  info->user      = ws;   /* accessible via lws_context_user() */
  info->options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

  /*
   * WebSocket-layer ping/pong – SHIP §10.4 (primary mechanism):
   *   "A SHIP node SHALL make use of the ping and pong control frames."
   *   "A SHIP node MUST NOT send ping messages at intervals smaller than
   *    50 seconds on a single connection."
   *   "The typical timeout waiting period for a pong message … SHALL be
   *    10 seconds."
   *
   * lws retry_and_idle_policy (lws 4.x) sends WS-layer ping frames
   * (opcode 0x9) and closes the connection on pong timeout.
   */
  static const lws_retry_bo_t ship_retry_policy = {
      .secs_since_valid_ping   = 50,  /* SHIP §10.4: ping interval >= 50s */
      .secs_since_valid_hangup = 60,  /* 50 + 10s pong timeout */
  };
  info->retry_and_idle_policy = &ship_retry_policy;

  /*
   * TCP keepalive (secondary fallback):
   * Not required by SHIP spec, but provides an extra safety net for
   * detecting half-open TCP connections where the peer process crashed
   * without sending FIN. Uses a longer interval to avoid interference
   * with the primary WS ping/pong mechanism.
   */
  info->ka_time     = 120; /* first probe after 120s idle */
  info->ka_probes   = 3;   /* 3 unanswered probes before dropping */
  info->ka_interval = 10;  /* 10s between probes */

  /* SHIP §13.2: "Extensions SHALL NOT be used." Explicitly disable to
   * prevent negotiation even if lws was built with extension support. */
  info->extensions = NULL;

  if (ws->config.tls) {
    const ship_tls_cred_t* tls = ws->config.tls;

    /* Validate that file paths have been populated */
    if (tls->cert_path[0] == '\0' || tls->key_path[0] == '\0') {
      EEBUS_LOG_ERROR(LOG_TAG,
          "TLS credential has no file paths set. "
          "Call ship_tls_load() or ship_tls_save() before creating the "
          "WebSocket context.");
      return EEBUS_ERR_INVALID;
    }

    if (port != CONTEXT_PORT_NO_LISTEN) {
      /* ── Server role ────────────────────────────────────────────────────
       * lws uses ssl_cert_filepath / ssl_private_key_filepath for the
       * local certificate presented to clients.
       *
       * NOTE: We do NOT set LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT
       * here because that flag forces OpenSSL to run PKI chain validation,
       * which rejects self-signed certificates.  SHIP §12.1 requires that
       * self-signed certs be accepted.  Instead, we install a custom verify
       * callback via LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS that
       * requires the client to present a certificate (mTLS) while ignoring
       * the PKI verdict.
       *
       * ssl_ca_cert_filepath: supplying our own certificate as the "CA" causes
       * lws / OpenSSL to build a trust store that includes it, which ensures
       * lws correctly sends a TLS CertificateRequest during the handshake.
       * Without this, some lws builds omit CertificateRequest when the CA
       * store is empty, resulting in no client certificate being sent by the
       * peer and an empty peer_ski after the handshake.
       * The PKI verdict is ignored after the peer certificate passes the SHIP
       * SKI binding check, so setting this path does not prevent self-signed
       * peer certificates from being accepted.                              */
      info->ssl_cert_filepath        = tls->cert_path;
      info->ssl_private_key_filepath = tls->key_path;
      /*
       * ssl_ca_cert_filepath is NOT used here: the field was added in a later
       * LWS version and is absent in LWS 4.3.  Instead we load the cert as a
       * CA trust anchor directly on the SSL_CTX in the callback
       * LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS below, which is
       * available in all LWS 4.x releases.  See that callback for rationale.
       */
    } else {
      /* ── Client role ────────────────────────────────────────────────────
       * lws uses client_ssl_cert_filepath / client_ssl_private_key_filepath
       * for the certificate the client presents during the mTLS handshake. */
      info->client_ssl_cert_filepath        = tls->cert_path;
      info->client_ssl_private_key_filepath = tls->key_path;
    }
  }

  return EEBUS_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_ws_server_create(ship_ws_t** ws_out, const ship_ws_config_t* config) {
  if (!ws_out || !config) return EEBUS_ERR_INVALID;

  ship_ws_t* ws = EEBUS_NEW(ship_ws_t);
  if (!ws) return EEBUS_ERR_NO_MEMORY;

  memcpy(&ws->config, config, sizeof(ws->config));
  ws->mode = SHIP_WS_MODE_SERVER;

  struct lws_context_creation_info info;
  eebus_err_t fill_err = fill_creation_info(&info, ws, (int)config->port);
  if (fill_err != EEBUS_OK) {
    EEBUS_FREE(ws);
    return fill_err;
  }

  ship_ws_init_logging();

  ws->lws_ctx = lws_create_context(&info);
  if (!ws->lws_ctx) {
    EEBUS_LOG_ERROR(LOG_TAG, "lws_create_context failed");
    EEBUS_FREE(ws);
    return EEBUS_ERR_INTERNAL;
  }

  *ws_out = ws;
  EEBUS_LOG_INFO(LOG_TAG, "WebSocket server created on port %u", config->port);
  return EEBUS_OK;
}

eebus_err_t ship_ws_client_connect(
    ship_ws_t**             ws_out,
    const ship_ws_config_t* config,
    const char*             host,
    uint16_t                port,
    const char*             path,
    const char*             hostname
) {
  if (!ws_out || !config || !host) return EEBUS_ERR_INVALID;

  ship_ws_t* ws = EEBUS_NEW(ship_ws_t);
  if (!ws) return EEBUS_ERR_NO_MEMORY;

  memcpy(&ws->config, config, sizeof(ws->config));
  ws->mode         = SHIP_WS_MODE_CLIENT;
  ws->remote_port  = port;
  ws->self_destruct = true;  /* CLIENT ship_ws_t is self-owned; see ws_service_thread */
  snprintf(ws->remote_host, sizeof(ws->remote_host), "%s", host);

  /*
   * SHIP §9.4: SNI MUST be supported.
   * ci.host is what lws puts into the TLS SNI extension.
   * ci.address is the TCP connect target (may be an IP address).
   * When the caller supplies an explicit mDNS hostname, use it for SNI;
   * otherwise fall back to using host (works when host is already a name).
   */
  const char* sni = (hostname && hostname[0] != '\0') ? hostname : host;
  snprintf(ws->remote_hostname, sizeof(ws->remote_hostname), "%s", sni);

  snprintf(ws->remote_path, sizeof(ws->remote_path), "%s",
           (path && path[0] != '\0') ? path : "/");

  struct lws_context_creation_info info;
  eebus_err_t fill_err = fill_creation_info(&info, ws, CONTEXT_PORT_NO_LISTEN);
  if (fill_err != EEBUS_OK) {
    EEBUS_FREE(ws);
    return fill_err;
  }
  /* LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT is already set by fill_creation_info */

  ship_ws_init_logging();

  ws->lws_ctx = lws_create_context(&info);
  if (!ws->lws_ctx) {
    EEBUS_LOG_ERROR(LOG_TAG, "lws_create_context (client) failed");
    EEBUS_FREE(ws);
    return EEBUS_ERR_INTERNAL;
  }

  /* Schedule the outbound connection */
  struct lws_client_connect_info ci;
  memset(&ci, 0, sizeof(ci));
  ci.context        = ws->lws_ctx;
  ci.address        = ws->remote_host;     /* TCP connect target (IP or name) */
  ci.port           = ws->remote_port;
  ci.path           = ws->remote_path;
  ci.host           = ws->remote_hostname; /* SNI hostname (§9.4)            */
  ci.origin         = ws->remote_hostname;
  ci.protocol       = SHIP_WS_SUBPROTOCOL;
  ci.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED |
                      LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
  /*
   * TLS handshake timeout is handled in LWS_CALLBACK_CONNECTING via
   * lws_set_timeout() – see lws_callback() above.  connect_timeout_secs
   * achieves the same but was added in a later LWS version and is absent
   * in LWS 4.3.
   */

  if (!lws_client_connect_via_info(&ci)) {
    EEBUS_LOG_ERROR(LOG_TAG, "lws_client_connect_via_info failed for %s:%u", host, port);
    lws_context_destroy(ws->lws_ctx);
    EEBUS_FREE(ws);
    return EEBUS_ERR_INTERNAL;
  }

  *ws_out = ws;
  EEBUS_LOG_INFO(LOG_TAG, "Connecting to %s (SNI=%s):%u%s",
                 host, ws->remote_hostname, port, path ? path : "/");
  return EEBUS_OK;
}

eebus_err_t ship_ws_start(ship_ws_t* ws) {
  if (!ws) return EEBUS_ERR_INVALID;
  if (ws->running) return EEBUS_ERR_EXISTS;

  ws->stop_requested = false;
  if (eebus_thread_create(&ws->thread, ws_service_thread, ws, "ws-svc", 0) != 0) {
    return EEBUS_ERR_INTERNAL;
  }

  /*
   * CLIENT connections are self-destructing: the service thread frees the
   * ship_ws_t after the loop exits, so no external eebus_thread_join()
   * will be called.  Detach immediately so the thread's resources are reclaimed
   * automatically by the OS when it exits.
   */
  if (ws->self_destruct) {
    eebus_thread_detach(ws->thread);
  }

  ws->running = true;
  return EEBUS_OK;
}

void ship_ws_stop(ship_ws_t* ws) {
  if (!ws || !ws->running) return;
  /* Self-destruct connections are detached; eebus_thread_join would be undefined. */
  if (ws->self_destruct) return;

  ws->stop_requested = true;
  lws_cancel_service(ws->lws_ctx);
  eebus_thread_join(ws->thread);
  ws->running = false;
}

void ship_ws_destroy(ship_ws_t* ws) {
  if (!ws) return;
  /* Self-destruct connections free themselves from the service thread. */
  if (ws->self_destruct) return;
  if (ws->running) ship_ws_stop(ws);
  if (ws->lws_ctx) lws_context_destroy(ws->lws_ctx);
  EEBUS_FREE(ws);
}

eebus_err_t ship_ws_send(void* wsi_ptr, uint8_t* data, size_t len) {
  if (!wsi_ptr || !data || len == 0) return EEBUS_ERR_INVALID;
  struct lws* wsi = (struct lws*)wsi_ptr;

  /* lws_write must be called from the service thread (writeable callback).
   * This function assumes the caller is already in that context. */
  int written = lws_write(wsi, data + LWS_PRE, len, LWS_WRITE_BINARY);
  if (written < 0) {
    EEBUS_LOG_ERROR(LOG_TAG, "lws_write returned %d", written);
    return EEBUS_ERR_IO;
  }
  return EEBUS_OK;
}

void ship_ws_queue_frame(void* wsi_ptr, uint8_t* lws_buf, size_t len, void* userdata) {
  (void)userdata;  /* Transport-level; unused here (wsi carries all context) */
  if (!wsi_ptr || !lws_buf) { EEBUS_FREE(lws_buf); return; }
  struct lws* wsi = (struct lws*)wsi_ptr;

  /*
   * Enqueue the frame into the per-wsi TX ring and request a WRITEABLE
   * callback so the LWS service thread drains it.
   *
   *   1. Caller prepares a (LWS_PRE + payload) buffer and transfers ownership.
   *   2. We append it to the ring queue under the session mutex.
 *   3. The writable request wakes or schedules work on the service thread.
   *   4. WRITEABLE callback dequeues one frame per invocation, calls
   *      lws_write(), frees the buffer, and re-arms if more remain.
   *
   * If the queue is already full, tx_enqueue() drops the *oldest* frame
   * (rather than the new one) and emits a WARNING.  This should not happen
   * in normal SHIP operation; it indicates a bug in the upper layers.
   */
  ship_ws_per_session_t* sess = (ship_ws_per_session_t*)lws_wsi_user(wsi);
  if (!sess) {
    EEBUS_LOG_WARN(LOG_TAG, "ship_ws_queue_frame: no per-session data (wsi closing?)");
    EEBUS_FREE(lws_buf);
    return;
  }

  eebus_mutex_lock(sess->tx_mutex);
  if (sess->close_pending) {
    eebus_mutex_unlock(sess->tx_mutex);
    EEBUS_FREE(lws_buf);
    return;
  }

  bool was_empty = (sess->tx_count == 0);
  if (sess->tx_count == TX_QUEUE_DEPTH) {
    EEBUS_LOG_WARN(LOG_TAG,
        "ship_ws_queue_frame: TX queue full (%d slots) – dropping oldest frame",
        TX_QUEUE_DEPTH);
  }
  tx_enqueue(sess, lws_buf, len);
  eebus_mutex_unlock(sess->tx_mutex);

  /* Wake the LWS service thread to drain the TX queue. */
  if (was_empty) {
    request_writable(wsi);
  }
}

void ship_ws_close_wsi(void* wsi_ptr, void* userdata) {
  (void)userdata;
  if (!wsi_ptr) return;
  struct lws* wsi = (struct lws*)wsi_ptr;

  /*
   * lws 4.x has no lws_close_free_wsi() public API. The close request is
   * recorded under the session mutex, then the service thread is asked to
   * flush pending TX data and close the wsi from its WRITEABLE callback.
   */
  ship_ws_per_session_t* sess =
      (ship_ws_per_session_t*)lws_wsi_user(wsi);
  if (sess && sess->tx_mutex) {
    eebus_mutex_lock(sess->tx_mutex);
    sess->close_pending = true;
    eebus_mutex_unlock(sess->tx_mutex);
  }
  request_writable(wsi);
}

/* ── Remote address accessors ────────────────────────────────────────────── */

const char* ship_ws_get_remote_host(const ship_ws_t* ws) {
  return ws ? ws->remote_host : "";
}

const char* ship_ws_get_remote_hostname(const ship_ws_t* ws) {
  return ws ? ws->remote_hostname : "";
}

uint16_t ship_ws_get_remote_port(const ship_ws_t* ws) {
  return ws ? ws->remote_port : 0;
}

const char* ship_ws_get_remote_path(const ship_ws_t* ws) {
  return ws ? ws->remote_path : "/ship/";
}
