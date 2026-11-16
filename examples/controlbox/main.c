// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/controlbox/main.c
 *
 * EEBus Control Box Demo
 *
 * Roles:
 *   LC EG (LPC+LPP) — sends consumption/production limits to downstream
 */

#define _POSIX_C_SOURCE 200809L

#include "eebus_mutex.h"
#include "eebus_thread.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "eebus_error.h"
#include "eebus_log.h"

#include "spine_device.h"
#include "spine_router.h"
#include "uc_lpc_lpp_eg.h"
#include "spine_codec_address.h"

#include "cli.h"
#include "dashboard.h"
#include "ship_helper.h"
#include "device_table.h"

#define CB_DEFAULT_PORT  7711
#define CB_DEMO_NAME     "controlbox-01"

/* Fallback initial active-limit power for on_cs_ready when the CS
 * exposes neither EC characteristic nor failsafe power. */
#define CB_INITIAL_DEFAULT_LPC_W   11000.0   /* §14a Steuerbare Verbrauchseinrichtung */
#define CB_INITIAL_DEFAULT_LPP_W   22000.0   /* 3-phase 32A typical feed-in cap */

typedef struct {
    ship_helper_t*     ship;
    spine_device_t*    device;
    spine_router_t*    router;

    uc_lpc_lpp_eg_t*   lc_eg;

    cli_t*             cli;
    dashboard_t*       dashboard;

    demo_device_t      devices[DEMO_MAX_DEVICES];
    eebus_mutex_t*     mu;
    volatile int       quit;
} cb_app_t;

/* ── SPINE/SHIP glue ────────────────────────────────────────────────────── */

static void spine_send(const char* session_id, const char* json, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    ship_helper_send(app->ship, session_id, json);
}

static void on_ship_data(const char* session_id, const char* peer_ski,
                          const char* json, void* ud) {
    (void)peer_ski;
    spine_router_dispatch(((cb_app_t*)ud)->router, session_id, json);
}

/* ── Device table callbacks ─────────────────────────────────────────────── */

static void on_peer_discovered(const ship_peer_info_t* peer, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    eebus_mutex_lock(app->mu);
    demo_device_t* d = dev_find_by_id(app->devices, peer->id);
    /* Fallback for SHIP-first ordering: peer connected via TLS before
     * announcing via mDNS, so an entry exists with only SKI populated. */
    if (!d) d = dev_find_by_ski(app->devices, peer->ski);
    if (!d) {
        d = dev_alloc(app->devices);
        if (!d) { eebus_mutex_unlock(app->mu); return; }
        memset(d, 0, sizeof(*d));
        d->active = true;
    }
    d->mdns.info = *peer;
    d->mdns.online = true;
    eebus_mutex_unlock(app->mu);
    cli_print(app->cli, "\n[CB] Discovered: %s (%s)\n", peer->instance_name, peer->ski);
}

static void on_peer_lost(const ship_peer_info_t* peer, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    eebus_mutex_lock(app->mu);
    demo_device_t* d = dev_find_by_id(app->devices, peer->id);
    if (d) d->mdns.online = false;
    eebus_mutex_unlock(app->mu);
}

static void on_ship_conn(const char* peer_ski, bool connected,
                         const char* session_id, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    if (connected) {
        spine_router_on_connect(app->router, peer_ski, session_id);
        eebus_mutex_lock(app->mu);
        demo_device_t* d = dev_find_by_ski(app->devices, peer_ski);
        if (!d) {
            d = dev_alloc(app->devices);
            if (d) {
                memset(d, 0, sizeof(*d));
                d->active = true;
                snprintf(d->mdns.info.ski, sizeof(d->mdns.info.ski), "%s", peer_ski);
            }
        }
        if (d) {
            d->ship.connected = true;
            snprintf(d->ship.session_id, sizeof(d->ship.session_id), "%s", session_id);
        }
        eebus_mutex_unlock(app->mu);
        cli_print(app->cli, "\n[CB] Connected: %s\n", peer_ski);
    } else {
        eebus_mutex_lock(app->mu);
        demo_device_t* d = dev_find_by_session(app->devices, session_id);
        if (d) {
            d->ship.connected = false;
            d->ship.session_id[0] = '\0';
            d->lpc.available = false;
            d->lpp.available = false;
            d->mpc.available = false;
        }
        eebus_mutex_unlock(app->mu);
        cli_print(app->cli, "\n[CB] Disconnected: %s\n", peer_ski);
        spine_router_on_disconnect(app->router, session_id);
    }
}

