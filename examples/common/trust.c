// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/trust.c
 */

#include "trust.h"
#include "tls_persist.h"
#include "eebus_memory.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ── Internal state ──────────────────────────────────────────────────────── */

struct trust_mgr {
  char            demo_name[64];
  ship_node_t*    node;
  trust_mode_t    mode;
  trust_prompt_fn prompt_fn;
  void*           userdata;
};

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/** Build path to the trusted/ directory. */
static eebus_err_t trusted_dir(const trust_mgr_t* mgr, char* buf, size_t sz) {
  char base[490];
  eebus_err_t rc = tls_persist_dir(mgr->demo_name, base, sizeof(base));
  if (rc != EEBUS_OK) return rc;
  int n = snprintf(buf, sz, "%s/trusted", base);
  if (n < 0 || (size_t)n >= sz) return EEBUS_ERR_IO;
  return EEBUS_OK;
}

/** Build path to a specific SKI file inside trusted/. */
static eebus_err_t ski_file_path(
    const trust_mgr_t* mgr,
    const char* ski,
    char* buf, size_t sz)
{
  char dir[512];
  eebus_err_t rc = trusted_dir(mgr, dir, sizeof(dir));
  if (rc != EEBUS_OK) return rc;
  int n = snprintf(buf, sz, "%s/%s.txt", dir, ski);
  if (n < 0 || (size_t)n >= sz) return EEBUS_ERR_IO;
  return EEBUS_OK;
}

/** Ensure the trusted/ directory exists. */
static eebus_err_t ensure_trusted_dir(const trust_mgr_t* mgr) {
  char dir[512];
  eebus_err_t rc = trusted_dir(mgr, dir, sizeof(dir));
  if (rc != EEBUS_OK) return rc;
  if (mkdir(dir, 0700) != 0 && errno != EEXIST) return EEBUS_ERR_IO;
  return EEBUS_OK;
}

/** Persist a trusted SKI to disk. */
static eebus_err_t persist_ski(const trust_mgr_t* mgr, const char* ski) {
  eebus_err_t rc = ensure_trusted_dir(mgr);
  if (rc != EEBUS_OK) return rc;

  char path[600];
  rc = ski_file_path(mgr, ski, path, sizeof(path));
  if (rc != EEBUS_OK) return rc;

  FILE* f = fopen(path, "w");
  if (!f) return EEBUS_ERR_IO;
  fprintf(f, "ski=%s\n", ski);
  fclose(f);
  return EEBUS_OK;
}

