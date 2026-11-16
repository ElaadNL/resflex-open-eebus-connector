// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/device_table.c
 *
 * Shared printers for the demo device table:
 *  - dashboard table (uses dashboard_printf, called from render fns)
 *  - one-shot detail view (uses plain printf, called from `device <id>`)
 */

#include "device_table.h"
#include "dashboard.h"
#include "trust.h"
#include <math.h>
#include <stdio.h>

/* Column widths kept in one place so header and rows stay aligned. */
#define COL_ID         26
#define COL_BRAND      12
#define COL_MODEL      16
#define COL_SKI        41
#define COL_MDNS        5
#define COL_TRUST       5
#define COL_CONNECTED   9
#define COL_HOST       36
#define COL_PORT        5

void dev_print_table_header(void) {
    dashboard_printf("  %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s\n",
                     COL_ID,        "SHIP-Id",
                     COL_BRAND,     "Brand",
                     COL_MODEL,     "Model",
                     COL_SKI,       "SKI",
                     COL_MDNS,      "mDNS",
                     COL_TRUST,     "Trust",
                     COL_CONNECTED, "Connected",
                     COL_HOST,      "Host",
                     COL_PORT,      "Port");
    dashboard_separator();
}

void dev_print_table_row(const demo_device_t* d, struct trust_mgr* trust) {
    if (!d || !d->active) return;
    bool trusted = trust && d->mdns.info.ski[0]
                   ? trust_mgr_is_trusted(trust, d->mdns.info.ski) : false;
    const char* host = d->mdns.info.hostname[0] ? d->mdns.info.hostname
                     : d->mdns.info.host[0]     ? d->mdns.info.host
                     : "-";
    char port_buf[16];
    if (d->mdns.info.port) snprintf(port_buf, sizeof(port_buf), "%u", d->mdns.info.port);
    else                   snprintf(port_buf, sizeof(port_buf), "-");

    dashboard_printf("  %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s\n",
                     COL_ID,        d->mdns.info.id[0]    ? d->mdns.info.id    : "-",
                     COL_BRAND,     d->mdns.info.brand[0] ? d->mdns.info.brand : "-",
                     COL_MODEL,     d->mdns.info.model[0] ? d->mdns.info.model : "-",
                     COL_SKI,       d->mdns.info.ski[0]   ? d->mdns.info.ski   : "-",
                     COL_MDNS,      d->mdns.online        ? "on"  : "off",
                     COL_TRUST,     trusted               ? "Yes" : "No",
                     COL_CONNECTED, d->ship.connected     ? "Yes" : "No",
                     COL_HOST,      host,
                     COL_PORT,      port_buf);
}

static const char* yes_no(bool b) { return b ? "Yes" : "No"; }

static void print_uc_section(const char* label, const dev_uc_t* uc) {
    printf("%s:\n", label);
    printf("  Support      : %s\n", yes_no(uc->support));
    if (!uc->support) return;
    printf("  Available    : %s\n", yes_no(uc->available));
    printf("  Actor        : %s\n", uc->actor[0]       ? uc->actor       : "-");
    printf("  Entity type  : %s\n", uc->entity_type[0] ? uc->entity_type : "-");
    printf("  Priority     : %d\n", uc->priority);
}

void dev_print_detail(const demo_device_t* d, struct trust_mgr* trust) {
    if (!d || !d->active) { printf("(inactive)\n"); return; }
    bool trusted = trust && d->mdns.info.ski[0]
                   ? trust_mgr_is_trusted(trust, d->mdns.info.ski) : false;

    printf("Device: %s\n", dev_display_name(d));
    printf("================================================================\n");
    printf("mDNS:\n");
    printf("  Online       : %s\n", yes_no(d->mdns.online));
    printf("  Instance     : %s\n", d->mdns.info.instance_name[0] ? d->mdns.info.instance_name : "-");
    printf("  ID           : %s\n", d->mdns.info.id[0]            ? d->mdns.info.id            : "-");
    printf("  SKI          : %s\n", d->mdns.info.ski[0]           ? d->mdns.info.ski           : "-");
    printf("  Host         : %s\n", d->mdns.info.host[0]          ? d->mdns.info.host          : "-");
    printf("  Hostname     : %s\n", d->mdns.info.hostname[0]      ? d->mdns.info.hostname      : "-");
    printf("  Port         : %u\n", d->mdns.info.port);
    printf("  Path         : %s\n", d->mdns.info.path[0]          ? d->mdns.info.path          : "-");
    printf("  TXT version  : %u\n", d->mdns.info.txtvers);
    printf("  Register     : %s\n", yes_no(d->mdns.info.register_flag));
    printf("  Brand        : %s\n", d->mdns.info.brand[0]         ? d->mdns.info.brand         : "-");
    printf("  Model        : %s\n", d->mdns.info.model[0]         ? d->mdns.info.model         : "-");
    printf("  Type         : %s\n", d->mdns.info.type[0]          ? d->mdns.info.type          : "-");
    printf("\n");
    printf("SHIP:\n");
    printf("  Trusted      : %s\n", yes_no(trusted));
    printf("  Connected    : %s\n", yes_no(d->ship.connected));
    printf("  Session ID   : %s\n", d->ship.session_id[0] ? d->ship.session_id : "-");
    printf("\n");
    print_uc_section("LPC", &d->lpc); printf("\n");
    print_uc_section("LPP", &d->lpp); printf("\n");
    print_uc_section("MPC", &d->mpc);
    printf("================================================================\n");
}

