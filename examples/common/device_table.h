// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/device_table.h
 *
 * Unified device table for EEBus demo programs.
 * Tracks device lifecycle: mDNS discovery → SHIP connection → UC readiness.
 */

#ifndef EEBUS_DEMO_DEVICE_TABLE_H_
#define EEBUS_DEMO_DEVICE_TABLE_H_

#include "ship_node.h"
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMO_MAX_DEVICES 32

/* ── mDNS layer ──────────────────────────────────────────────────────────── */

typedef struct {
    ship_peer_info_t info;
    bool             online;
} dev_mdns_t;

/* ── SHIP layer ──────────────────────────────────────────────────────────── */

typedef struct {
    bool connected;
    char session_id[64];
} dev_ship_t;

/* ── UC layer (generic, used for lpc/lpp/mpc) ────────────────────────────── */

typedef struct {
    bool support;
    bool available;
    char actor[32];        /* "ControllableSystem" / "EnergyGuard" / "MonitoredUnit" / "MonitoringAppliance" */
    char entity_type[64];  /* "EVSE" / "CEM" / "GridGuard" / "HeatPumpAppliance" / "Inverter" */
    int  priority;         /* per-direction allocation priority (1 = highest) */
} dev_uc_t;

/* ── Device entry ────────────────────────────────────────────────────────── */

typedef struct {
    bool       active;
    dev_mdns_t mdns;
    dev_ship_t ship;
    dev_uc_t   lpc;
    dev_uc_t   lpp;
    dev_uc_t   mpc;
} demo_device_t;

/* ── Lookup functions ────────────────────────────────────────────────────── */

static inline demo_device_t* dev_find_by_id(demo_device_t* tbl, const char* id) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < DEMO_MAX_DEVICES; i++)
        if (tbl[i].active && tbl[i].mdns.info.id[0]
            && strcmp(tbl[i].mdns.info.id, id) == 0)
            return &tbl[i];
    return NULL;
}

static inline demo_device_t* dev_find_by_ski(demo_device_t* tbl, const char* ski) {
    if (!ski || !ski[0]) return NULL;
    for (int i = 0; i < DEMO_MAX_DEVICES; i++)
        if (tbl[i].active && strcmp(tbl[i].mdns.info.ski, ski) == 0)
            return &tbl[i];
    return NULL;
}

static inline demo_device_t* dev_find_by_session(demo_device_t* tbl, const char* sid) {
    if (!sid || !sid[0]) return NULL;
    for (int i = 0; i < DEMO_MAX_DEVICES; i++)
        if (tbl[i].active && tbl[i].ship.session_id[0]
            && strcmp(tbl[i].ship.session_id, sid) == 0)
            return &tbl[i];
    return NULL;
}

static inline demo_device_t* dev_alloc(demo_device_t* tbl) {
    for (int i = 0; i < DEMO_MAX_DEVICES; i++)
        if (!tbl[i].active) return &tbl[i];
    return NULL;
}

/* ── Display name helper ─────────────────────────────────────────────────── */

static inline const char* dev_display_name(const demo_device_t* d) {
    if (d->mdns.info.id[0])            return d->mdns.info.id;
    if (d->mdns.info.instance_name[0]) return d->mdns.info.instance_name;
    if (d->mdns.info.ski[0])           return d->mdns.info.ski;
    return "(unknown)";
}

/* ── Print helpers (defined in device_table.c) ───────────────────────────── */

struct trust_mgr;  /* forward declaration; defined in trust.h */

/** Print the dashboard table header (uses dashboard_printf). */
void dev_print_table_header(void);

/** Print one row in the dashboard table (uses dashboard_printf).
 *  Pass trust=NULL to skip the Trust column live query. */
void dev_print_table_row(const demo_device_t* d, struct trust_mgr* trust);

/** Print all fields of a device organised by section (uses plain printf).
 *  Used by the `device <ship-id>` CLI command. */
void dev_print_detail(const demo_device_t* d, struct trust_mgr* trust);

/* ── LC downstream table (for lpc/lpp show) ──────────────────────────────── */

#include "uc_lc.h"  /* uc_lc_limit_t / uc_lc_failsafe_t */

/** Top border + header row + middle separator for the lpc/lpp show
 *  downstream-CS table (uses plain printf). */
void dev_print_lc_header(void);

/** One row of the lpc/lpp show downstream-CS table (uses plain printf).
 *  Caller pre-fetches limit / failsafe / ec from the EG cache and passes
 *  them in. ec may be NULL or have type=LC_POWER_MAX_NONE to render '-'. */
void dev_print_lc_row(const demo_device_t* d, bool is_lpc,
                       const uc_lc_limit_t* lim,
                       const uc_lc_failsafe_t* fs,
                       const uc_lc_ec_characteristic_t* ec);

/** Bottom border for the lpc/lpp show downstream-CS table. */
void dev_print_lc_footer(void);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_DEVICE_TABLE_H_ */