/* ── LC EG: CS ready ─────────────────────────────────────────────────────── */

/* Resolve initial active-limit power for a freshly-ready CS:
 * EC characteristic → CS failsafe power → static constant.
 * Returns positive magnitude; LPP write site negates for SPINE
 * production sign convention. */
static double cb_resolve_initial_power(cb_app_t* app, const char* sid,
                                         bool is_lpc, const char** src_out) {
    uc_lc_ec_characteristic_t ec;
    if (is_lpc) uc_lpc_eg_get_ec_characteristic(app->lc_eg, sid, &ec);
    else        uc_lpp_eg_get_ec_characteristic(app->lc_eg, sid, &ec);
    if (ec.type != LC_POWER_MAX_NONE && fabs(ec.value_w) > 0.0) {
        *src_out = "EC";
        return fabs(ec.value_w);
    }

    uc_lc_failsafe_t fs;
    if (is_lpc) uc_lpc_eg_get_failsafe(app->lc_eg, sid, &fs);
    else        uc_lpp_eg_get_failsafe(app->lc_eg, sid, &fs);
    if (fabs(fs.power_limit_w) > 0.0) {
        *src_out = "failsafe";
        return fabs(fs.power_limit_w);
    }

    *src_out = "constant";
    return is_lpc ? CB_INITIAL_DEFAULT_LPC_W : CB_INITIAL_DEFAULT_LPP_W;
}

static void on_cs_ready(uc_lc_eg_t* eg, const char* session_id,
                          const char* entity_type,
                          bool lpc_supported, bool lpp_supported,
                          bool lpc_available, bool lpp_available, void* ud) {
    (void)eg;
    cb_app_t* app = (cb_app_t*)ud;
    eebus_mutex_lock(app->mu);
    demo_device_t* d = dev_find_by_session(app->devices, session_id);
    if (d) {
        if (lpc_supported) {
            d->lpc.support = true;
            d->lpc.available = lpc_available;
            snprintf(d->lpc.actor, sizeof(d->lpc.actor), "ControllableSystem");
            snprintf(d->lpc.entity_type, sizeof(d->lpc.entity_type), "%s",
                     entity_type ? entity_type : "");
        }
        if (lpp_supported) {
            d->lpp.support = true;
            d->lpp.available = lpp_available;
            snprintf(d->lpp.actor, sizeof(d->lpp.actor), "ControllableSystem");
            snprintf(d->lpp.entity_type, sizeof(d->lpp.entity_type), "%s",
                     entity_type ? entity_type : "");
        }
    }
    eebus_mutex_unlock(app->mu);
    cli_print(app->cli, "\n[CB] CS ready: %s (%s) LPC:%s LPP:%s\n",
              session_id, entity_type ? entity_type : "",
              lpc_supported ? "yes" : "no", lpp_supported ? "yes" : "no");

    /* Default policy [LPC-913]: send an active limit within the 60s
     * acceptance window so the remote CS leaves INIT. Initial power
     * comes from EC → CS failsafe → static constant; user can override
     * later via 'lpc limit ...' / 'lpp limit ...'. */
    if (lpc_available) {
        const char* src;
        double p_w = cb_resolve_initial_power(app, session_id, true, &src);
        uc_lc_limit_t lim = {
            .is_limit_active     = true,
            .is_limit_changeable = true,
            .value_w             = p_w,
        };
        uc_lpc_eg_write_limit(eg, session_id, &lim);
        EEBUS_LOG_INFO("CB",
            "on_cs_ready: %s LPC active %.0fW (from %s)",
            session_id, p_w, src);
    }
    if (lpp_available) {
        const char* src;
        double p_w = cb_resolve_initial_power(app, session_id, false, &src);
        uc_lc_limit_t lim = {
            .is_limit_active     = true,
            .is_limit_changeable = true,
            .value_w             = -p_w,   /* LPP wire convention: value_w <= 0 */
        };
        uc_lpp_eg_write_limit(eg, session_id, &lim);
        EEBUS_LOG_INFO("CB",
            "on_cs_ready: %s LPP active %.0fW (from %s)",
            session_id, p_w, src);
    }
}