/** Remove a SKI file from disk. */
static void remove_ski_file(const trust_mgr_t* mgr, const char* ski) {
  char path[600];
  if (ski_file_path(mgr, ski, path, sizeof(path)) == EEBUS_OK) {
    remove(path);
  }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

trust_mgr_t* trust_mgr_create(
    const char*    demo_name,
    ship_node_t*   node,
    trust_mode_t   mode,
    trust_prompt_fn prompt_fn,
    void*          userdata)
{
  if (!demo_name || !node) return NULL;

  trust_mgr_t* mgr = EEBUS_CALLOC(1, sizeof(*mgr));
  if (!mgr) return NULL;

  snprintf(mgr->demo_name, sizeof(mgr->demo_name), "%s", demo_name);
  mgr->node      = node;
  mgr->mode      = mode;
  mgr->prompt_fn = prompt_fn;
  mgr->userdata  = userdata;
  return mgr;
}

void trust_mgr_destroy(trust_mgr_t* mgr) {
  EEBUS_FREE(mgr);
}

void trust_mgr_set_mode(trust_mgr_t* mgr, trust_mode_t mode) {
  if (!mgr) return;
  mgr->mode = mode;
  printf("[TRUST] Mode: %s\n",
         mode == TRUST_MODE_AUTO ? "auto" : "manual");
}

trust_mode_t trust_mgr_get_mode(const trust_mgr_t* mgr) {
  return mgr ? mgr->mode : TRUST_MODE_MANUAL;
}

/* Print every field of ship_peer_info_t; unknown strings render as "-". */
static void print_peer_info(const ship_peer_info_t* peer) {
  printf("  Name        : %s\n", peer->instance_name[0] ? peer->instance_name : "(unknown)");
  printf("  ID          : %s\n", peer->id[0]            ? peer->id            : "-");
  printf("  SKI         : %s\n", peer->ski);
  printf("  Host        : %s\n", peer->host[0]          ? peer->host          : "-");
  printf("  Hostname    : %s\n", peer->hostname[0]      ? peer->hostname      : "-");
  printf("  Port        : %u\n", peer->port);
  printf("  Path        : %s\n", peer->path[0]          ? peer->path          : "-");
  printf("  TXT version : %u\n", peer->txtvers);
  printf("  Register    : %s\n", peer->register_flag ? "true" : "false");
  printf("  Brand       : %s\n", peer->brand[0]         ? peer->brand         : "-");
  printf("  Model       : %s\n", peer->model[0]         ? peer->model         : "-");
  printf("  Type        : %s\n", peer->type[0]          ? peer->type          : "-");
}

void trust_mgr_on_discovered(
    trust_mgr_t*            mgr,
    const ship_peer_info_t* peer)
{
  if (!mgr || !peer || !peer->ski[0]) return;

  /* Already trusted → nothing to do */
  if (ship_node_is_trusted(mgr->node, peer->ski)) return;

  if (mgr->mode == TRUST_MODE_AUTO) {
    printf("[TRUST] Auto-trusting %s (%s)\n",
           peer->instance_name[0] ? peer->instance_name : "device",
           peer->ski);
    trust_mgr_accept(mgr, peer->ski);
    return;
  }

  /* Manual mode: fire the prompt callback if registered, otherwise print
   * the full peer info as the built-in default prompt. */
  if (mgr->prompt_fn) {
    mgr->prompt_fn(peer, mgr->userdata);
  } else {
    printf("\n[TRUST] New device discovered:\n");
    print_peer_info(peer);
    printf("  Run: trust accept %s\n\n", peer->ski);
  }
}

void trust_mgr_on_pairing_requested(trust_mgr_t* mgr, const char* ski) {
  if (!mgr || !ski) return;

  /* Already trusted (e.g. race between mDNS and inbound connect) */
  if (ship_node_is_trusted(mgr->node, ski)) return;

  if (mgr->mode == TRUST_MODE_AUTO) {
    printf("[TRUST] Auto-trusting inbound device (%s)\n", ski);
    trust_mgr_accept(mgr, ski);
    return;
  }

  /* Manual mode: only SKI is known for inbound. Build a minimal peer_info
   * and either call the registered prompt or print the built-in default. */
  ship_peer_info_t info;
  memset(&info, 0, sizeof(info));
  snprintf(info.ski, sizeof(info.ski), "%s", ski);

  if (mgr->prompt_fn) {
    mgr->prompt_fn(&info, mgr->userdata);
  } else {
    printf("\n[TRUST] Inbound pairing request:\n");
    print_peer_info(&info);
    printf("  (SHIP §12.3.1.2: verify this SKI matches the remote device)\n");
    printf("  Run: trust accept %s\n", ski);
    printf("       trust reject %s\n\n", ski);
  }
}

eebus_err_t trust_mgr_accept(trust_mgr_t* mgr, const char* ski) {
  if (!mgr || !ski) return EEBUS_ERR_INVALID;

  eebus_err_t rc = ship_node_trust_ski(mgr->node, ski);
  if (rc != EEBUS_OK && rc != EEBUS_ERR_EXISTS) return rc;

  rc = persist_ski(mgr, ski);
  if (rc != EEBUS_OK) {
    fprintf(stderr, "[TRUST] Warning: could not persist SKI to disk\n");
  }

  printf("[TRUST] Trusted: %s\n", ski);
  return EEBUS_OK;
}

void trust_mgr_reject(trust_mgr_t* mgr, const char* ski) {
  if (!mgr || !ski) return;
  ship_node_reject_ski(mgr->node, ski);
  printf("[TRUST] Rejected: %s\n", ski);
}

eebus_err_t trust_mgr_revoke(trust_mgr_t* mgr, const char* ski) {
  if (!mgr || !ski) return EEBUS_ERR_INVALID;

  eebus_err_t rc = ship_node_revoke_ski(mgr->node, ski);
  if (rc != EEBUS_OK) return rc;

  remove_ski_file(mgr, ski);
  printf("[TRUST] Revoked: %s\n", ski);
  return EEBUS_OK;
}

eebus_err_t trust_mgr_load_trusted(trust_mgr_t* mgr) {
  if (!mgr) return EEBUS_ERR_INVALID;

  char dir[512];
  eebus_err_t rc = trusted_dir(mgr, dir, sizeof(dir));
  if (rc != EEBUS_OK) return rc;

  DIR* d = opendir(dir);
  if (!d) return EEBUS_OK;  /* Directory doesn't exist yet — not an error */

  int count = 0;
  struct dirent* entry;
  while ((entry = readdir(d)) != NULL) {
    /* Each file is named <ski>.txt (40 hex chars + ".txt" = 44 chars) */
    size_t len = strlen(entry->d_name);
    if (len != 44 || strcmp(entry->d_name + 40, ".txt") != 0) continue;

    /* Extract SKI (first 40 chars of filename) */
    char ski[41];
    memcpy(ski, entry->d_name, 40);
    ski[40] = '\0';

    rc = ship_node_trust_ski(mgr->node, ski);
    if (rc == EEBUS_OK || rc == EEBUS_ERR_EXISTS) count++;
  }
  closedir(d);

  if (count > 0) {
    printf("[TRUST] Loaded %d trusted SKI(s) from disk\n", count);
  }
  return EEBUS_OK;
}

void trust_mgr_print_list(const trust_mgr_t* mgr) {
  if (!mgr) return;

  char dir[512];
  if (trusted_dir(mgr, dir, sizeof(dir)) != EEBUS_OK) return;

  DIR* d = opendir(dir);
  if (!d) {
    printf("  (no trusted devices)\n");
    return;
  }

  printf("Trusted devices:\n");
  int count = 0;
  struct dirent* entry;
  while ((entry = readdir(d)) != NULL) {
    size_t len = strlen(entry->d_name);
    if (len != 44 || strcmp(entry->d_name + 40, ".txt") != 0) continue;

    char ski[41];
    memcpy(ski, entry->d_name, 40);
    ski[40] = '\0';
    printf("  SKI: %s\n", ski);
    count++;
  }
  closedir(d);

  if (count == 0) printf("  (no trusted devices)\n");
  else            printf("\n  %d trusted SKI(s)\n", count);
}

bool trust_mgr_is_trusted(const trust_mgr_t* mgr, const char* ski) {
  if (!mgr || !ski || !ski[0]) return false;
  return ship_node_is_trusted(mgr->node, ski);
}
