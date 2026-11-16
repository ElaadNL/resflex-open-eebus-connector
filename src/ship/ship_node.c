// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE   /* strnlen */

/**
 * @file  ship_node.c
 * @brief SHIP node manager: orchestrates TLS, mDNS, WebSocket, and connections.
 */

#include "ship_node.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"
#include "../platform/eebus_mutex.h"
#include "../platform/eebus_timer.h"
#include "connection/ship_connection.h"
#include "mdns/ship_mdns.h"
#include "tls/ship_tls.h"
#include "websocket/ship_ws.h"

#define LOG_TAG "ship_node"

/* ── Per-connection tracking ─────────────────────────────────────────────── */

typedef struct {
  void*            wsi;          /**< lws wsi pointer (lookup key)           */
  ship_conn_t*     conn;         /**< SHIP handshake state machine            */

  /**
   * Peer SKI.  Populated immediately in on_ws_connected() from the TLS
   * certificate (before any SHIP messages are exchanged), so it is available
   * for §12.2.2 duplicate-connection detection during the handshake.
   * Overwritten with the same value again by on_conn_handshake_complete()
   * once the Access Methods phase confirms it.
   */
  char             peer_ski[41];

  /* Outbound connection details (CLIENT role only).
   * Stored here so the reconnect logic can re-dial the same peer.        */
  bool     is_outbound;
  char     peer_host[256];     /**< IP address for TCP connect            */
  char     peer_hostname[256]; /**< mDNS hostname for SNI (§9.4)         */
  uint16_t peer_port;
  char     peer_path[128];

  /* Connection lifecycle tracking for upper-layer callback decisions.    */
  bool        is_established; /**< True once on_conn_handshake_complete fired */
  eebus_err_t last_error;     /**< Set by on_conn_error; default EEBUS_OK     */
} ship_active_conn_t;

/* ── Reconnect state ─────────────────────────────────────────────────────── */

/**
 * @brief State for one pending reconnect attempt.
 *
 * Allocated in the ship_node_t::reconnects[] fixed array.  When a trusted
 * outbound connection drops unexpectedly, a slot is activated and the
 * backoff timer is started.  On success (handshake complete) the slot is
 * cleared.
 *
 * Backoff: delay_ms = min(1000 * 2^attempts, RECONNECT_MAX_DELAY_MS)
 */
#define RECONNECT_BASE_DELAY_MS  1000U   /* 1 s  */
#define RECONNECT_MAX_DELAY_MS  60000U   /* 60 s */

typedef struct {
  bool             active;
  char             peer_ski[41];
  char             peer_host[256];     /**< IP address for TCP connect    */
  char             peer_hostname[256]; /**< mDNS hostname for SNI (§9.4) */
  uint16_t         peer_port;
  char             peer_path[128];
  uint8_t          attempts;        /**< number of retries so far */
  eebus_timer_t*   timer;
  struct ship_node* node;           /**< back-pointer for timer callback */
} ship_reconnect_slot_t;

/* ── Node structure ──────────────────────────────────────────────────────── */

/* Maximum number of mDNS-discovered peers kept in the cache. */
#define SHIP_NODE_MAX_DISCOVERED_PEERS  64

/**
 * @brief One entry in the mDNS peer discovery cache.
 *
 * Any peer seen via mDNS is stored here (regardless of trust status) so that
 * ship_node_trust_ski() can immediately connect to it without waiting for
 * another mDNS announcement (SHIP §5 Registration: "SHALL search for SHIP
 * nodes with trusted SKI values and connect to them").
 */
typedef struct {
  bool             valid;
  ship_peer_info_t info;   /**< All peer info; ship_peer_info_t is a public value type */
} ship_discovered_peer_t;

struct ship_node {
  ship_node_config_t config;

  /* TLS credentials */
  ship_tls_cred_t tls;

  /* Sub-modules */
  ship_mdns_t* mdns;
  ship_ws_t*   server_ws;

  /* Active connections (protected by conn_mutex) */
  eebus_mutex_t*      conn_mutex;
  ship_active_conn_t  conns[SHIP_NODE_MAX_CONNECTIONS];
  size_t              conn_count;

  /* Trusted SKI whitelist (protected by trust_mutex) */
  eebus_mutex_t* trust_mutex;
  char           trusted_skis[SHIP_NODE_MAX_TRUSTED_SKIS][41];
  size_t         trusted_count;

  /*
   * Auto-accept window (SHIP §12.3.1.1, protected by trust_mutex).
   * When active, the first unknown peer SKI received in the Hello phase
   * is automatically trusted (user trust level = 8) and the window closes.
   * Maximum duration: 120 s per spec; default: 60 s.
   */
  bool           auto_accept_active;
  eebus_timer_t* auto_accept_timer;

  bool started;

  /* Pending reconnect slots (protected by reconnect_mutex) */
  eebus_mutex_t*        reconnect_mutex;
  ship_reconnect_slot_t reconnects[SHIP_NODE_MAX_CONNECTIONS];

  /*
   * mDNS peer discovery cache (protected by conn_mutex).
   * Every peer seen via mDNS is upserted here so ship_node_trust_ski() can
   * immediately initiate an outbound connection even when trust is granted
   * after the initial mDNS announcement.
   */
  ship_discovered_peer_t discovered_peers[SHIP_NODE_MAX_DISCOVERED_PEERS];
  size_t                 discovered_count;

  /*
   * [Removed] pending_host/hostname/port/path – these shared fields caused a
   * race condition when two concurrent ship_node_connect_peer() calls ran in
   * parallel.  Outbound address info is now passed directly through the
   * ship_ws_t transport handle (ship_ws_get_remote_host() etc.) which is
   * per-connection and free of contention.
   */
};

/* ── Internal helpers ────────────────────────────────────────────────────── */

/** Find an active connection by wsi pointer. Caller holds conn_mutex. */
static ship_active_conn_t* find_by_wsi(ship_node_t* n, void* wsi) {
  for (size_t i = 0; i < n->conn_count; i++) {
    if (n->conns[i].wsi == wsi) return &n->conns[i];
  }
  return NULL;
}

/** Find an active connection by peer SKI. Caller holds conn_mutex. */
static ship_active_conn_t* find_by_ski(ship_node_t* n, const char* ski) {
  if (!ski || ski[0] == '\0') return NULL;
  for (size_t i = 0; i < n->conn_count; i++) {
    if (strncmp(n->conns[i].peer_ski, ski, 40) == 0) return &n->conns[i];
  }
  return NULL;
}

/** Find an active connection by ship_conn_t pointer. Caller holds conn_mutex. */
static ship_active_conn_t* find_by_conn(ship_node_t* n, const ship_conn_t* conn) {
  for (size_t i = 0; i < n->conn_count; i++) {
    if (n->conns[i].conn == conn) return &n->conns[i];
  }
  return NULL;
}

/** Remove a connection slot and release its registry reference. Caller holds conn_mutex. */
static void remove_conn(ship_node_t* n, ship_active_conn_t* ac) {
  ship_conn_destroy(ac->conn);
  /* Swap with last to keep array compact */
  size_t idx  = (size_t)(ac - n->conns);
  size_t last = n->conn_count - 1;
  if (idx != last) {
    n->conns[idx] = n->conns[last];
  }
  n->conn_count--;
}

/* ── ship_conn callbacks (called from connection's internal context) ──────── */

/* Forward declarations – reconnect helpers are defined later in this file */
static ship_reconnect_slot_t* find_reconnect(ship_node_t* n, const char* ski);
static void clear_reconnect(ship_reconnect_slot_t* s);

