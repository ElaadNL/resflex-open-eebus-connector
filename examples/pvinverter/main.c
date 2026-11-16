// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/pvinverter/main.c
 *
 * EEBus PV Inverter Demo  (rewritten for new spine/uc API)
 *
 * Roles:
 *   LPP CS — accepts production power limits from HEMS
 */

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "eebus_mutex.h"
#include "eebus_thread.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "eebus_error.h"
#include "eebus_log.h"

#include "spine_device.h"
#include "spine_router.h"
#include "uc_lpp_cs.h"
#include "spine_codec_address.h"

#include "cli.h"
#include "dashboard.h"
#include "ship_helper.h"
#include "failsafe_persist.h"
#include "cs_config.h"

#define PV_DEFAULT_PORT   7715
#define PV_PEAK_POWER_W   10000
#define PV_DEMO_NAME      "pvinverter-01"

typedef struct {
    ship_helper_t*     ship;
    spine_device_t*    device;
    spine_router_t*    router;
    uc_lpp_cs_t*       lpp_cs;
    cli_t*             cli;
    dashboard_t*       dashboard;

    double limit_w;  bool limit_active;
    double dc_input_w;     /* irradiance-based DC power */
    double ac_output_w;    /* = min(dc*efficiency, limit) */
    double efficiency;     /* 0.95 nominal */

    eebus_mutex_t* mu;
    volatile int    quit;
} pv_app_t;

static void spine_send(const char* session_id, const char* json, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    ship_helper_send(app->ship, session_id, json);
}

static void on_ship_data(const char* session_id, const char* peer_ski,
                          const char* json, void* ud) {
    (void)peer_ski;
    spine_router_dispatch(((pv_app_t*)ud)->router, session_id, json);
}

static void on_ship_conn(const char* peer_ski, bool connected,
                         const char* session_id, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    if (connected) {
        cli_print(app->cli, "\n[PV] HEMS connected: %s\n", peer_ski);
        spine_router_on_connect(app->router, peer_ski, session_id);
    } else {
        cli_print(app->cli, "\n[PV] HEMS disconnected\n");
        spine_router_on_disconnect(app->router, session_id);
    }
}

static void recalc_output(pv_app_t* app) {
    double max_ac = app->dc_input_w * app->efficiency;
    app->ac_output_w = (app->limit_active && max_ac > app->limit_w)
                       ? app->limit_w : max_ac;
}

static const char* state_str(lc_cs_state_t s) {
    switch (s) {
    case LC_STATE_INIT:                 return "init";
    case LC_STATE_UNLIMITED_CONTROLLED: return "unlimited/controlled";
    case LC_STATE_LIMITED:              return "limited";
    case LC_STATE_FAILSAFE:            return "failsafe";
    case LC_STATE_UNLIMITED_AUTONOMOUS: return "unlimited/autonomous";
    }
    return "unknown";
}

static bool on_failsafe_write(const uc_lc_failsafe_t* lpc_failsafe,
                               const uc_lc_failsafe_t* lpp_failsafe, void* ud) {
    (void)lpc_failsafe;
    pv_app_t* app = (pv_app_t*)ud;
    if (lpp_failsafe) {
        cli_print(app->cli, "[PV] Failsafe accepted: %.0fW, duration=%us\n",
                  lpp_failsafe->power_limit_w, lpp_failsafe->duration_s);
        if (!failsafe_persist_update_lpp(PV_DEMO_NAME, lpp_failsafe))
            fprintf(stderr, "[PV] Failsafe persist failed\n");
    }
    return true;
}

static void on_eg_ready(const char* session_id, const char* entity_type,
                         uc_lc_eg_dir_t eg_support, uc_lc_eg_dir_t eg_available,
                         void* ud) {
    (void)eg_support; (void)eg_available;
    pv_app_t* app = (pv_app_t*)ud;
    cli_print(app->cli, "\n[PV] EG ready: %s (%s)\n", session_id, entity_type);
}

static void on_state_change(uc_lpp_cs_t* cs, lc_cs_state_t old_state,
                             lc_cs_state_t new_state, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    cli_print(app->cli, "[PV] CS state: %s → %s\n", state_str(old_state), state_str(new_state));
    eebus_mutex_lock(app->mu);
    switch (new_state) {
    case LC_STATE_INIT:
    case LC_STATE_FAILSAFE: {
        uc_lc_failsafe_t fs;
        uc_lpp_cs_get_failsafe(cs, &fs);
        app->limit_active = true;
        app->limit_w = fabs(fs.power_limit_w);
        recalc_output(app);
        break;
    }
    case LC_STATE_LIMITED: {
        uc_lc_limit_t lim;
        uc_lpp_cs_get_limit(cs, &lim);
        app->limit_active = true;
        app->limit_w = fabs(lim.value_w);
        recalc_output(app);
        break;
    }
    case LC_STATE_UNLIMITED_CONTROLLED:
    case LC_STATE_UNLIMITED_AUTONOMOUS:
        app->limit_active = false;
        app->limit_w = PV_PEAK_POWER_W;
        recalc_output(app);
        break;
    }
    eebus_mutex_unlock(app->mu);
}

