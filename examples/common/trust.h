// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/trust.h
 *
 * Trust management for EEBus demo programs.
 *
 * Wraps ship_node_trust_ski() / ship_node_revoke_ski() with:
 *   - File persistence  (~/.eebus/<n>/trusted/<ski>.txt)
 *   - Auto / manual trust modes
 *   - New-device prompt callback (manual mode)
 */

#ifndef EEBUS_DEMO_TRUST_H_
#define EEBUS_DEMO_TRUST_H_

#include <stdbool.h>
#include "eebus_error.h"
#include "ship_node.h"   /* ship_node_t, SHIP_SKI_STR_LEN */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Trust mode ──────────────────────────────────────────────────────────── */

typedef enum {
  TRUST_MODE_MANUAL = 0, /**< Prompt user to confirm each new SKI. */
  TRUST_MODE_AUTO   = 1, /**< Accept every discovered SKI automatically. */
} trust_mode_t;

/* ── Trust manager ───────────────────────────────────────────────────────── */

typedef struct trust_mgr trust_mgr_t;

/**
 * @brief Called (in MANUAL mode) when a new peer is discovered.
 *
 * The callback should prompt the user and call trust_mgr_accept() or
 * trust_mgr_reject() with the given SKI.
 *
 * @param peer       Complete peer information from mDNS discovery.
 * @param userdata   Value passed to trust_mgr_create().
 */
typedef void (*trust_prompt_fn)(
    const ship_peer_info_t* peer,
    void*                   userdata);

/**
 * @brief Create a trust manager.
 *
 * @param demo_name  Demo program name (used for ~/.eebus/<demo_name>/trusted/).
 * @param node       The ship_node_t to call trust_ski / revoke_ski on.
 * @param mode       Initial trust mode.
 * @param prompt_fn  Callback for manual-mode prompts (may be NULL if AUTO).
 * @param userdata   Forwarded to prompt_fn.
 * @return New trust_mgr_t, or NULL on allocation failure.
 */
trust_mgr_t* trust_mgr_create(
    const char*    demo_name,
    ship_node_t*   node,
    trust_mode_t   mode,
    trust_prompt_fn prompt_fn,
    void*          userdata);

/** Free a trust manager (does not destroy the ship_node). */
void trust_mgr_destroy(trust_mgr_t* mgr);

/* ── Configuration ───────────────────────────────────────────────────────── */

/** Change the trust mode at runtime. */
void trust_mgr_set_mode(trust_mgr_t* mgr, trust_mode_t mode);

/** Return the current trust mode. */
trust_mode_t trust_mgr_get_mode(const trust_mgr_t* mgr);

/* ── Discovery hook ──────────────────────────────────────────────────────── */

/**
 * @brief Call this from the ship_node on_peer_discovered callback.
 *
 * In AUTO mode: immediately calls ship_node_trust_ski() and persists.
 * In MANUAL mode: calls prompt_fn; the user must then call
 *                 trust_mgr_accept() or trust_mgr_reject().
 */
void trust_mgr_on_discovered(
    trust_mgr_t*            mgr,
    const ship_peer_info_t* peer);

/**
 * @brief Call this from the ship_node on_pairing_requested callback.
 *
 * Handles an inbound connection from an unknown peer waiting in Hello
 * PENDING state (SHIP §12.3.1.2).
 *
 * In AUTO mode: immediately calls trust_mgr_accept().
 * In MANUAL mode: calls prompt_fn (name and address are NULL for inbound
 *                 connections — only the SKI is known at this point).
 *                 The user must then call trust_mgr_accept() or
 *                 trust_mgr_reject() with the given ski.
 */
void trust_mgr_on_pairing_requested(
    trust_mgr_t* mgr,
    const char*  ski);

/* ── Manual accept / reject ──────────────────────────────────────────────── */

/**
 * @brief Accept a pending SKI (called from the prompt callback or CLI).
 *
 * Calls ship_node_trust_ski() and writes ~/.eebus/<n>/trusted/<ski>.txt.
 */
eebus_err_t trust_mgr_accept(trust_mgr_t* mgr, const char* ski);

/**
 * @brief Reject a pending inbound SKI.
 *
 * Calls ship_node_reject_ski() to send Hello{aborted} immediately,
 * without waiting for the Hello timeout.
 */
void trust_mgr_reject(trust_mgr_t* mgr, const char* ski);

/* ── Revoke ──────────────────────────────────────────────────────────────── */

/**
 * @brief Revoke trust for a SKI.
 *
 * Calls ship_node_revoke_ski() and removes the file from trusted/.
 */
eebus_err_t trust_mgr_revoke(trust_mgr_t* mgr, const char* ski);

/* ── Persistence ─────────────────────────────────────────────────────────── */

/**
 * @brief Load all previously trusted SKIs from disk into ship_node.
 *
 * Call once after trust_mgr_create() and ship_node_start().
 */
eebus_err_t trust_mgr_load_trusted(trust_mgr_t* mgr);

/* ── Query ───────────────────────────────────────────────────────────────── */

/**
 * @brief Print the trusted SKI list to stdout (for the `trust list` command).
 */
void trust_mgr_print_list(const trust_mgr_t* mgr);

/**
 * @brief Check whether a SKI is currently trusted.
 *
 * Authoritative query against the underlying ship_node trust state.
 */
bool trust_mgr_is_trusted(const trust_mgr_t* mgr, const char* ski);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_TRUST_H_ */