static void on_conn_handshake_complete(ship_conn_t* conn,
                                        const char* peer_ski,
                                        void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  /*
   * peer_ski was already stored in the connection slot at on_ws_connected()
   * from the TLS certificate. No need to write it again here.
   *
   * Mark the slot as established so on_ws_closed can decide whether to
   * fire on_disconnected (paired with on_connected) or on_connect_error.
   */
  eebus_mutex_lock(n->conn_mutex);
  ship_active_conn_t* ac_est = find_by_conn(n, conn);
  if (ac_est) ac_est->is_established = true;
  eebus_mutex_unlock(n->conn_mutex);

  /* Handshake succeeded - cancel any pending reconnect for this peer */
  eebus_mutex_lock(n->reconnect_mutex);
  ship_reconnect_slot_t* rs = find_reconnect(n, peer_ski);
  if (rs) {
    EEBUS_LOG_DEBUG(LOG_TAG,
                    "Handshake complete for %s - clearing reconnect slot",
                    peer_ski);
    clear_reconnect(rs);
  }
  eebus_mutex_unlock(n->reconnect_mutex);

  const char* session_id = ship_conn_get_session_id(conn);
  EEBUS_LOG_INFO(LOG_TAG, "%s peer connected: %s", session_id, peer_ski);

  if (n->config.callbacks.on_connected) {
    n->config.callbacks.on_connected(session_id, peer_ski,
                                     n->config.callbacks.userdata);
  }
}

static void on_conn_spine_data(ship_conn_t* conn,
                                const ship_event_spine_data_t* data,
                                void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  if (n->config.callbacks.on_spine_data) {
    n->config.callbacks.on_spine_data(
        ship_conn_get_session_id(conn),
        ship_conn_get_peer_ski(conn),
        data->json,
        data->len,
        n->config.callbacks.userdata
    );
  }
}

/* ── Reconnect helpers ───────────────────────────────────────────────────── */

/** Find an active reconnect slot by peer SKI.  Caller must hold reconnect_mutex. */
static ship_reconnect_slot_t* find_reconnect(ship_node_t* n, const char* ski) {
  for (size_t i = 0; i < SHIP_NODE_MAX_CONNECTIONS; i++) {
    if (n->reconnects[i].active
        && strncmp(n->reconnects[i].peer_ski, ski, 40) == 0) {
      return &n->reconnects[i];
    }
  }
  return NULL;
}

/** Find an unused reconnect slot. */
static ship_reconnect_slot_t* alloc_reconnect(ship_node_t* n) {
  for (size_t i = 0; i < SHIP_NODE_MAX_CONNECTIONS; i++) {
    if (!n->reconnects[i].active) return &n->reconnects[i];
  }
  return NULL;
}

/** Cancel and free a reconnect slot. */
static void clear_reconnect(ship_reconnect_slot_t* s) {
  if (!s || !s->active) return;
  eebus_timer_stop(s->timer);
  eebus_timer_destroy(s->timer);
  s->timer  = NULL;
  s->active = false;
}

/** Compute exponential backoff: 1 s × 2^attempts, capped at 60 s. */
static unsigned int reconnect_delay_ms(uint8_t attempts) {
  unsigned int d = RECONNECT_BASE_DELAY_MS;
  for (uint8_t i = 0; i < attempts; i++) {
    d *= 2;
    if (d >= RECONNECT_MAX_DELAY_MS) return RECONNECT_MAX_DELAY_MS;
  }
  return d;
}

/* Forward declaration – timer callback needs ship_node_connect_peer */
static void on_reconnect_timer(void* userdata);

/**
 * @brief Arm (or re-arm) a reconnect slot for an outbound peer.
 *
 * @param reset_attempts  Pass true to reset the backoff level (attempts=0,
 *                        first retry after base delay); false to preserve
 *                        the current backoff level for fault-driven retries.
 */
static void schedule_reconnect(ship_node_t*  n,
                                const char*   ski,
                                const char*   host,
                                uint16_t      port,
                                const char*   path,
                                const char*   hostname,
                                bool          reset_attempts) {
  if (!n->started) return;

  /* Fall back to host when hostname not available */
  const char* sni = (hostname && hostname[0] != '\0') ? hostname : host;

  eebus_mutex_lock(n->reconnect_mutex);

  ship_reconnect_slot_t* s = find_reconnect(n, ski);
  if (!s) {
    s = alloc_reconnect(n);
    if (!s) {
      eebus_mutex_unlock(n->reconnect_mutex);
      EEBUS_LOG_WARN(LOG_TAG, "No free reconnect slot for %s", ski);
      return;
    }
    memset(s, 0, sizeof(*s));
    s->active = true;
    s->node   = n;
    snprintf(s->peer_ski,      sizeof(s->peer_ski),      "%s", ski);
    snprintf(s->peer_host,     sizeof(s->peer_host),     "%s", host);
    snprintf(s->peer_hostname, sizeof(s->peer_hostname), "%s", sni);
    snprintf(s->peer_path,     sizeof(s->peer_path),     "%s", path ? path : "/ship/");
    s->peer_port = port;

    if (eebus_timer_create(&s->timer, on_reconnect_timer, s) != EEBUS_OK) {
      s->active = false;
      eebus_mutex_unlock(n->reconnect_mutex);
      EEBUS_LOG_ERROR(LOG_TAG, "Failed to create reconnect timer for %s", ski);
      return;
    }
  } else {
    /* Update address (mDNS may have resolved a newer IP) */
    snprintf(s->peer_host,     sizeof(s->peer_host),     "%s", host);
    snprintf(s->peer_hostname, sizeof(s->peer_hostname), "%s", sni);
    if (path) snprintf(s->peer_path, sizeof(s->peer_path), "%s", path);
    s->peer_port = port;
  }

  if (reset_attempts) s->attempts = 0;

  unsigned int delay = reconnect_delay_ms(s->attempts);
  EEBUS_LOG_INFO(LOG_TAG,
                 "Reconnect to %s in %u ms (attempt %u)",
                 ski, delay, (unsigned)s->attempts + 1);
  eebus_timer_start(s->timer, delay, false);

  eebus_mutex_unlock(n->reconnect_mutex);
}

static void on_reconnect_timer(void* userdata) {
  ship_reconnect_slot_t* s = (ship_reconnect_slot_t*)userdata;
  ship_node_t*           n = s->node;

  /* Copy fields to stack under lock, then release before calling connect */
  eebus_mutex_lock(n->reconnect_mutex);
  if (!n->started || !s->active) {
    eebus_mutex_unlock(n->reconnect_mutex);
    return;
  }

  char     host[256], hostname[256], path[128], ski[41];
  uint16_t port;
  snprintf(host,     sizeof(host),     "%s", s->peer_host);
  snprintf(hostname, sizeof(hostname), "%s", s->peer_hostname);
  snprintf(path,     sizeof(path),     "%s", s->peer_path);
  snprintf(ski,      sizeof(ski),      "%s", s->peer_ski);
  port = s->peer_port;
  s->attempts++;
  eebus_mutex_unlock(n->reconnect_mutex);

  /* Check if already connected (needs conn_mutex, not reconnect_mutex) */
  eebus_mutex_lock(n->conn_mutex);
  bool already = (find_by_ski(n, ski) != NULL);
  eebus_mutex_unlock(n->conn_mutex);

  if (already) {
    EEBUS_LOG_INFO(LOG_TAG,
                   "Reconnect to %s cancelled - already connected", ski);
    eebus_mutex_lock(n->reconnect_mutex);
    clear_reconnect(s);
    eebus_mutex_unlock(n->reconnect_mutex);
    return;
  }

  EEBUS_LOG_INFO(LOG_TAG, "Reconnect attempt %u to %s",
                 (unsigned)s->attempts, ski);

  eebus_err_t rc = ship_node_connect_peer(n, host, port, path, hostname);
  if (rc != EEBUS_OK) {
    /* Connection setup failed immediately – reschedule */
    EEBUS_LOG_WARN(LOG_TAG, "Reconnect to %s failed (%s) - rescheduling",
                   ski, eebus_err_str(rc));
    eebus_mutex_lock(n->reconnect_mutex);
    if (s->active && s->timer) {
      unsigned int delay = reconnect_delay_ms(s->attempts);
      eebus_timer_start(s->timer, delay, false);
    }
    eebus_mutex_unlock(n->reconnect_mutex);
  }
  /*
   * On success the timer is one-shot and will not fire again.
   * The slot is cleared in on_conn_handshake_complete() once the
   * new connection reaches DATA_EXCHANGE.
   */
}

