// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/ship_helper.c
 */

#include "ship_helper.h"
#include "eebus_memory.h"

#include <stdio.h>
#include <string.h>

/* ── Internal state ──────────────────────────────────────────────────────── */

struct ship_helper {
  ship_node_t*        node;
  trust_mgr_t*        trust;
  ship_tls_cred_t     cred;
  char                ski[SHIP_SKI_STR_LEN];

  ship_helper_data_fn on_data;
  ship_helper_conn_fn on_conn;
  void (*on_peer_discovered)(const ship_peer_info_t* peer, void* userdata);
  void (*on_peer_lost)(const ship_peer_info_t* peer, void* userdata);
  void*               userdata;
};

/* ── ship_node callbacks (internal) ─────────────────────────────────────── */

static void node_on_spine_data(
    const char* session_id,
    const char* peer_ski,
    const char* json,
    size_t      len,
    void*       userdata)
{
  (void)len;
  ship_helper_t* h = (ship_helper_t*)userdata;
  if (h->on_data) h->on_data(session_id, peer_ski, json, h->userdata);
}

static void node_on_connected(const char* session_id,
                               const char* peer_ski, void* userdata) {
  ship_helper_t* h = (ship_helper_t*)userdata;
  printf("[SHIP] Connected: %s\n", peer_ski);
  if (h->on_conn) h->on_conn(peer_ski, true, session_id, h->userdata);
}

static void node_on_disconnected(
    const char*         session_id,
    const char*         peer_ski,
    ship_close_reason_t reason,
    void*               userdata)
{
  ship_helper_t* h = (ship_helper_t*)userdata;
  printf("[SHIP] Disconnected: %s reason=%s\n",
         peer_ski, ship_close_reason_str(reason));
  if (h->on_conn) h->on_conn(peer_ski, false, session_id, h->userdata);
}

static void node_on_connect_error(const char* host, uint16_t port,
                                   const char* path, const char* hostname,
                                   eebus_err_t err, void* userdata)
{
  (void)userdata; (void)hostname; (void)path;
  printf("[SHIP] Connect error: %s:%u err=%s\n", host, port, eebus_err_str(err));
}

static void node_on_peer_discovered(
    const ship_peer_info_t* peer,
    void*                   userdata)
{
  ship_helper_t* h = (ship_helper_t*)userdata;
  trust_mgr_on_discovered(h->trust, peer);
  if (h->on_peer_discovered)
    h->on_peer_discovered(peer, h->userdata);
}

static void node_on_peer_lost(
    const ship_peer_info_t* peer,
    void*                   userdata)
{
  ship_helper_t* h = (ship_helper_t*)userdata;
  if (h->on_peer_lost)
    h->on_peer_lost(peer, h->userdata);
}