/* ── Dashboard ──────────────────────────────────────────────────────────── */

static void dashboard_render(void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    trust_mgr_t* trust = ship_helper_get_trust(app->ship);
    eebus_mutex_lock(app->mu);
    dashboard_printf("  EEBus Control Box\n");
    dashboard_separator();
    dev_print_table_header();
    for (int i = 0; i < DEMO_MAX_DEVICES; i++)
        dev_print_table_row(&app->devices[i], trust);
    eebus_mutex_unlock(app->mu);
}

/* ── CLI handlers ───────────────────────────────────────────────────────── */

static void cmd_monitor(int argc, char** argv, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    if (argc < 2)                            dashboard_refresh(app->dashboard);
    else if (!strcmp(argv[1], "on"))         dashboard_set_mode(app->dashboard, DASHBOARD_MODE_AUTO);
    else if (!strcmp(argv[1], "off"))        dashboard_set_mode(app->dashboard, DASHBOARD_MODE_MANUAL);
    else                                     printf("Usage: monitor [on|off]\n");
}

/* Pressing Enter while monitor is AUTO drops back to MANUAL. */
static void on_blank_line(void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    dashboard_set_mode(app->dashboard, DASHBOARD_MODE_MANUAL);
}

/* ── lpc/lpp dispatch ───────────────────────────────────────────────────── */

typedef struct {
    const char* dir_name;   /* "lpc" or "lpp" */
    bool        is_lpc;
} cb_dir_ctx_t;

static spine_err_t cb_dir_write_limit(uc_lpc_lpp_eg_t* eg, bool is_lpc,
                                       const char* sid, const uc_lc_limit_t* lim) {
    if (is_lpc) return uc_lpc_eg_write_limit(eg, sid, lim);
    /* LPP: SPINE production convention requires value_w <= 0. CS direction
     * validation checks the sign regardless of is_limit_active, so flip
     * unconditionally to be safe. */
    uc_lc_limit_t flipped = *lim;
    flipped.value_w = -fabs(flipped.value_w);
    return uc_lpp_eg_write_limit(eg, sid, &flipped);
}

static void cb_dir_get_limit(const uc_lpc_lpp_eg_t* eg, bool is_lpc,
                              const char* sid, uc_lc_limit_t* out) {
    if (is_lpc) uc_lpc_eg_get_limit(eg, sid, out);
    else        uc_lpp_eg_get_limit(eg, sid, out);
}

static void cb_dir_get_failsafe(const uc_lpc_lpp_eg_t* eg, bool is_lpc,
                                 const char* sid, uc_lc_failsafe_t* out) {
    if (is_lpc) uc_lpc_eg_get_failsafe(eg, sid, out);
    else        uc_lpp_eg_get_failsafe(eg, sid, out);
}

static void cb_dir_get_ec(const uc_lpc_lpp_eg_t* eg, bool is_lpc,
                           const char* sid, uc_lc_ec_characteristic_t* out) {
    if (is_lpc) uc_lpc_eg_get_ec_characteristic(eg, sid, out);
    else        uc_lpp_eg_get_ec_characteristic(eg, sid, out);
}