/* ── ship_conn callbacks ─────────────────────────────────────────────────── */

static void on_conn_closed(ship_conn_t* conn,
                            ship_close_reason_t reason,
                            void* userdata) {
  (void)userdata;

  EEBUS_LOG_INFO(LOG_TAG, "%s connection closed: reason=%s",
                 ship_conn_get_session_id(conn),
                 ship_close_reason_str(reason));
  /*
   * User notification is deferred to on_ws_closed, which is the single
   * decision point for on_disconnected / on_connect_error.  The reason
   * is already stored on the connection via ship_conn_get_pending_close_reason().
   */
}

static void on_conn_error(ship_conn_t* conn,
                          eebus_err_t err,
                          void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  EEBUS_LOG_ERROR(LOG_TAG, "%s connection error: %s",
                  ship_conn_get_session_id(conn), eebus_err_str(err));

  /* Record the error on the slot so on_ws_closed can surface it via
   * on_connect_error (outbound only).  Inbound failures remain silent. */
  eebus_mutex_lock(n->conn_mutex);
  ship_active_conn_t* ac = find_by_conn(n, conn);
  if (ac) ac->last_error = err;
  eebus_mutex_unlock(n->conn_mutex);
}

/* ── WebSocket callbacks ─────────────────────────────────────────────────── */

/**
 * @brief Trust-check callback wired into every ship_conn_config_t.
 *
 * Called by the SHIP connection state machine at Hello-phase entry
 * (SHIP §13.4.4.1).  Checks the node's trusted-SKI whitelist and,
 * if auto_accept is active, accepts the first unknown SKI automatically.
 */
static ship_trust_result_t on_conn_trust_check(ship_conn_t* conn,
                                                const char* peer_ski,
                                                void* userdata) {
  (void)conn;
  ship_node_t* n = (ship_node_t*)userdata;

  if (ship_node_is_trusted(n, peer_ski)) {
    return SHIP_TRUST_READY;
  }

  /* Check auto_accept window */
  eebus_mutex_lock(n->trust_mutex);
  bool aa = n->auto_accept_active;
  eebus_mutex_unlock(n->trust_mutex);

  if (aa) {
    /*
     * Auto-accept: trust this SKI immediately (user trust level = 8,
     * SHIP §12.3.1.1).  Add to whitelist, close the window, update mDNS.
     * ship_node_trust_ski() is safe to call here – it acquires trust_mutex
     * internally and does not call back into the connection layer.
     */
    EEBUS_LOG_INFO(LOG_TAG, "auto_accept: trusting new peer SKI %s", peer_ski);
    ship_node_trust_ski(n, peer_ski);
    ship_node_stop_auto_accept(n);   /* one device per window */
    return SHIP_TRUST_READY;
  }

  /* Neither trusted nor auto_accept active → keep PENDING so the upper
   * layer can still call ship_node_trust_ski().
   * Notify the application so the user can be prompted (SHIP §12.3.1.2:
   * "the SHIP node MUST inform the user when unknown SKI values are
   * presented during a TLS handshake").                                   */
  if (n->config.callbacks.on_pairing_requested) {
    n->config.callbacks.on_pairing_requested(
        peer_ski, n->config.callbacks.userdata);
  }
  return SHIP_TRUST_PENDING;
}