static bool on_limit_write(const uc_lc_limit_t* lpc_limit,
                            const uc_lc_limit_t* lpp_limit, void* ud) {
    (void)lpc_limit;
    pv_app_t* app = (pv_app_t*)ud;
    if (lpp_limit)
        cli_print(app->cli, "[PV] Limit write: %s  %.0fW%s%s\n",
                  lpp_limit->is_limit_active ? "active" : "inactive",
                  fabs(lpp_limit->value_w),
                  lpp_limit->time_period.end_time_present ? "  endTime: " : "",
                  lpp_limit->time_period.end_time_present
                      ? lpp_limit->time_period.end_time : "");
    return true;
}

static void bg_thread(void* arg) {
    pv_app_t* app = (pv_app_t*)arg;
    int tick = 0;
    while (!app->quit) {
        nanosleep(&(struct timespec){1, 0}, NULL);
        eebus_mutex_lock(app->mu);
        /* Simulate sun following a sine curve over 86400s */
        tick++;
        double hour_fraction = fmod((double)tick / 3600.0, 24.0);
        double sun = fmax(0.0, sin((hour_fraction - 6.0) / 12.0 * M_PI));
        app->dc_input_w = sun * PV_PEAK_POWER_W + (double)(rand() % 200 - 100);
        if (app->dc_input_w < 0.0) app->dc_input_w = 0.0;
        recalc_output(app);
        eebus_mutex_unlock(app->mu);
    }
}

static void fmt_eg_hb(uc_lpp_cs_t* cs, char* buf, size_t sz) {
    time_t t; bool active;
    if (!uc_lpp_cs_get_eg_heartbeat(cs, &t, &active)) { snprintf(buf, sz, "never"); return; }
    struct tm lt; localtime_r(&t, &lt);
    char hms[16]; strftime(hms, sizeof(hms), "%H:%M:%S", &lt);
    snprintf(buf, sz, "%s (%llds ago%s)", hms, (long long)(time(NULL) - t),
             active ? "" : ", stale");
}

static void dashboard_render(void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    eebus_mutex_lock(app->mu);
    dashboard_printf("  EEBus PV Inverter\n");
    dashboard_separator();
    dashboard_row("DC input:", "%.0f W", app->dc_input_w);
    dashboard_row("AC output:", "%.0f W", app->ac_output_w);
    dashboard_row("Efficiency:", "%.1f %%", app->efficiency * 100.0);
    dashboard_separator();
    dashboard_row("CS state:", "%s", state_str(uc_lpp_cs_get_state(app->lpp_cs)));
    char eg_hb[48]; fmt_eg_hb(app->lpp_cs, eg_hb, sizeof(eg_hb));
    dashboard_row("Last EG heartbeat:", "%s", eg_hb);
    uc_lc_limit_t lim; uc_lpp_cs_get_limit(app->lpp_cs, &lim);
    dashboard_printf("  Limit:        %.0f W (%s)%s%s\n",
           fabs(lim.value_w), lim.is_limit_active ? "active" : "inactive",
           lim.time_period.end_time_present ? "  endTime: " : "",
           lim.time_period.end_time_present ? lim.time_period.end_time : "");
    uc_lc_failsafe_t fs; uc_lpp_cs_get_failsafe(app->lpp_cs, &fs);
    dashboard_row("Failsafe:", "%.0f W / %us", fs.power_limit_w, fs.duration_s);
    uc_lc_ec_characteristic_t ec; uc_lpp_cs_get_ec_characteristic(app->lpp_cs, &ec);
    if (ec.type != LC_POWER_MAX_NONE && ec.value_w > 0.0)
        dashboard_row("EC max:", "%.0f W (%s)", fabs(ec.value_w),
                      ec.type == LC_POWER_MAX_NOMINAL ? "Nominal" : "Contractual");
    eebus_mutex_unlock(app->mu);
}

static void cmd_monitor(int argc, char** argv, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    if (argc < 2)                     dashboard_refresh(app->dashboard);
    else if (!strcmp(argv[1], "on"))  dashboard_set_mode(app->dashboard, DASHBOARD_MODE_AUTO);
    else if (!strcmp(argv[1], "off")) dashboard_set_mode(app->dashboard, DASHBOARD_MODE_MANUAL);
    else                              printf("Usage: monitor [on|off]\n");
}