/* ── LC downstream table (lpc/lpp show) ──────────────────────────────────── */

#define COL_ENTITY    20
#define COL_EC        10
#define COL_AVAIL      7
#define COL_PRIO       6
#define COL_LIMIT     10
#define COL_ACTIVE     8
#define COL_ENDTIME   22
#define COL_FAILSAFE  16

/* 9 columns + 8 inter-column spaces + 2-char prefix */
#define LC_TABLE_WIDTH  (COL_ID + COL_ENTITY + COL_EC + COL_AVAIL + COL_PRIO + \
                          COL_LIMIT + COL_ACTIVE + COL_ENDTIME + COL_FAILSAFE \
                          + 8 + 2)

static const char* ec_type_short(lc_power_max_type_t t) {
    switch (t) {
    case LC_POWER_MAX_NOMINAL:     return "N";
    case LC_POWER_MAX_CONTRACTUAL: return "C";
    default:                       return "-";
    }
}

static void lc_print_line(char ch) {
    putchar(' '); putchar(' ');
    for (int i = 0; i < LC_TABLE_WIDTH - 2; i++) putchar(ch);
    putchar('\n');
}

void dev_print_lc_header(void) {
    lc_print_line('=');
    printf("  %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s %-*s\n",
           COL_ID,       "SHIP-Id",
           COL_ENTITY,   "Entity",
           COL_EC,       "EC",
           COL_AVAIL,    "Avail",
           COL_PRIO,     "Prio",
           COL_LIMIT,    "Limit",
           COL_ACTIVE,   "Active",
           COL_ENDTIME,  "endTime",
           COL_FAILSAFE, "Failsafe");
    lc_print_line('-');
}

void dev_print_lc_footer(void) {
    lc_print_line('=');
}

void dev_print_lc_row(const demo_device_t* d, bool is_lpc,
                       const uc_lc_limit_t* lim,
                       const uc_lc_failsafe_t* fs,
                       const uc_lc_ec_characteristic_t* ec) {
    if (!d || !d->active || !lim || !fs) return;
    const dev_uc_t* uc = is_lpc ? &d->lpc : &d->lpp;
    char lim_buf[16], fs_buf[24], ec_buf[16];
    snprintf(lim_buf, sizeof(lim_buf), "%.0f W", fabs(lim->value_w));
    snprintf(fs_buf, sizeof(fs_buf), "%.0fW / %us",
             fs->power_limit_w, fs->duration_s);
    if (ec && ec->type != LC_POWER_MAX_NONE && ec->value_w > 0.0)
        snprintf(ec_buf, sizeof(ec_buf), "%.0fW %s",
                 fabs(ec->value_w), ec_type_short(ec->type));
    else
        snprintf(ec_buf, sizeof(ec_buf), "-");

    printf("  %-*s %-*s %-*s %-*s %-*d %-*s %-*s %-*s %-*s\n",
           COL_ID,       d->mdns.info.id[0]    ? d->mdns.info.id    : "-",
           COL_ENTITY,   uc->entity_type[0]    ? uc->entity_type    : "-",
           COL_EC,       ec_buf,
           COL_AVAIL,    uc->available ? "Yes" : "No",
           COL_PRIO,     uc->priority,
           COL_LIMIT,    lim_buf,
           COL_ACTIVE,   lim->is_limit_active ? "Yes" : "No",
           COL_ENDTIME,  lim->time_period.end_time_present ? lim->time_period.end_time : "-",
           COL_FAILSAFE, fs_buf);
}