static void on_ws_connected(void* wsi, const char* peer_ip,
                            const char* peer_ski, bool is_server,
                            ship_ws_t* transport, void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  EEBUS_LOG_INFO(LOG_TAG, "New WebSocket connection from %s  peer_ski=%s  role=%s",
                 peer_ip, peer_ski[0] ? peer_ski : "(unknown)",
                 is_server ? "SERVER" : "CLIENT");

  eebus_mutex_lock(n->conn_mutex);

  if (n->conn_count >= SHIP_NODE_MAX_CONNECTIONS) {
    EEBUS_LOG_WARN(
        LOG_TAG,
        "Connection limit reached (%u); closing WebSocket connection",
        SHIP_NODE_MAX_CONNECTIONS);
    eebus_mutex_unlock(n->conn_mutex);
    ship_ws_close_wsi(wsi, NULL);
    return;
  }

  /* Build ship_conn config for this wsi */
  ship_conn_config_t cc = {
    .role             = is_server ? SHIP_ROLE_SERVER : SHIP_ROLE_CLIENT,
    .hello_timeout_ms = n->config.hello_timeout_ms,
    .on_trust_check   = on_conn_trust_check,
    .on_close_wsi     = ship_ws_close_wsi,
    /*
     * TX path: the connection layer builds the frame and calls send_frame
     * to hand it off to the transport.  ship_ws_queue_frame() stores it in
     * the lws per-wsi session data and requests a WRITEABLE callback, which
     * then calls lws_write().  This is the correct lws TX pattern.
     */
    .send_frame       = ship_ws_queue_frame,
    .callbacks = {
      .on_handshake_complete = on_conn_handshake_complete,
      .on_spine_data         = on_conn_spine_data,
      .on_closed             = on_conn_closed,
      .on_error              = on_conn_error,
    },
    /* Callback identity is passed as ship_conn_t*; userdata is the node. */
    .userdata         = n,
  };
  memcpy(cc.local_ski, n->tls.ski, 40);
  cc.local_ski[40] = '\0';
  /* §13.4.6 Table 24: accessMethods.id is the SHIP id, not the SKI */
  snprintf(cc.instance_name, sizeof(cc.instance_name), "%s",
           n->config.instance_name);
  snprintf(cc.ship_id, sizeof(cc.ship_id), "%s",
           n->config.ship_id[0] ? n->config.ship_id : n->config.instance_name);
  /* Populate tls_peer_ski so session_id can be built at start */
  snprintf(cc.tls_peer_ski, sizeof(cc.tls_peer_ski), "%s", peer_ski);

  /*
   * Step 1: Create ship_conn and allocate the NEW slot BEFORE scanning for
   * §12.2.2 duplicates.  This guarantees: when an old connection's
   * on_ws_closed eventually fires, has_other_conn will correctly see the
   * new slot and suppress spurious reconnect.
   */
  ship_conn_t* conn = NULL;
  if (ship_conn_create(&conn, &cc) != EEBUS_OK) {
    EEBUS_LOG_ERROR(LOG_TAG, "ship_conn_create failed");
    eebus_mutex_unlock(n->conn_mutex);
    return;
  }

  ship_active_conn_t* slot = &n->conns[n->conn_count++];
  slot->wsi       = wsi;
  slot->conn      = conn;

  /* Populate peer_ski immediately from the TLS-derived SKI so that
   * find_by_ski() works for in-progress connections (e.g. mDNS auto-connect
   * guard).  on_conn_handshake_complete() will overwrite it with the same
   * value once Access Methods confirms it.   */
  snprintf(slot->peer_ski, sizeof(slot->peer_ski), "%s", peer_ski);

  /* For CLIENT connections record the outbound address so the reconnect
   * logic can re-dial without going back through mDNS.
   *
   * We read directly from the per-connection transport handle rather than
   * from n->pending_* to avoid the race condition that exists when two
   * concurrent ship_node_connect_peer() calls overwrite the shared fields
   * before on_ws_connected() has consumed them.                            */
  if (!is_server) {
    slot->is_outbound = true;
    snprintf(slot->peer_host,     sizeof(slot->peer_host),
             "%s", ship_ws_get_remote_host(transport));
    snprintf(slot->peer_hostname, sizeof(slot->peer_hostname),
             "%s", ship_ws_get_remote_hostname(transport));
    slot->peer_port = ship_ws_get_remote_port(transport);
    snprintf(slot->peer_path, sizeof(slot->peer_path),
             "%s", ship_ws_get_remote_path(transport));
  }

  /*
   * Step 2: §12.2.2 Prevent Double Connections with SKI Comparison.
   *
   * "The SHIP node with the greater SKI SHOULD check for double connections
   *  directly during the TLS handshake."
   *
   * Rules:
   *  - local SKI >= peer SKI → we close all old duplicates for this peer.
   *  - local SKI <  peer SKI → peer will close its old duplicates; we just
   *    log the situation.
   *
   * In both cases the NEW connection we just allocated is kept.  Do NOT
   * call remove_conn() for the old slots: we want on_ws_closed() to fire
   * naturally so the application is notified via on_disconnected (for
   * established old connections) or on_connect_error (outbound, early state).
   *
   * Identify the new slot by existing->conn == conn (pointer on heap-allocated
   * ship_conn_t, invariant under any future array moves).
   */
  ship_conn_t* duplicate_conns[SHIP_NODE_MAX_CONNECTIONS];
  void*        duplicate_wsis[SHIP_NODE_MAX_CONNECTIONS];
  size_t       duplicate_count = 0;

  if (peer_ski[0] != '\0') {
    bool we_close_old = (strncmp(n->tls.ski, peer_ski, 40) >= 0);

    for (size_t i = 0; i < n->conn_count; i++) {
      ship_active_conn_t* existing = &n->conns[i];
      if (existing->conn == conn) continue;                    /* skip new slot */
      if (strncmp(existing->peer_ski, peer_ski, 40) != 0) continue;

      if (we_close_old) {
        /*
         * The registry lock protects the lookup and reference acquisition.
         * The actual close is performed after the registry lock is released.
         */
        if (duplicate_count >= SHIP_NODE_MAX_CONNECTIONS) break;
        if (!ship_conn_retain(existing->conn)) continue;

        duplicate_conns[duplicate_count] = existing->conn;
        duplicate_wsis[duplicate_count] = existing->wsi;
        duplicate_count++;

        EEBUS_LOG_INFO(LOG_TAG,
                       "§12.2.2 scheduling old duplicate connection close "
                       "for peer %s",
                       peer_ski);
      } else {
        EEBUS_LOG_WARN(LOG_TAG,
                       "§12.2.2 duplicate connection for peer %s detected; "
                       "peer is responsible for closing old",
                       peer_ski);
      }
    }
  }

  /* Keep the new connection alive while the handshake is started outside the
   * registry lock.  The connection slot owns the other reference. */
  bool start_ref = ship_conn_retain(conn);
  eebus_mutex_unlock(n->conn_mutex);

  for (size_t i = 0; i < duplicate_count; i++) {
    eebus_err_t close_rc =
        ship_conn_close(duplicate_conns[i], SHIP_CLOSE_REASON_UNSPECIFIC);

    if (close_rc != EEBUS_OK) {
      /* Early states cannot send connectionClose; close the transport. */
      ship_ws_close_wsi(duplicate_wsis[i], NULL);
    }

    ship_conn_release(duplicate_conns[i]);
  }

  /* Start SHIP handshake */
  if (start_ref) {
    ship_conn_start(conn, NULL, wsi);
    ship_conn_release(conn);
  }
}

static void on_ws_receive(void* wsi, const uint8_t* data, size_t len, void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  eebus_mutex_lock(n->conn_mutex);
  ship_active_conn_t* ac = find_by_wsi(n, wsi);
  ship_conn_t* conn = ac ? ac->conn : NULL;

  if (!conn || !ship_conn_retain(conn)) {
    eebus_mutex_unlock(n->conn_mutex);
    EEBUS_LOG_WARN(LOG_TAG, "Received frame on closing or unknown connection");
    return;
  }
  eebus_mutex_unlock(n->conn_mutex);

  ship_conn_receive(conn, data, len);
  ship_conn_release(conn);
}

static void on_ws_closed(void* wsi, void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  eebus_mutex_lock(n->conn_mutex);
  ship_active_conn_t* ac = find_by_wsi(n, wsi);
  if (!ac) {
    eebus_mutex_unlock(n->conn_mutex);
    return;
  }

  /*
   * Capture all values we need before remove_conn() destroys ac->conn.
   *
   * User-callback policy (single decision point):
   *   - is_established == true  → fire on_disconnected (paired with the
   *                                earlier on_connected), with the recorded
   *                                pending_close_reason (NONE for unexpected
   *                                drops, UNSPECIFIC/REMOVED_CONNECTION for
   *                                graceful closes).
   *   - is_established == false && is_outbound → fire on_connect_error
   *                                (user initiated the connect; they deserve
   *                                 feedback).
   *   - is_established == false && !is_outbound → silent.
   *
   * Reconnect policy:
   *   - Graceful close (pending_reason != NONE) → no reconnect.
   *   - Other active connection exists for same peer → no reconnect (dedup).
   *   - Otherwise → reconnect if outbound + trusted.
   */
  ship_close_reason_t pending_reason =
      ship_conn_get_pending_close_reason(ac->conn);
  bool                is_outbound    = ac->is_outbound;
  bool                is_established = ac->is_established;
  eebus_err_t         last_error     = ac->last_error;

  char session_id[40];
  snprintf(session_id, sizeof(session_id), "%s",
           ship_conn_get_session_id(ac->conn));

  char peer_ski[41];
  snprintf(peer_ski, sizeof(peer_ski), "%s", ac->peer_ski);

  char     host[256]     = {0};
  char     hostname[256] = {0};
  uint16_t port          = 0;
  char     path[128]     = {0};
  snprintf(host,     sizeof(host),     "%s", ac->peer_host);
  snprintf(hostname, sizeof(hostname), "%s", ac->peer_hostname);
  port = ac->peer_port;
  snprintf(path, sizeof(path), "%s", ac->peer_path);

  remove_conn(n, ac);

  /* After remove_conn(): find_by_ski will only return OTHER connections with
   * the same SKI (the just-removed one is gone from the array). */
  bool has_other_conn = (find_by_ski(n, peer_ski) != NULL);

  bool should_reconnect =
      is_outbound
      && ship_node_is_trusted(n, peer_ski)
      && pending_reason == SHIP_CLOSE_REASON_NONE
      && !has_other_conn;

  eebus_mutex_unlock(n->conn_mutex);

  /* User callback */
  if (is_established) {
    /* Paired with a prior on_connected – always notify. */
    if (n->config.callbacks.on_disconnected) {
      n->config.callbacks.on_disconnected(
          session_id, peer_ski, pending_reason,
          n->config.callbacks.userdata);
    }
  } else if (is_outbound) {
    if (n->config.callbacks.on_connect_error) {
      eebus_err_t err = (last_error != EEBUS_OK)
                            ? last_error
                            : EEBUS_ERR_DISCONNECTED;
      n->config.callbacks.on_connect_error(
          host, port, path, hostname, err,
          n->config.callbacks.userdata);
    }
  }
  /* else: inbound connection failed before handshake → silent */

  if (should_reconnect) {
    schedule_reconnect(n, peer_ski, host, port, path, hostname,
                       false /* preserve backoff level */);
  }
}