/* Pressing Enter while monitor is AUTO drops back to MANUAL. */
static void on_blank_line(void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    dashboard_set_mode(app->dashboard, DASHBOARD_MODE_MANUAL);
}

static void cmd_irr(int argc, char** argv, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    if (argc < 2) { printf("Usage: irr <W>  (DC input power)\n"); return; }
    eebus_mutex_lock(app->mu);
    app->dc_input_w = strtod(argv[1], NULL);
    recalc_output(app);
    eebus_mutex_unlock(app->mu);
}

static void cmd_trust(int argc, char** argv, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    trust_mgr_t* t = ship_helper_get_trust(app->ship);
    if (argc < 2) { printf("Usage: trust [list|accept|reject|revoke|mode]\n"); return; }
    if      (!strcmp(argv[1], "list"))                 trust_mgr_print_list(t);
    else if (!strcmp(argv[1], "accept") && argc >= 3) trust_mgr_accept(t, argv[2]);
    else if (!strcmp(argv[1], "reject") && argc >= 3) trust_mgr_reject(t, argv[2]);
    else if (!strcmp(argv[1], "revoke") && argc >= 3) trust_mgr_revoke(t, argv[2]);
    else if (!strcmp(argv[1], "mode")   && argc >= 3)
        trust_mgr_set_mode(t, !strcmp(argv[2], "auto") ? TRUST_MODE_AUTO : TRUST_MODE_MANUAL);
}

static void cmd_connect(int argc, char** argv, void* ud) {
    pv_app_t* app = (pv_app_t*)ud;
    if (argc < 2) { printf("Usage: connect <IP>:<PORT>\n"); return; }
    char ip[64]; uint16_t port = PV_DEFAULT_PORT;
    char* colon = strrchr(argv[1], ':');
    if (colon) {
        size_t n = (size_t)(colon-argv[1]); if (n>=sizeof(ip)) n=sizeof(ip)-1;
        memcpy(ip,argv[1],n); ip[n]='\0'; port=(uint16_t)atoi(colon+1);
    } else snprintf(ip,sizeof(ip),"%s",argv[1]);
    ship_helper_connect(app->ship, ip, port);
}

static void cmd_log(int argc, char** argv, void* ud) {
    (void)ud; if (argc<2) return;
    if (!strcmp(argv[1],"debug")) eebus_log_set_level(EEBUS_LOG_DEBUG);
    else if (!strcmp(argv[1],"info")) eebus_log_set_level(EEBUS_LOG_INFO);
    else if (!strcmp(argv[1],"off"))  eebus_log_set_level(EEBUS_LOG_ERROR+1);
}

static void cmd_quit(int argc, char** argv, void* ud) {
    (void)argc; (void)argv;
    pv_app_t* app = (pv_app_t*)ud; app->quit = 1; cli_quit(app->cli);
}

static pv_app_t* g_app = NULL;
static void on_signal(int s) { (void)s; if (g_app) { g_app->quit=1; cli_quit(g_app->cli); } }