static void node_on_pairing_requested(const char* peer_ski, void* userdata) {
  ship_helper_t* h = (ship_helper_t*)userdata;
  trust_mgr_on_pairing_requested(h->trust, peer_ski);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

eebus_err_t ship_helper_create(
    ship_helper_t**            out,
    const ship_helper_config_t* cfg)
{
  if (!out || !cfg || !cfg->name) return EEBUS_ERR_INVALID;

  ship_helper_t* h = EEBUS_CALLOC(1, sizeof(*h));
  if (!h) return EEBUS_ERR_NO_MEMORY;

  h->on_data             = cfg->on_data;
  h->on_conn             = cfg->on_conn;
  h->on_peer_discovered  = cfg->on_peer_discovered;
  h->on_peer_lost        = cfg->on_peer_lost;
  h->userdata            = cfg->userdata;

  /* Load or generate TLS credentials */
  eebus_err_t rc = tls_persist_load(cfg->name, &h->cred);
  if (rc != EEBUS_OK) {
    EEBUS_FREE(h);
    return rc;
  }
  memcpy(h->ski, h->cred.ski, SHIP_SKI_STR_LEN);

  /* Build ship_node config */
  ship_node_config_t node_cfg;
  memset(&node_cfg, 0, sizeof(node_cfg));
  snprintf(node_cfg.instance_name, sizeof(node_cfg.instance_name),
           "EEBus-%s", cfg->name);
  node_cfg.port          = cfg->port;
  node_cfg.register_flag = cfg->register_flag;
  node_cfg.cert_path     = h->cred.cert_path;
  node_cfg.key_path      = h->cred.key_path;

  if (cfg->brand)   snprintf(node_cfg.brand,   sizeof(node_cfg.brand),   "%s", cfg->brand);
  if (cfg->type)    snprintf(node_cfg.type,    sizeof(node_cfg.type),    "%s", cfg->type);
  if (cfg->model)   snprintf(node_cfg.model,   sizeof(node_cfg.model),   "%s", cfg->model);
  if (cfg->ship_id) snprintf(node_cfg.ship_id, sizeof(node_cfg.ship_id), "%s", cfg->ship_id);

  node_cfg.callbacks.on_spine_data       = node_on_spine_data;
  node_cfg.callbacks.on_connected        = node_on_connected;
  node_cfg.callbacks.on_disconnected     = node_on_disconnected;
  node_cfg.callbacks.on_connect_error    = node_on_connect_error;
  node_cfg.callbacks.on_peer_discovered  = node_on_peer_discovered;
  node_cfg.callbacks.on_peer_lost        = node_on_peer_lost;
  node_cfg.callbacks.on_pairing_requested = node_on_pairing_requested;
  node_cfg.callbacks.userdata            = h;

  rc = ship_node_create(&h->node, &node_cfg);
  if (rc != EEBUS_OK) {
    ship_tls_cred_free(&h->cred);
    EEBUS_FREE(h);
    return rc;
  }

  /* Create trust manager */
  h->trust = trust_mgr_create(
      cfg->name, h->node,
      cfg->trust_mode,
      cfg->trust_prompt,
      cfg->userdata);
  if (!h->trust) {
    ship_node_destroy(h->node);
    ship_tls_cred_free(&h->cred);
    EEBUS_FREE(h);
    return EEBUS_ERR_NO_MEMORY;
  }

  *out = h;
  return EEBUS_OK;
}

eebus_err_t ship_helper_start(ship_helper_t* h) {
  if (!h) return EEBUS_ERR_INVALID;

  /* Load previously trusted SKIs from disk */
  trust_mgr_load_trusted(h->trust);

  eebus_err_t rc = ship_node_start(h->node);
  if (rc != EEBUS_OK) return rc;

  printf("[SHIP] Node started — SKI: %s  port: listening\n", h->ski);
  return EEBUS_OK;
}

void ship_helper_stop(ship_helper_t* h) {
  if (h && h->node) ship_node_stop(h->node);
}

void ship_helper_destroy(ship_helper_t* h) {
  if (!h) return;
  ship_node_stop(h->node);
  ship_node_destroy(h->node);
  trust_mgr_destroy(h->trust);
  ship_tls_cred_free(&h->cred);
  EEBUS_FREE(h);
}

eebus_err_t ship_helper_connect(
    ship_helper_t* h,
    const char*    ip,
    uint16_t       port)
{
  if (!h || !ip) return EEBUS_ERR_INVALID;

  /* For manual connect we don't know the SKI yet — pass NULL.
   * ship_node will authenticate via mTLS and call on_peer_discovered. */
  eebus_err_t rc = ship_node_connect_peer(h->node, ip, port, NULL, NULL);
  if (rc != EEBUS_OK) {
    fprintf(stderr, "[SHIP] Connect to %s:%u failed: %s\n",
            ip, port, eebus_err_str(rc));
  }
  return rc;
}

eebus_err_t ship_helper_send(
    ship_helper_t* h,
    const char*    session_id,
    const char*    json)
{
  if (!h || !session_id || !json) return EEBUS_ERR_INVALID;
  return ship_node_send_spine(h->node, session_id, json);
}

ship_node_t* ship_helper_get_node(ship_helper_t* h) {
  return h ? h->node : NULL;
}

trust_mgr_t* ship_helper_get_trust(ship_helper_t* h) {
  return h ? h->trust : NULL;
}

const char* ship_helper_get_ski(const ship_helper_t* h) {
  return h ? h->ski : NULL;
}