/* ── mDNS callback ───────────────────────────────────────────────────────── */

static void on_mdns_peer_found(const ship_mdns_peer_t* peer, void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;

  EEBUS_LOG_INFO(LOG_TAG, "mDNS peer discovered: %s ski=%s id=%s",
                 peer->instance_name, peer->ski,
                 peer->id && peer->id[0] ? peer->id : "(none)");

  /* ── 1. Upsert into the discovery cache ──────────────────────────────
   *
   * Lookup key: prefer "id" (SHIP §7.3.2 permanent device identifier).
   * Fall back to "ski" only when id is absent (malformed TXT record).
   * This ensures that certificate rotation (new SKI, same id) updates
   * the existing slot rather than creating a duplicate entry.
   */
  eebus_mutex_lock(n->conn_mutex);
  ship_discovered_peer_t* slot = NULL;
  for (size_t i = 0; i < n->discovered_count; i++) {
    ship_discovered_peer_t* dp = &n->discovered_peers[i];
    if (!dp->valid) continue;
    if (peer->id && peer->id[0] && dp->info.id[0] &&
        strncmp(dp->info.id, peer->id, sizeof(dp->info.id) - 1) == 0) {
      /* id match — covers cert rotation (ski may have changed) */
      slot = dp;
      break;
    }
    if ((!peer->id || !peer->id[0]) &&
        peer->ski && peer->ski[0] && dp->info.ski[0] &&
        strncmp(dp->info.ski, peer->ski, 40) == 0) {
      /* fallback: no id present, match by ski */
      slot = dp;
      break;
    }
  }
  if (!slot && n->discovered_count < SHIP_NODE_MAX_DISCOVERED_PEERS) {
    slot = &n->discovered_peers[n->discovered_count++];
  }
  if (slot) {
    slot->valid = true;
    snprintf(slot->info.instance_name, sizeof(slot->info.instance_name), "%s",
             peer->instance_name ? peer->instance_name : "");
    snprintf(slot->info.ski,      sizeof(slot->info.ski),
             "%s", peer->ski  ? peer->ski  : "");
    snprintf(slot->info.id,       sizeof(slot->info.id),
             "%s", peer->id   ? peer->id   : "");
    snprintf(slot->info.host,     sizeof(slot->info.host),
             "%s", peer->host ? peer->host : "");
    snprintf(slot->info.hostname, sizeof(slot->info.hostname), "%s",
             peer->hostname ? peer->hostname : (peer->host ? peer->host : ""));
    slot->info.port          = peer->port;
    snprintf(slot->info.path, sizeof(slot->info.path), "%s",
             peer->path ? peer->path : "/ship/");
    slot->info.txtvers       = peer->txtvers;
    slot->info.register_flag = peer->register_flag;
    snprintf(slot->info.brand, sizeof(slot->info.brand),
             "%s", peer->brand ? peer->brand : "");
    snprintf(slot->info.model, sizeof(slot->info.model),
             "%s", peer->model ? peer->model : "");
    snprintf(slot->info.type,  sizeof(slot->info.type),
             "%s", peer->type  ? peer->type  : "");
  }
  eebus_mutex_unlock(n->conn_mutex);

  /* ── 2. Auto-connect to trusted peers ────────────────────────────────
   *
   * Must happen BEFORE on_peer_discovered so that if the application calls
   * ship_node_trust_ski() inside the callback (AUTO mode), the cache is
   * already populated and we don't trigger a second connection attempt here.
   */
  if (ship_node_is_trusted(n, peer->ski)) {
    eebus_mutex_lock(n->conn_mutex);
    bool already_conn = (find_by_ski(n, peer->ski) != NULL);
    eebus_mutex_unlock(n->conn_mutex);

    eebus_mutex_lock(n->reconnect_mutex);
    bool pending_reconnect = (find_reconnect(n, peer->ski) != NULL);
    eebus_mutex_unlock(n->reconnect_mutex);

    if (!already_conn && !pending_reconnect) {
      EEBUS_LOG_INFO(LOG_TAG, "Auto-connecting to trusted peer %s", peer->ski);
      ship_node_connect_peer(n, peer->host, peer->port, peer->path, peer->hostname);
    } else if (pending_reconnect) {
      /* mDNS re-announcement means peer is online - reset backoff and
       * reconnect immediately with the fresh address. */
      schedule_reconnect(n, peer->ski, peer->host, peer->port, peer->path,
                         peer->hostname, true);
    }
  }

  /* ── 3. Notify application (cache and auto-connect already done) ─────
   *
   * Placing the callback last ensures:
   *   a) ship_node_trust_ski() called from within the callback will find a
   *      fully populated cache entry and can initiate a connection.
   *   b) In AUTO mode, trust_mgr_on_discovered() → ship_node_trust_ski()
   *      sees the already-running auto-connect and will not create a
   *      duplicate connection.
   */
  if (n->config.callbacks.on_peer_discovered) {
    ship_peer_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.instance_name, sizeof(info.instance_name), "%s",
             peer->instance_name ? peer->instance_name : "");
    snprintf(info.ski,      sizeof(info.ski),      "%s", peer->ski  ? peer->ski  : "");
    snprintf(info.id,       sizeof(info.id),       "%s", peer->id   ? peer->id   : "");
    snprintf(info.host,     sizeof(info.host),     "%s", peer->host ? peer->host : "");
    snprintf(info.hostname, sizeof(info.hostname), "%s",
             peer->hostname ? peer->hostname : (peer->host ? peer->host : ""));
    info.port          = peer->port;
    snprintf(info.path, sizeof(info.path), "%s",
             peer->path ? peer->path : "/ship/");
    info.txtvers       = peer->txtvers;
    info.register_flag = peer->register_flag;
    snprintf(info.brand, sizeof(info.brand), "%s", peer->brand ? peer->brand : "");
    snprintf(info.model, sizeof(info.model), "%s", peer->model ? peer->model : "");
    snprintf(info.type,  sizeof(info.type),  "%s", peer->type  ? peer->type  : "");
    n->config.callbacks.on_peer_discovered(&info, n->config.callbacks.userdata);
  }
}