static void cmd_lc_dispatch(int argc, char** argv, cb_app_t* app, cb_dir_ctx_t* ctx) {
    if (argc < 2) {
        printf("Usage: %s [limit|failsafe|show]\n", ctx->dir_name);
        return;
    }
    const char* sub = argv[1];

    /* limit <ship-id> <W>|off [endTime] */
    if (strcmp(sub, "limit") == 0) {
        if (argc < 4) {
            printf("Usage: %s limit <ship-id> <W>|off [endTime]\n", ctx->dir_name);
            return;
        }
        eebus_mutex_lock(app->mu);
        demo_device_t* d = dev_find_by_id(app->devices, argv[2]);
        if (!d || !d->ship.session_id[0]) {
            printf("Device not found or not connected: %s\n", argv[2]);
            eebus_mutex_unlock(app->mu); return;
        }
        uc_lc_limit_t lim = { .is_limit_changeable = true };
        if (strcmp(argv[3], "off") == 0) {
            lim.is_limit_active = false;
        } else {
            lim.is_limit_active = true;
            lim.value_w = strtod(argv[3], NULL);
            if (argc >= 5) {
                snprintf(lim.time_period.end_time, sizeof(lim.time_period.end_time),
                         "%s", argv[4]);
                lim.time_period.end_time_present = true;
            }
        }
        cb_dir_write_limit(app->lc_eg, ctx->is_lpc, d->ship.session_id, &lim);
        printf("[CB] %s limit → %s: ", ctx->dir_name, dev_display_name(d));
        if (lim.is_limit_active) {
            printf("%.0fW%s%s\n", fabs(lim.value_w),
                   lim.time_period.end_time_present ? "  endTime: " : "",
                   lim.time_period.end_time_present ? lim.time_period.end_time : "");
        } else printf("inactive\n");
        eebus_mutex_unlock(app->mu);
        return;
    }

    /* failsafe <ship-id> power <W>
     * failsafe <ship-id> duration <S> */
    if (strcmp(sub, "failsafe") == 0) {
        if (argc < 5) {
            printf("Usage: %s failsafe <ship-id> {power <W> | duration <S>}\n",
                   ctx->dir_name);
            return;
        }
        eebus_mutex_lock(app->mu);
        demo_device_t* d = dev_find_by_id(app->devices, argv[2]);
        if (!d || !d->ship.session_id[0]) {
            printf("Device not found or not connected: %s\n", argv[2]);
            eebus_mutex_unlock(app->mu); return;
        }
        const char* sub2 = argv[3];
        const char* sid  = d->ship.session_id;
        if (strcmp(sub2, "power") == 0) {
            double p_w = strtod(argv[4], NULL);
            if (ctx->is_lpc) uc_lpc_eg_write_failsafe_power(app->lc_eg, sid, p_w);
            else             uc_lpp_eg_write_failsafe_power(app->lc_eg, sid, p_w);
            printf("[CB] %s failsafe power → %s: %.0fW\n",
                   ctx->dir_name, dev_display_name(d), p_w);
        } else if (strcmp(sub2, "duration") == 0) {
            uint32_t s = (uint32_t)atoi(argv[4]);
            if (ctx->is_lpc) uc_lpc_eg_write_failsafe_duration(app->lc_eg, sid, s);
            else             uc_lpp_eg_write_failsafe_duration(app->lc_eg, sid, s);
            printf("[CB] %s failsafe duration → %s: %us\n",
                   ctx->dir_name, dev_display_name(d), s);
        } else {
            printf("Usage: %s failsafe <ship-id> {power <W> | duration <S>}\n",
                   ctx->dir_name);
        }
        eebus_mutex_unlock(app->mu);
        return;
    }

    /* show */
    if (strcmp(sub, "show") == 0) {
        eebus_mutex_lock(app->mu);
        printf("%s downstream CS:\n", ctx->dir_name);
        dev_print_lc_header();
        int found = 0;
        for (int i = 0; i < DEMO_MAX_DEVICES; i++) {
            demo_device_t* d = &app->devices[i];
            if (!d->active) continue;
            const dev_uc_t* uc = ctx->is_lpc ? &d->lpc : &d->lpp;
            if (!uc->support || strcmp(uc->actor, "ControllableSystem") != 0) continue;
            uc_lc_limit_t lim;
            uc_lc_failsafe_t fs;
            uc_lc_ec_characteristic_t ec;
            cb_dir_get_limit(app->lc_eg, ctx->is_lpc, d->ship.session_id, &lim);
            cb_dir_get_failsafe(app->lc_eg, ctx->is_lpc, d->ship.session_id, &fs);
            cb_dir_get_ec(app->lc_eg, ctx->is_lpc, d->ship.session_id, &ec);
            dev_print_lc_row(d, ctx->is_lpc, &lim, &fs, &ec);
            found++;
        }
        if (!found) printf("  (none)\n");
        dev_print_lc_footer();
        eebus_mutex_unlock(app->mu);
        return;
    }

    printf("Usage: %s [limit|failsafe|show]\n", ctx->dir_name);
}

static void cmd_lpc(int argc, char** argv, void* ud) {
    cb_dir_ctx_t ctx = { .dir_name = "lpc", .is_lpc = true };
    cmd_lc_dispatch(argc, argv, (cb_app_t*)ud, &ctx);
}