int main(int argc, char** argv) {
    uint16_t port = PV_DEFAULT_PORT;
    trust_mode_t trust_mode = TRUST_MODE_MANUAL;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"--trust") && i+1<argc && !strcmp(argv[++i],"auto"))
            trust_mode = TRUST_MODE_AUTO;
        else if (!strcmp(argv[i],"--port") && i+1<argc)
            port = (uint16_t)atoi(argv[++i]);
    }
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         EEBus PV Inverter                        ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    eebus_log_config_t lcfg = EEBUS_LOG_CONFIG_DEFAULT;
    lcfg.min_level = EEBUS_LOG_DEBUG; lcfg.console_enabled = false;
    lcfg.file_path = "eebus-pv.log"; eebus_log_init(&lcfg);

    pv_app_t app; memset(&app, 0, sizeof(app));
    eebus_mutex_create(&app.mu);
    app.limit_w = PV_PEAK_POWER_W; app.efficiency = 0.95;
    app.dc_input_w = 5000.0;
    recalc_output(&app);
    g_app = &app;
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);

    ship_helper_config_t ship_cfg = {
        .name = PV_DEMO_NAME, .port = port,
        .register_flag = true, .trust_mode = trust_mode,
        .on_data = on_ship_data, .on_conn = on_ship_conn, .userdata = &app,
        .brand = "EEBus Demo", .type = "Inverter", .model = "Demo PV v2",
        .ship_id = "eebus-demo-pv-001",
    };
    if (ship_helper_create(&app.ship, &ship_cfg) != EEBUS_OK) return 1;
    printf("[PV] SKI: %s\n\n", ship_helper_get_ski(app.ship));

    char dev_id[SPINE_DEVICE_ID_MAX];
    snprintf(dev_id, sizeof(dev_id), "d:_n:example_%.8s", ship_helper_get_ski(app.ship));
    if (spine_device_create_local(&app.device, dev_id, ship_helper_get_ski(app.ship), "Inverter") != SPINE_OK) return 1;
    if (spine_router_create(&app.router, app.device, spine_send, &app) != SPINE_OK) return 1;

    spine_addr_t e1; spine_addr_make(&e1, dev_id, 1, 0);
    uc_lpp_cs_config_t lpp_cfg = {
        .entity = {
            .device = app.device, .cs_entity_addr = e1,
            .entity_type = "Inverter",
            .initial_available = true,
            .heartbeat_interval_s = 55,
            .measurement_id = 1,
            .on_limit_write = on_limit_write,
            .on_failsafe_write = on_failsafe_write,
            .on_eg_ready = on_eg_ready,
            .userdata = &app,
        },
        .lpp = {
            .initial_limit = { .is_limit_changeable = true },
            .initial_failsafe = { .power_limit_w = PV_PEAK_POWER_W, .duration_s = 7200,
                                   .power_limit_changeable = true, .duration_changeable = true },
            .initial_ec_characteristic = { .type = LC_POWER_MAX_NOMINAL, .value_w = PV_PEAK_POWER_W,
                                           .electrical_connection_id = 1, .parameter_id = 1 },
            .on_state_change = on_state_change,
        },
    };
    static const cs_config_t pv_cs_defaults = {
        .lpp_present = true,
        .lpp = {
            .apl_max_w     = PV_PEAK_POWER_W,
            .apl_min_w     = 0,
            .fapl_max_w    = PV_PEAK_POWER_W,
            .fapl_min_w    = 0,
            .pfapl_w       = PV_PEAK_POWER_W,
            .mfsdm_s       = 36000,
            .pfsdm_s       = 7200,
            .nominal_max_w = PV_PEAK_POWER_W,
        },
    };
    cs_config_init_if_absent(PV_DEMO_NAME, &pv_cs_defaults);
    cs_config_t pv_cscfg;
    if (cs_config_load(PV_DEMO_NAME, &pv_cscfg) && pv_cscfg.lpp_present) {
        lpp_cfg.lpp.initial_failsafe.power_limit_w    = pv_cscfg.lpp.pfapl_w;
        lpp_cfg.lpp.initial_failsafe.duration_s       = pv_cscfg.lpp.pfsdm_s;
        lpp_cfg.lpp.initial_ec_characteristic.value_w = pv_cscfg.lpp.nominal_max_w;
    }
    failsafe_persist_t pv_fp = {0};
    if (failsafe_persist_load(PV_DEMO_NAME, &pv_fp) && pv_fp.lpp_present)
        lpp_cfg.lpp.initial_failsafe = pv_fp.lpp;
    if (uc_lpp_cs_create(&app.lpp_cs, &lpp_cfg) != SPINE_OK) return 1;

    if (ship_helper_start(app.ship) != EEBUS_OK) return 1;

    eebus_thread_t* bg = NULL; eebus_thread_create(&bg, bg_thread, &app, "pv-bg", 0);

    app.cli = cli_create("PV");
    cli_enable_history(app.cli, PV_DEMO_NAME);
    cli_set_blank_handler(app.cli, on_blank_line, &app);
    cli_register(app.cli, "monitor", cmd_monitor, "monitor [on|off]",       &app);
    cli_register(app.cli, "irr",     cmd_irr,     "irr <W>  set DC input",  &app);
    cli_register(app.cli, "trust",   cmd_trust,   "trust [list|accept|reject|mode]", &app);
    cli_register(app.cli, "connect", cmd_connect, "connect <IP>:<PORT>",    &app);
    cli_register(app.cli, "log",     cmd_log,     "log [debug|info|off]",   &app);
    cli_register(app.cli, "cfg",     cs_config_cli_handler,
                 "cfg [show [lpc|lpp] | set <lpc|lpp> <field> <value>]",
                 (void*)PV_DEMO_NAME);
    cli_register(app.cli, "quit",    cmd_quit,    "Quit",                   &app);

    app.dashboard = dashboard_create(DASHBOARD_MODE_MANUAL, 3, dashboard_render, &app);
    dashboard_start(app.dashboard);
    cli_run(app.cli);

    app.quit = 1; eebus_thread_join(bg);
    dashboard_stop(app.dashboard); dashboard_destroy(app.dashboard);
    cli_destroy(app.cli);
    uc_lpp_cs_destroy(app.lpp_cs);
    spine_router_destroy(app.router);
    spine_device_destroy(app.device);
    ship_helper_destroy(app.ship); eebus_mutex_destroy(app.mu);
    printf("\n[PV] Shutdown complete.\n"); eebus_log_deinit(); return 0;
}