static void on_mdns_peer_removed(const char* instance_name, void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;
  EEBUS_LOG_INFO(LOG_TAG, "mDNS peer removed: %s", instance_name);

  /* ── 1. Remove from discovery cache and capture a snapshot ───────────
   *
   * mDNS removal events are keyed by instance_name (the DNS-SD service
   * instance label).  We copy the full peer info before removing the slot
   * so that the application callback and the reconnect-cancel logic below
   * both have access to the peer's ski and id.
   */
  ship_peer_info_t removed_info;
  memset(&removed_info, 0, sizeof(removed_info));
  bool found = false;

  eebus_mutex_lock(n->conn_mutex);
  for (size_t i = 0; i < n->discovered_count; i++) {
    ship_discovered_peer_t* dp = &n->discovered_peers[i];
    if (dp->valid && strncmp(dp->info.instance_name, instance_name,
                              sizeof(dp->info.instance_name) - 1) == 0) {
      removed_info = dp->info;   /* copy before removal */
      found = true;
      /* Remove by swapping with the last entry — O(1), order-independent. */
      n->discovered_peers[i] = n->discovered_peers[n->discovered_count - 1];
      memset(&n->discovered_peers[n->discovered_count - 1], 0,
             sizeof(ship_discovered_peer_t));
      n->discovered_count--;
      EEBUS_LOG_DEBUG(LOG_TAG, "Discovery cache removed '%s' (id=%s ski=%s)",
                      instance_name,
                      removed_info.id[0] ? removed_info.id : "(none)",
                      removed_info.ski);
      break;
    }
  }
  eebus_mutex_unlock(n->conn_mutex);

  /* ── 2. Cancel any pending reconnect ─────────────────────────────────
   *
   * If the peer has explicitly withdrawn from mDNS there is no point
   * retrying — it will just time out.  When it re-announces, the
   * on_mdns_peer_found path will trigger auto-connect again.
   */
  if (found && removed_info.ski[0]) {
    eebus_mutex_lock(n->reconnect_mutex);
    ship_reconnect_slot_t* rs = find_reconnect(n, removed_info.ski);
    if (rs) {
      EEBUS_LOG_INFO(LOG_TAG,
          "Cancelling reconnect for %s (peer left mDNS)", removed_info.ski);
      clear_reconnect(rs);
    }
    eebus_mutex_unlock(n->reconnect_mutex);
  }

  /* ── 3. Notify application ────────────────────────────────────────────
   *
   * Pass the full snapshot so the application can identify the device by
   * its permanent id field rather than the mDNS instance_name.
   * If the peer was removed before its info was ever cached (e.g. malformed
   * TXT record filtered by avahi), we still fire with a minimal snapshot
   * containing only the instance_name so the app is informed.
   */
  if (n->config.callbacks.on_peer_lost) {
    if (!found) {
      /* Peer was never cached — build a minimal snapshot from instance_name */
      snprintf(removed_info.instance_name, sizeof(removed_info.instance_name),
               "%s", instance_name ? instance_name : "");
    }
    n->config.callbacks.on_peer_lost(&removed_info,
                                      n->config.callbacks.userdata);
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_node_create(ship_node_t** node_out, const ship_node_config_t* config) {
  if (!node_out || !config) return EEBUS_ERR_INVALID;

  ship_node_t* n = EEBUS_NEW(ship_node_t);
  if (!n) return EEBUS_ERR_NO_MEMORY;

  memcpy(&n->config, config, sizeof(n->config));

  /* Initialise mutexes */
  eebus_err_t rc = eebus_mutex_create(&n->conn_mutex);
  if (rc != EEBUS_OK) goto fail;

  rc = eebus_mutex_create(&n->trust_mutex);
  if (rc != EEBUS_OK) goto fail;

  rc = eebus_mutex_create(&n->reconnect_mutex);
  if (rc != EEBUS_OK) goto fail;

  /* Load or generate TLS credentials */
  char safe_ski[41];
  if (config->cert_path && config->key_path) {
    rc = ship_tls_load(&n->tls, config->cert_path, config->key_path);
    if (rc != EEBUS_OK) {
      EEBUS_LOG_ERROR(LOG_TAG, "Failed to load TLS credentials: %s", eebus_err_str(rc));
      goto fail;
    }
    EEBUS_LOG_INFO(LOG_TAG, "TLS loaded, SKI=%s",
                   eebus_log_sanitize(safe_ski, sizeof(safe_ski), n->tls.ski));
  } else {
    rc = ship_tls_generate(&n->tls, config->instance_name);
    if (rc != EEBUS_OK) {
      EEBUS_LOG_ERROR(LOG_TAG, "Failed to generate TLS credentials: %s", eebus_err_str(rc));
      goto fail;
    }
    EEBUS_LOG_INFO(LOG_TAG, "TLS generated (ephemeral), SKI=%s",
                   eebus_log_sanitize(safe_ski, sizeof(safe_ski), n->tls.ski));
  }

  *node_out = n;
  return EEBUS_OK;

fail:
  eebus_mutex_destroy(n->conn_mutex);
  eebus_mutex_destroy(n->trust_mutex);
  eebus_mutex_destroy(n->reconnect_mutex);
  ship_tls_cred_free(&n->tls);
  EEBUS_FREE(n);
  return rc;
}

eebus_err_t ship_node_start(ship_node_t* n) {
  if (!n) return EEBUS_ERR_INVALID;
  if (n->started) return EEBUS_ERR_EXISTS;

  /* Start WebSocket server first — must be ready before mDNS announces
   * our presence and before auto-connect to trusted peers is triggered. */
  ship_ws_config_t ws_cfg = {
    .tls         = &n->tls,
    .port        = n->config.port,
    .on_connected = on_ws_connected,
    .on_receive   = on_ws_receive,
    .on_closed    = on_ws_closed,
    .userdata     = n,
  };

  eebus_err_t rc = ship_ws_server_create(&n->server_ws, &ws_cfg);
  if (rc != EEBUS_OK) return rc;

  rc = ship_ws_start(n->server_ws);
  if (rc != EEBUS_OK) {
    ship_ws_destroy(n->server_ws);
    n->server_ws = NULL;
    return rc;
  }

  /* Mark node as started before mDNS so that on_mdns_peer_found() can
   * call ship_node_connect_peer() successfully. */
  n->started = true;
  EEBUS_LOG_INFO(LOG_TAG, "SHIP node '%s' started on port %u, SKI=%s",
                 n->config.instance_name, n->config.port, n->tls.ski);

  /* Start mDNS last — at this point the WebSocket server is listening and
   * n->started is true, so any peer discovered immediately can be connected. */
  ship_mdns_config_t mdns_cfg = {
    .port          = n->config.port,
    .register_flag = n->config.register_flag,
  };
  snprintf(mdns_cfg.instance_name, sizeof(mdns_cfg.instance_name),
           "%s", n->config.instance_name);
  /* §7.3.2: mDNS TXT "id" must match accessMethods.id for reverse reconnect */
  snprintf(mdns_cfg.id, sizeof(mdns_cfg.id), "%s",
           n->config.ship_id[0] ? n->config.ship_id : n->config.instance_name);
  memcpy(mdns_cfg.ski, n->tls.ski, 40);
  mdns_cfg.ski[40] = '\0';
  snprintf(mdns_cfg.brand, sizeof(mdns_cfg.brand), "%s", n->config.brand);
  snprintf(mdns_cfg.type,  sizeof(mdns_cfg.type),  "%s", n->config.type);
  snprintf(mdns_cfg.model, sizeof(mdns_cfg.model), "%s", n->config.model);

  rc = ship_mdns_create(
      &n->mdns, &mdns_cfg, on_mdns_peer_found, on_mdns_peer_removed, n
  );
  if (rc != EEBUS_OK) {
    ship_ws_stop(n->server_ws);
    ship_ws_destroy(n->server_ws);
    n->server_ws = NULL;
    n->started = false;
    return rc;
  }

  rc = ship_mdns_start(n->mdns);
  if (rc != EEBUS_OK) {
    ship_mdns_destroy(n->mdns);
    n->mdns = NULL;
    ship_ws_stop(n->server_ws);
    ship_ws_destroy(n->server_ws);
    n->server_ws = NULL;
    n->started = false;
    return rc;
  }

  return EEBUS_OK;
}

void ship_node_stop(ship_node_t* n) {
  if (!n || !n->started) return;

  /* Cancel any active auto_accept window */
  ship_node_stop_auto_accept(n);

  /* Cancel all pending reconnect timers */
  eebus_mutex_lock(n->reconnect_mutex);
  for (size_t i = 0; i < SHIP_NODE_MAX_CONNECTIONS; i++) {
    clear_reconnect(&n->reconnects[i]);
  }
  eebus_mutex_unlock(n->reconnect_mutex);

  /* Close all active connections.
   *
   * During shutdown we do NOT attempt a SHIP-layer graceful close
   * (ship_conn_close → timer + announce → wait for confirm) because:
   *   1. ship_ws_stop() is called immediately after, which joins the lws
   *      service thread and destroys the lws context – there is no time for
   *      a confirm round-trip.
   *   2. Calling ship_conn_close() then ship_conn_destroy() immediately
   *      would create a timer + pending WRITEABLE callback pointing to a
   *      freed conn (UAF).
   *
   * Instead we close the WebSocket transport directly (equivalent to TCP
   * FIN), then destroy the SHIP state machine.  The peer will detect the
   * abrupt close and handle reconnection as per SHIP §12.4.
   */
  eebus_mutex_lock(n->conn_mutex);
  for (size_t i = 0; i < n->conn_count; i++) {
    ship_ws_close_wsi(n->conns[i].wsi, NULL);
    ship_conn_destroy(n->conns[i].conn);
  }
  n->conn_count = 0;
  eebus_mutex_unlock(n->conn_mutex);

  if (n->server_ws) {
    ship_ws_stop(n->server_ws);
    ship_ws_destroy(n->server_ws);
    n->server_ws = NULL;
  }

  if (n->mdns) {
    ship_mdns_stop(n->mdns);
    ship_mdns_destroy(n->mdns);
    n->mdns = NULL;
  }

  n->started = false;
  EEBUS_LOG_INFO(LOG_TAG, "SHIP node stopped");
}

void ship_node_destroy(ship_node_t* n) {
  if (!n) return;
  if (n->started) ship_node_stop(n);
  eebus_timer_destroy(n->auto_accept_timer);
  ship_tls_cred_free(&n->tls);
  eebus_mutex_destroy(n->conn_mutex);
  eebus_mutex_destroy(n->trust_mutex);
  eebus_mutex_destroy(n->reconnect_mutex);
  EEBUS_FREE(n);
}

/* ── Auto-accept window (SHIP §12.3.1.1) ────────────────────────────────── */

static void on_auto_accept_timeout(void* userdata) {
  ship_node_t* n = (ship_node_t*)userdata;
  EEBUS_LOG_INFO(LOG_TAG, "auto_accept window expired");
  ship_node_stop_auto_accept(n);
}

eebus_err_t ship_node_start_auto_accept(ship_node_t* n, unsigned int duration_ms) {
  if (!n) return EEBUS_ERR_INVALID;

  /* Clamp to spec maximum of 120 s */
  if (duration_ms == 0 || duration_ms > 120000U) duration_ms = 60000U;

  eebus_mutex_lock(n->trust_mutex);
  n->auto_accept_active = true;
  eebus_mutex_unlock(n->trust_mutex);

  /* (Re-)start the expiry timer */
  if (!n->auto_accept_timer) {
    eebus_timer_create(&n->auto_accept_timer, on_auto_accept_timeout, n);
  }
  eebus_timer_start(n->auto_accept_timer, duration_ms, false);

  /* Advertise register=true so other nodes know we accept new peers */
  if (n->mdns) ship_mdns_set_register(n->mdns, true);

  EEBUS_LOG_INFO(LOG_TAG, "auto_accept window opened (%u ms)", duration_ms);
  return EEBUS_OK;
}

void ship_node_stop_auto_accept(ship_node_t* n) {
  if (!n) return;

  eebus_mutex_lock(n->trust_mutex);
  bool was_active = n->auto_accept_active;
  n->auto_accept_active = false;
  eebus_mutex_unlock(n->trust_mutex);

  if (n->auto_accept_timer) eebus_timer_stop(n->auto_accept_timer);

  /* Withdraw register=true advertisement */
  if (was_active && n->mdns) ship_mdns_set_register(n->mdns, false);

  if (was_active) EEBUS_LOG_INFO(LOG_TAG, "auto_accept window closed");
}

eebus_err_t ship_node_trust_ski(ship_node_t* n, const char* ski) {
  if (!n || !ski) return EEBUS_ERR_INVALID;
  if (strnlen(ski, 41) != 40) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(n->trust_mutex);

  for (size_t i = 0; i < n->trusted_count; i++) {
    if (strncmp(n->trusted_skis[i], ski, 40) == 0) {
      eebus_mutex_unlock(n->trust_mutex);
      return EEBUS_ERR_EXISTS;
    }
  }

  if (n->trusted_count >= SHIP_NODE_MAX_TRUSTED_SKIS) {
    eebus_mutex_unlock(n->trust_mutex);
    return EEBUS_ERR_OVERFLOW;
  }

  snprintf(n->trusted_skis[n->trusted_count], sizeof(n->trusted_skis[0]), "%s", ski);
  n->trusted_count++;
  eebus_mutex_unlock(n->trust_mutex);

  EEBUS_LOG_INFO(LOG_TAG, "Trusted SKI added: %s", ski);

  /*
   * Activate any connections that are waiting in HELLO_PENDING for this SKI.
   * SHIP §13.4.4.1: once trust is established, switch PENDING → READY.
   * ship_conn_set_ready() is safe to call outside conn_mutex because it
   * acquires its own internal mutex.
   */
  eebus_mutex_lock(n->conn_mutex);
  for (size_t i = 0; i < n->conn_count; i++) {
    ship_active_conn_t* ac = &n->conns[i];
    if (ship_conn_get_state(ac->conn) == SHIP_STATE_HELLO_PENDING
        && strncmp(ship_conn_get_tls_peer_ski(ac->conn), ski, 40) == 0) {
      EEBUS_LOG_INFO(LOG_TAG, "Activating PENDING connection to newly trusted SKI %s",
                     ski);
      ship_conn_t* conn = ac->conn;
      if (!ship_conn_retain(conn)) continue;
      eebus_mutex_unlock(n->conn_mutex);
      ship_conn_set_ready(conn);
      ship_conn_release(conn);
      eebus_mutex_lock(n->conn_mutex);
      /* Re-scan from start: set_ready may trigger callbacks that modify conns[] */
      i = (size_t)-1;  /* i++ in for-loop brings this back to 0 */
    }
  }
  eebus_mutex_unlock(n->conn_mutex);

  /*
   * Auto-connect if this SKI is already in the discovery cache but not yet
   * connected (SHIP §5: "SHALL search for SHIP nodes with trusted SKI values
   * and connect to them to establish the registration").
   * This covers the case where mDNS saw the peer before the user granted trust.
   */
  eebus_mutex_lock(n->reconnect_mutex);
  bool pending_reconnect = (find_reconnect(n, ski) != NULL);
  eebus_mutex_unlock(n->reconnect_mutex);

  eebus_mutex_lock(n->conn_mutex);
  for (size_t i = 0; i < n->discovered_count; i++) {
    ship_discovered_peer_t* dp = &n->discovered_peers[i];
    if (!dp->valid) continue;
    if (strncmp(dp->info.ski, ski, 40) != 0) continue;

    bool already_conn = (find_by_ski(n, ski) != NULL);
    if (!already_conn && !pending_reconnect) {
      /* Copy address before releasing mutex for the connect call */
      char     host[256];     snprintf(host,     sizeof(host),     "%s", dp->info.host);
      char     hostname[256]; snprintf(hostname, sizeof(hostname), "%s", dp->info.hostname);
      uint16_t port = dp->info.port;
      char     path[128];     snprintf(path,     sizeof(path),     "%s", dp->info.path);
      eebus_mutex_unlock(n->conn_mutex);

      EEBUS_LOG_INFO(LOG_TAG,
          "Auto-connecting to newly trusted peer %s (cached mDNS address)", ski);
      ship_node_connect_peer(n, host, port, path, hostname);
      return EEBUS_OK;
    }
    break; /* SKI is unique in the cache */
  }
  eebus_mutex_unlock(n->conn_mutex);

  return EEBUS_OK;
}

eebus_err_t ship_node_revoke_ski(ship_node_t* n, const char* ski) {
  if (!n || !ski) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(n->trust_mutex);

  for (size_t i = 0; i < n->trusted_count; i++) {
    if (strncmp(n->trusted_skis[i], ski, 40) == 0) {
      /* Shift remaining entries */
      if (i < n->trusted_count - 1) {
        memmove(&n->trusted_skis[i], &n->trusted_skis[i + 1],
                (n->trusted_count - i - 1) * 41);
      }
      n->trusted_count--;
      eebus_mutex_unlock(n->trust_mutex);

      /* Close the connection(s) to this peer if any are active.
       *
       * ship_conn_close() only succeeds when the connection is in
       * DATA_EXCHANGE.  In earlier states (CMI, Hello, …) it returns
       * EEBUS_ERR_STATE; in that case we force-close the WebSocket
       * transport directly.  Do NOT remove_conn here: let on_ws_closed
       * fire naturally so outbound callers receive on_connect_error and
       * the cleanup path stays consistent with other intentional-close
       * cases (e.g. §12.2.2 duplicate-connection handling).
       */
      ship_conn_t* close_conns[SHIP_NODE_MAX_CONNECTIONS];
      void*        close_wsis[SHIP_NODE_MAX_CONNECTIONS];
      size_t       close_count = 0;

      eebus_mutex_lock(n->conn_mutex);
      for (size_t j = 0; j < n->conn_count; j++) {
        ship_active_conn_t* ac = &n->conns[j];
        if (strncmp(ac->peer_ski, ski, 40) != 0) continue;

        if (close_count >= SHIP_NODE_MAX_CONNECTIONS) break;

        /*
         * The registry lock protects the lookup and reference acquisition.
         * The actual close is performed after the registry lock is released.
         */
        if (!ship_conn_retain(ac->conn)) continue;

        close_conns[close_count] = ac->conn;
        close_wsis[close_count] = ac->wsi;
        close_count++;
      }
      eebus_mutex_unlock(n->conn_mutex);

      for (size_t j = 0; j < close_count; j++) {
        eebus_err_t close_rc =
            ship_conn_close(close_conns[j],
                            SHIP_CLOSE_REASON_REMOVED_CONNECTION);
        if (close_rc != EEBUS_OK) {
          EEBUS_LOG_INFO(LOG_TAG,
                         "revoke_ski: graceful close unavailable; forcing "
                         "WebSocket close for %s",
                         ski);
          ship_ws_close_wsi(close_wsis[j], NULL);
        }

        ship_conn_release(close_conns[j]);
      }

      /* Cancel any pending reconnect for the revoked SKI */
      eebus_mutex_lock(n->reconnect_mutex);
      ship_reconnect_slot_t* rs = find_reconnect(n, ski);
      if (rs) clear_reconnect(rs);
      eebus_mutex_unlock(n->reconnect_mutex);

      EEBUS_LOG_INFO(LOG_TAG, "Trusted SKI revoked: %s", ski);
      return EEBUS_OK;
    }
  }

  eebus_mutex_unlock(n->trust_mutex);
  return EEBUS_ERR_NOT_FOUND;
}

eebus_err_t ship_node_reject_ski(ship_node_t* n, const char* ski) {
  if (!n || !ski) return EEBUS_ERR_INVALID;

  bool found = false;
  eebus_mutex_lock(n->conn_mutex);
  for (size_t i = 0; i < n->conn_count; i++) {
    ship_active_conn_t* ac = &n->conns[i];
    if (ship_conn_get_state(ac->conn) == SHIP_STATE_HELLO_PENDING
        && strncmp(ship_conn_get_tls_peer_ski(ac->conn), ski, 40) == 0) {
      ship_conn_t* conn = ac->conn;
      if (!ship_conn_retain(conn)) continue;
      eebus_mutex_unlock(n->conn_mutex);
      ship_conn_reject(conn);
      ship_conn_release(conn);
      found = true;
      eebus_mutex_lock(n->conn_mutex);
      /* Re-scan: reject may fire callbacks that modify conns[] */
      i = (size_t)-1;  /* i++ in for-loop brings this back to 0 */
    }
  }
  eebus_mutex_unlock(n->conn_mutex);

  return found ? EEBUS_OK : EEBUS_ERR_NOT_FOUND;
}

bool ship_node_is_trusted(const ship_node_t* n, const char* ski) {
  if (!n || !ski) return false;

  eebus_mutex_lock(n->trust_mutex);
  bool found = false;
  for (size_t i = 0; i < n->trusted_count; i++) {
    if (strncmp(n->trusted_skis[i], ski, 40) == 0) {
      found = true;
      break;
    }
  }
  eebus_mutex_unlock(n->trust_mutex);
  return found;
}

eebus_err_t ship_node_connect_peer(
    ship_node_t* n,
    const char*  host,
    uint16_t     port,
    const char*  path,
    const char*  hostname
) {
  if (!n || !host) return EEBUS_ERR_INVALID;
  if (!n->started) return EEBUS_ERR_STATE;

  /* Fall back to host when no explicit SNI hostname is provided */
  const char* sni = (hostname && hostname[0] != '\0') ? hostname : host;

  ship_ws_config_t ws_cfg = {
    .tls          = &n->tls,
    .port         = n->config.port,
    .on_connected = on_ws_connected,
    .on_receive   = on_ws_receive,
    .on_closed    = on_ws_closed,
    .userdata     = n,
  };

  ship_ws_t* client_ws = NULL;
  eebus_err_t rc = ship_ws_client_connect(&client_ws, &ws_cfg, host, port,
                                           path ? path : "/ship/", sni);
  if (rc != EEBUS_OK) return rc;

  return ship_ws_start(client_ws);
}

/** Find an active connection by session_id. Caller holds conn_mutex. */
static ship_active_conn_t* find_by_session_id(ship_node_t* n, const char* session_id) {
  if (!session_id || session_id[0] == '\0') return NULL;
  for (size_t i = 0; i < n->conn_count; i++) {
    if (strcmp(ship_conn_get_session_id(n->conns[i].conn), session_id) == 0)
      return &n->conns[i];
  }
  return NULL;
}

eebus_err_t ship_node_send_spine(
    ship_node_t* n,
    const char*  session_id,
    const char*  spine_json
) {
  if (!n || !session_id || !spine_json) return EEBUS_ERR_INVALID;

  eebus_mutex_lock(n->conn_mutex);
  ship_active_conn_t* ac = find_by_session_id(n, session_id);
  ship_conn_t* conn = ac ? ac->conn : NULL;

  if (!conn || !ship_conn_retain(conn)) {
    eebus_mutex_unlock(n->conn_mutex);
    return EEBUS_ERR_NOT_FOUND;
  }
  eebus_mutex_unlock(n->conn_mutex);

  eebus_err_t rc = ship_conn_send_spine(conn, spine_json);
  ship_conn_release(conn);
  return rc;
}

eebus_err_t ship_node_get_local_ski(const ship_node_t* n, char ski_out[41]) {
  if (!n || !ski_out) return EEBUS_ERR_INVALID;
  if (n->tls.ski[0] == '\0') return EEBUS_ERR_STATE;
  snprintf(ski_out, 41, "%s", n->tls.ski);
  return EEBUS_OK;
}