static void cmd_lpp(int argc, char** argv, void* ud) {
    cb_dir_ctx_t ctx = { .dir_name = "lpp", .is_lpc = false };
    cmd_lc_dispatch(argc, argv, (cb_app_t*)ud, &ctx);
}

static void cmd_trust(int argc, char** argv, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    trust_mgr_t* t = ship_helper_get_trust(app->ship);
    if (argc < 2) { printf("Usage: trust [list|accept|reject|revoke|mode]\n"); return; }
    if      (strcmp(argv[1], "list")   == 0) trust_mgr_print_list(t);
    else if (strcmp(argv[1], "accept") == 0 && argc >= 3) trust_mgr_accept(t, argv[2]);
    else if (strcmp(argv[1], "reject") == 0 && argc >= 3) trust_mgr_reject(t, argv[2]);
    else if (strcmp(argv[1], "revoke") == 0 && argc >= 3) trust_mgr_revoke(t, argv[2]);
    else if (strcmp(argv[1], "mode")   == 0 && argc >= 3)
        trust_mgr_set_mode(t, strcmp(argv[2], "auto") == 0 ? TRUST_MODE_AUTO : TRUST_MODE_MANUAL);
}

static void cmd_connect(int argc, char** argv, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    if (argc < 2) { printf("Usage: connect <IP>:<PORT>\n"); return; }
    char ip[64]; uint16_t port = CB_DEFAULT_PORT;
    char* colon = strrchr(argv[1], ':');
    if (colon) {
        size_t n = (size_t)(colon - argv[1]);
        if (n >= sizeof(ip)) n = sizeof(ip) - 1;
        memcpy(ip, argv[1], n); ip[n] = '\0';
        port = (uint16_t)atoi(colon + 1);
    } else snprintf(ip, sizeof(ip), "%s", argv[1]);
    ship_helper_connect(app->ship, ip, port);
}

static void cmd_device(int argc, char** argv, void* ud) {
    cb_app_t* app = (cb_app_t*)ud;
    trust_mgr_t* trust = ship_helper_get_trust(app->ship);
    eebus_mutex_lock(app->mu);

    if (argc < 2) {
        printf("  %-30s %s\n", "SHIP-Id", "SKI");
        int found = 0;
        for (int i = 0; i < DEMO_MAX_DEVICES; i++) {
            demo_device_t* d = &app->devices[i];
            if (!d->active) continue;
            printf("  %-30s %s\n",
                   d->mdns.info.id[0] ? d->mdns.info.id : "-",
                   d->mdns.info.ski[0] ? d->mdns.info.ski : "-");
            found++;
        }
        if (!found) printf("  (none)\n");
        eebus_mutex_unlock(app->mu);
        return;
    }

    demo_device_t* d = dev_find_by_id(app->devices, argv[1]);
    if (!d) {
        printf("Device not found: %s\n", argv[1]);
        eebus_mutex_unlock(app->mu);
        return;
    }
    dev_print_detail(d, trust);
    eebus_mutex_unlock(app->mu);
}

static void cmd_log(int argc, char** argv, void* ud) {
    (void)ud;
    if (argc < 2) return;
    if (!strcmp(argv[1], "debug")) eebus_log_set_level(EEBUS_LOG_DEBUG);
    else if (!strcmp(argv[1], "info")) eebus_log_set_level(EEBUS_LOG_INFO);
    else if (!strcmp(argv[1], "off"))  eebus_log_set_level(EEBUS_LOG_ERROR + 1);
}

static void cmd_quit(int argc, char** argv, void* ud) {
    (void)argc; (void)argv;
    cb_app_t* app = (cb_app_t*)ud; app->quit = 1; cli_quit(app->cli);
}

/* ── Signal ─────────────────────────────────────────────────────────────── */

static cb_app_t* g_app = NULL;
static void on_signal(int sig) { (void)sig; if (g_app) { g_app->quit = 1; cli_quit(g_app->cli); } }

/* ── main ───────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    uint16_t port = CB_DEFAULT_PORT;
    trust_mode_t trust_mode = TRUST_MODE_MANUAL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trust") && i+1 < argc && !strcmp(argv[++i], "auto"))
            trust_mode = TRUST_MODE_AUTO;
        else if (!strcmp(argv[i], "--port") && i+1 < argc)
            port = (uint16_t)atoi(argv[++i]);
    }

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         EEBus Control Box                        ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    eebus_log_config_t log_cfg = EEBUS_LOG_CONFIG_DEFAULT;
    log_cfg.min_level = EEBUS_LOG_DEBUG; log_cfg.console_enabled = false;
    log_cfg.file_path = "eebus-cb.log";
    eebus_log_init(&log_cfg);

    cb_app_t app;
    memset(&app, 0, sizeof(app));
    eebus_mutex_create(&app.mu);
    g_app = &app;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    ship_helper_config_t ship_cfg = {
        .name = CB_DEMO_NAME, .port = port,
        .register_flag = true, .trust_mode = trust_mode,
        .on_data = on_ship_data, .on_conn = on_ship_conn,
        .on_peer_discovered = on_peer_discovered,
        .on_peer_lost = on_peer_lost,
        .userdata = &app,
        .brand = "EEBus Demo", .type = "GridGuard", .model = "Demo CB v2",
        .ship_id = "eebus-demo-cb-001",
    };
    if (ship_helper_create(&app.ship, &ship_cfg) != EEBUS_OK) return 1;
    printf("[CB] SKI: %s\n\n", ship_helper_get_ski(app.ship));

    char dev_id[SPINE_DEVICE_ID_MAX];
    snprintf(dev_id, sizeof(dev_id), "d:_n:example_%.8s", ship_helper_get_ski(app.ship));
    if (spine_device_create_local(&app.device, dev_id, ship_helper_get_ski(app.ship), "GridGuard") != SPINE_OK) return 1;
    if (spine_router_create(&app.router, app.device, spine_send, &app) != SPINE_OK) return 1;

    /* LC EG (LPC+LPP) on entity 1 */
    spine_addr_t e1; spine_addr_make(&e1, dev_id, 1, 0);
    uc_lpc_lpp_eg_config_t eg_cfg = {
        .device = app.device,
        .eg_entity_addr = e1,
        .entity_type = "GridGuard",
        .on_cs_ready = on_cs_ready,
        .userdata = &app,
        .heartbeat_interval_s = 55,
        .initial_available = true,
    };
    if (uc_lpc_lpp_eg_create(&app.lc_eg, &eg_cfg) != SPINE_OK) return 1;

    if (ship_helper_start(app.ship) != EEBUS_OK) return 1;

    app.cli = cli_create("CB");
    cli_enable_history(app.cli, CB_DEMO_NAME);
    cli_set_blank_handler(app.cli, on_blank_line, &app);
    cli_register(app.cli, "monitor",   cmd_monitor,   "monitor [on|off]",                          &app);
    cli_register(app.cli, "device",    cmd_device,    "device [<ship-id>] — list or show device details", &app);
    cli_register(app.cli, "lpc",       cmd_lpc,       "lpc [limit|failsafe|show] ...",             &app);
    cli_register(app.cli, "lpp",       cmd_lpp,       "lpp [limit|failsafe|show] ...",             &app);
    cli_register(app.cli, "trust",     cmd_trust,     "trust [list|accept|reject|revoke|mode]",    &app);
    cli_register(app.cli, "connect",   cmd_connect,   "connect <IP>:<PORT>",                       &app);
    cli_register(app.cli, "log",       cmd_log,       "log [debug|info|off]",                      &app);
    cli_register(app.cli, "quit",      cmd_quit,      "Quit",                                      &app);

    app.dashboard = dashboard_create(DASHBOARD_MODE_MANUAL, 3, dashboard_render, &app);
    dashboard_start(app.dashboard);
    cli_run(app.cli);

    app.quit = 1;
    dashboard_stop(app.dashboard); dashboard_destroy(app.dashboard);
    cli_destroy(app.cli);
    uc_lpc_lpp_eg_destroy(app.lc_eg);
    spine_router_destroy(app.router);
    spine_device_destroy(app.device);
    ship_helper_destroy(app.ship);
    eebus_mutex_destroy(app.mu);
    printf("\n[CB] Shutdown complete.\n");
    eebus_log_deinit();
    return 0;
}
