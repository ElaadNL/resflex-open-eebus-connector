// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc_cs.c — LoadControl Controllable System (CS) implementation.
 *
 * One uc_lc_cs_entity owns one SPINE entity and one or two per-direction
 * contexts (uc_lc_cs). The LPC-only / LPP-only / combined use cases are thin
 * wrappers over the shared internal entity create/destroy.
 *
 * NOTE: this file is being built up step by step. The operational API,
 * the real feature handlers, the heartbeat/timer machinery and the
 * ElectricalConnection handler are filled in by later steps; the handlers
 * are registered here as stubs so the entity can be created and the
 * translation unit links.
 */

#include "uc_lc_cs.h"
#include "../spine/codec/spine_codec_load_ctrl.h"
#include "../spine/codec/spine_codec_dev_cfg.h"
#include "../spine/codec/spine_codec_dev_diag.h"
#include "../spine/codec/spine_codec_elec_conn.h"
#include "../spine/codec/spine_codec_result.h"
#include "../spine/codec/spine_rfe.h"
#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"
#include "../platform/eebus_mutex.h"
#include "../platform/eebus_timer.h"

#define LOG_TAG "uc.lc.cs"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ════════════════════════════════════════════════════════════════════════════
 * Structs
 * ════════════════════════════════════════════════════════════════════════════ */

struct uc_lc_cs {
    struct uc_lc_cs_entity*   entity;        /* back-pointer (mandatory) */
    lc_direction_t            direction;
    bool                      enabled;       /* false → direction not present */
    lc_cs_state_t             state;

    uint32_t                  limit_id;
    uint32_t                  failsafe_value_key_id;
    uint32_t                  failsafe_duration_key_id;  /* shared key in dual */
    uint32_t                  ec_char_id;

    uc_lc_cs_state_change_fn  on_state_change;

    eebus_timer_t*            init_timer;
    eebus_timer_t*            limit_write_timer;
    eebus_timer_t*            failsafe_dur_timer;
    eebus_timer_t*            limit_duration_timer;
};

typedef struct uc_lc_cs_entity uc_lc_cs_entity_t;

struct uc_lc_cs_entity {
    uc_lc_cs_entity_config_t  ent_cfg;

    spine_addr_t              lc_server_addr;
    spine_addr_t              cfg_server_addr;
    spine_addr_t              diag_server_addr;
    spine_addr_t              diag_client_addr;
    spine_addr_t              ec_server_addr;

    lc_limit_list_t           limit_list;     /* merged: LPC + LPP limits */
    dev_cfg_kv_list_t         failsafe_kv;    /* merged: values + shared duration */
    ec_characteristic_list_t  ec_chars;       /* merged: LPC + LPP EC */
    ec_description_list_t     ec_descs;       /* one entry per electrical connection */
    lc_limit_desc_list_t      limit_descs;    /* merged descriptions */
    dev_cfg_kv_desc_list_t    cfg_descs;      /* merged kv descriptions */

    uint64_t                  hb_counter;
    eebus_timer_t*            hb_timer;
    bool                      eg_hb_received;
    time_t                    eg_hb_received_time;
    eebus_timer_t*            eg_hb_watchdog;

    eebus_mutex_t*            mutex;

    uc_lc_cs_t                lpc;            /* enabled=false → not present */
    uc_lc_cs_t                lpp;
};

/* ── Forward declarations (timers / heartbeat / peer-ready) ───────────────── */
static const char* lc_state_str(lc_cs_state_t s);
static void cs_init_timer_cb(void* ud);
static void cs_failsafe_dur_cb(void* ud);
static void cs_limit_write_timer_cb(void* ud);
static void cs_limit_duration_cb(void* ud);
static void cs_hb_watchdog_cb(void* ud);
static void cs_hb_timer_cb(void* ud);
static void cs_diag_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                  const spine_msg_t* msg);
static void cs_diag_handle_notify(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                    const spine_msg_t* msg);
static void lc_cs_peer_ready(spine_device_t* remote_dev,
                               const spine_nm_uc_entry_t* uc_infos,
                               uint8_t uc_count, void* ud);

/* ════════════════════════════════════════════════════════════════════════════
 * Conversion helpers (UC ↔ codec)
 * ════════════════════════════════════════════════════════════════════════════ */

/* UC → codec */
static void limit_to_codec(const uc_lc_limit_t* lim, uint32_t limit_id,
                              lc_limit_data_t* out) {
    memset(out, 0, sizeof(*out));
    out->limit_id = limit_id;
    out->is_limit_changeable = lim->is_limit_changeable;
    out->is_limit_changeable_present = true;
    out->is_limit_active = lim->is_limit_active;
    out->is_limit_active_present = true;
    out->time_period = lim->time_period;
    spine_scaled_number_from_double(lim->value_w, &out->value);
    out->value_present = true;
}

/* codec → UC. NULL-tolerant: a NULL codec entry yields a zeroed result. */
static void limit_from_codec(const lc_limit_data_t* c, uc_lc_limit_t* out) {
    memset(out, 0, sizeof(*out));
    if (!c) return;
    if (c->is_limit_changeable_present)
        out->is_limit_changeable = c->is_limit_changeable;
    if (c->is_limit_active_present)
        out->is_limit_active = c->is_limit_active;
    out->time_period = c->time_period;
    if (c->value_present)
        out->value_w = spine_scaled_number_to_double(&c->value);
}

/* UC → codec EC characteristic */
static void ec_char_to_codec(const uc_lc_ec_characteristic_t* uc,
                                lc_direction_t direction,
                                uint32_t characteristic_id,
                                ec_characteristic_data_t* out) {
    memset(out, 0, sizeof(*out));
    out->electrical_connection_id = uc->electrical_connection_id;
    out->parameter_id = uc->parameter_id;
    out->characteristic_id = characteristic_id;
    snprintf(out->characteristic_context,
             sizeof(out->characteristic_context), "entity");
    out->characteristic_context_present = true;
    const char* type_str;
    if (uc->type == LC_POWER_MAX_NOMINAL)
        type_str = (direction == LC_DIRECTION_CONSUME)
            ? "powerConsumptionNominalMax" : "powerProductionNominalMax";
    else
        type_str = (direction == LC_DIRECTION_CONSUME)
            ? "contractualConsumptionNominalMax" : "contractualProductionNominalMax";
    snprintf(out->characteristic_type,
             sizeof(out->characteristic_type), "%s", type_str);
    out->characteristic_type_present = true;
    spine_scaled_number_from_double(uc->value_w, &out->value);
    out->value_present = true;
    snprintf(out->unit, sizeof(out->unit), "W");
    out->unit_present = true;
}

/* codec → UC EC characteristic. NULL-tolerant. */
static void ec_char_from_codec(const ec_characteristic_data_t* c,
                                 uc_lc_ec_characteristic_t* out) {
    memset(out, 0, sizeof(*out));
    if (!c) return;
    out->electrical_connection_id = c->electrical_connection_id;
    out->parameter_id = c->parameter_id;
    if (c->characteristic_type_present) {
        /* "contractual...NominalMax" also contains "Nominal";
         * prefix-match "contractual" first. */
        if (strncmp(c->characteristic_type, "contractual", 11) == 0)
            out->type = LC_POWER_MAX_CONTRACTUAL;
        else if (strncmp(c->characteristic_type, "power", 5) == 0)
            out->type = LC_POWER_MAX_NOMINAL;
    }
    if (c->value_present)
        out->value_w = spine_scaled_number_to_double(&c->value);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Feature handler stubs — real implementations land in later steps
 * ════════════════════════════════════════════════════════════════════════════ */

/* LoadControl read/write — implementations at end of file. */
static void cs_lc_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                const spine_msg_t* msg);
static void cs_lc_handle_write(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                 const spine_msg_t* msg);

static void cs_lc_server_handler(spine_device_t* dev,
                                   const spine_msg_t* msg, void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    spine_cmd_t cmd = msg->header.cmd;
    const char* fn  = msg->payload.fn_name;

    if (cmd == SPINE_CMD_READ) {
        cs_lc_handle_read(a, dev, msg);
    } else if (cmd == SPINE_CMD_WRITE &&
               strcmp(fn, "loadControlLimitListData") == 0) {
        cs_lc_handle_write(a, dev, msg);
    }
}

/* DeviceConfiguration read/write — implementations at end of file. */
static void cs_cfg_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                 const spine_msg_t* msg);
static void cs_cfg_handle_write(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                  const spine_msg_t* msg);

static void cs_cfg_server_handler(spine_device_t* dev,
                                    const spine_msg_t* msg, void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    spine_cmd_t cmd = msg->header.cmd;
    const char* fn  = msg->payload.fn_name;

    if (cmd == SPINE_CMD_READ) {
        cs_cfg_handle_read(a, dev, msg);
    } else if (cmd == SPINE_CMD_WRITE &&
               strcmp(fn, "deviceConfigurationKeyValueListData") == 0) {
        cs_cfg_handle_write(a, dev, msg);
    }
}

static void cs_diag_handler(spine_device_t* dev,
                              const spine_msg_t* msg, void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    spine_cmd_t cmd = msg->header.cmd;
    const char* fn  = msg->payload.fn_name;

    if (cmd == SPINE_CMD_READ &&
        strcmp(fn, "deviceDiagnosisHeartbeatData") == 0) {
        cs_diag_handle_read(a, dev, msg);
    } else if (cmd == SPINE_CMD_NOTIFY &&
               strcmp(fn, "deviceDiagnosisHeartbeatData") == 0) {
        cs_diag_handle_notify(a, dev, msg);
    }
}

static void cs_ec_handler(spine_device_t* dev,
                            const spine_msg_t* msg, void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    if (msg->header.cmd != SPINE_CMD_READ) return;

    const char* fn = msg->payload.fn_name;
    /* Empty fn = read-whole-feature; defaults to characteristic list. */
    bool is_char = (fn[0] == '\0')
        || strcmp(fn, "electricalConnectionCharacteristicListData") == 0;
    bool is_desc =
        strcmp(fn, "electricalConnectionDescriptionListData") == 0;
    if (!is_char && !is_desc) return;

    eebus_mutex_lock(a->mutex);
    cJSON* full = NULL;
    const char* reply_fn = NULL;
    const char* id_key   = NULL;
    if (is_char && a->ec_chars.count > 0) {
        full     = ec_characteristic_list_encode(&a->ec_chars);
        reply_fn = "electricalConnectionCharacteristicListData";
        id_key   = "characteristicId";
    } else if (is_desc && a->ec_descs.count > 0) {
        full     = ec_description_list_encode(&a->ec_descs);
        reply_fn = "electricalConnectionDescriptionListData";
        id_key   = "electricalConnectionId";
    }
    if (!full) { eebus_mutex_unlock(a->mutex); return; }

    cJSON* data = full;
    bool reply_partial = false;
    if (msg->payload.partial.present) {
        data = spine_rfe_filter_read(full, id_key,
            msg->payload.partial.selector, msg->payload.partial.elements);
        cJSON_Delete(full);
        reply_partial = true;
    }
    if (data) {
        spine_msg_t reply;
        spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                           spine_device_next_msg_counter(dev),
                           reply_fn, data);
        if (reply_partial) reply.payload.partial.present = true;
        spine_device_send(dev, &reply);
        cJSON_Delete(data);
    }
    eebus_mutex_unlock(a->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lifecycle helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* Initialize one per-direction context and append its description / cache /
 * failsafe-value / EC entries to the entity's merged lists. The shared
 * failsafe-duration entry is added separately by cs_add_duration(). */
static void cs_dir_setup(uc_lc_cs_entity_t* a, uc_lc_cs_t* dir,
                            lc_direction_t direction,
                            const uc_lc_cs_dir_config_t* cfg,
                            uint32_t limit_id, uint32_t value_key,
                            uint32_t duration_key, uint32_t ec_id) {
    dir->entity                   = a;
    dir->direction                = direction;
    dir->enabled                  = true;
    dir->state                    = LC_STATE_INIT;
    dir->limit_id                 = limit_id;
    dir->failsafe_value_key_id    = value_key;
    dir->failsafe_duration_key_id = duration_key;
    dir->ec_char_id               = ec_id;
    dir->on_state_change          = cfg->on_state_change;

    /* Limit description */
    lc_limit_desc_t* desc = &a->limit_descs.items[a->limit_descs.count++];
    desc->limit_id = limit_id;
    snprintf(desc->limit_type, sizeof(desc->limit_type),
             "signDependentAbsValueLimit");
    desc->limit_type_present = true;
    snprintf(desc->limit_category, sizeof(desc->limit_category), "obligation");
    desc->limit_category_present = true;
    snprintf(desc->limit_direction, sizeof(desc->limit_direction), "%s",
             direction == LC_DIRECTION_CONSUME ? "consume" : "produce");
    desc->limit_direction_present = true;
    snprintf(desc->unit, sizeof(desc->unit), "W");
    desc->unit_present = true;
    snprintf(desc->scope_type, sizeof(desc->scope_type), "activePowerLimit");
    desc->scope_type_present = true;
    if (a->ent_cfg.measurement_id) {
        desc->measurement_id = a->ent_cfg.measurement_id;
        desc->measurement_id_present = true;
    }

    /* Limit cache */
    limit_to_codec(&cfg->initial_limit, limit_id,
                   &a->limit_list.items[a->limit_list.count++]);

    /* Failsafe value KV description */
    dev_cfg_kv_desc_data_t* vd = &a->cfg_descs.items[a->cfg_descs.count++];
    vd->key_id = value_key;
    snprintf(vd->key_name, sizeof(vd->key_name), "%s",
             direction == LC_DIRECTION_CONSUME
             ? DEV_CFG_KEY_FAILSAFE_CONSUMPTION_ACTIVE_POWER_LIMIT
             : DEV_CFG_KEY_FAILSAFE_PRODUCTION_ACTIVE_POWER_LIMIT);
    vd->key_name_present = true;
    snprintf(vd->value_type, sizeof(vd->value_type), "scaledNumber");
    vd->value_type_present = true;
    snprintf(vd->unit, sizeof(vd->unit), "W");
    vd->unit_present = true;

    /* Failsafe value KV cache */
    dev_cfg_kv_data_t* vk = &a->failsafe_kv.items[a->failsafe_kv.count++];
    vk->key_id = value_key;
    vk->value_type = DEV_CFG_VALUE_SCALED_NUM;
    spine_scaled_number_from_double(cfg->initial_failsafe.power_limit_w,
                                    &vk->value.scaled_num_val);
    vk->value_present = true;
    vk->is_value_changeable = cfg->initial_failsafe.power_limit_changeable;
    vk->is_value_changeable_present = true;

    /* EC characteristic (Scenario 4) — only if the direction declares one */
    if (cfg->initial_ec_characteristic.type != LC_POWER_MAX_NONE) {
        ec_char_to_codec(&cfg->initial_ec_characteristic, direction, ec_id,
                         &a->ec_chars.items[a->ec_chars.count++]);
    }

    /* INIT timer: 120s with no heartbeat+limit → UNLIMITED_AUTONOMOUS [LPC-906] */
    eebus_timer_create(&dir->init_timer, cs_init_timer_cb, dir);
    eebus_timer_start(dir->init_timer, 120000, false);
}

/* Append the single shared failsafe-duration KV description + cache entry.
 * `fs` is either direction's initial_failsafe (validated equal in dual mode). */
static void cs_add_duration(uc_lc_cs_entity_t* a, uint32_t duration_key,
                               const uc_lc_failsafe_t* fs) {
    uint32_t dur = fs->duration_s;
    if (dur < 7200)  dur = 7200;
    if (dur > 86400) dur = 86400;

    dev_cfg_kv_desc_data_t* dd = &a->cfg_descs.items[a->cfg_descs.count++];
    dd->key_id = duration_key;
    snprintf(dd->key_name, sizeof(dd->key_name), "%s",
             DEV_CFG_KEY_FAILSAFE_DURATION_MINIMUM);
    dd->key_name_present = true;
    snprintf(dd->value_type, sizeof(dd->value_type), "duration");
    dd->value_type_present = true;

    dev_cfg_kv_data_t* dk = &a->failsafe_kv.items[a->failsafe_kv.count++];
    dk->key_id = duration_key;
    dk->value_type = DEV_CFG_VALUE_DURATION;
    snprintf(dk->value.duration_val, sizeof(dk->value.duration_val),
             "PT%uS", dur);
    dk->value_present = true;
    dk->is_value_changeable = fs->duration_changeable;
    dk->is_value_changeable_present = true;
}

/* Register the SPINE node-management use case for one direction. */
static void cs_register_usecase(uc_lc_cs_entity_t* a, lc_direction_t direction) {
    spine_nm_uc_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.actor, sizeof(entry.actor), "ControllableSystem");
    snprintf(entry.use_case_name, sizeof(entry.use_case_name), "%s",
             direction == LC_DIRECTION_CONSUME
             ? "limitationOfPowerConsumption"
             : "limitationOfPowerProduction");
    snprintf(entry.use_case_version, sizeof(entry.use_case_version), "1.0.0");
    snprintf(entry.use_case_doc_sub_revision,
             sizeof(entry.use_case_doc_sub_revision), "release");
    for (uint8_t s = 1; s <= 4; s++) entry.scenario_support[s-1] = s;
    entry.scenario_count = 4;
    entry.address = a->ent_cfg.cs_entity_addr;
    entry.available = a->ent_cfg.initial_available;
    spine_device_add_usecase(a->ent_cfg.device, &entry);
}

/* Stop and free one direction's four timers (NULL-safe). */
static void cs_dir_destroy_timers(uc_lc_cs_t* dir) {
    eebus_timer_destroy(dir->init_timer);
    eebus_timer_destroy(dir->limit_write_timer);
    eebus_timer_destroy(dir->failsafe_dur_timer);
    eebus_timer_destroy(dir->limit_duration_timer);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public lifecycle API
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t uc_lc_cs_create(const uc_lc_cs_entity_config_t* ent,
                              const uc_lc_cs_dir_config_t*    lpc_cfg,
                              const uc_lc_cs_dir_config_t*    lpp_cfg,
                              uc_lc_cs_t**                    lpc_out,
                              uc_lc_cs_t**                    lpp_out) {
    if (!ent || !ent->device)                return SPINE_ERR_INVALID;
    if (!lpc_cfg && !lpp_cfg)                return SPINE_ERR_INVALID;

    /* [LPC-022]: the failsafe duration is shared — both directions must
     * declare the same value / changeability. */
    if (lpc_cfg && lpp_cfg) {
        if (lpc_cfg->initial_failsafe.duration_s
                != lpp_cfg->initial_failsafe.duration_s
         || lpc_cfg->initial_failsafe.duration_changeable
                != lpp_cfg->initial_failsafe.duration_changeable) {
            EEBUS_LOG_WARN(LOG_TAG,
                "combined CS rejected: LPC/LPP failsafe duration mismatch");
            return SPINE_ERR_INVALID;
        }
    }

    uc_lc_cs_entity_t* a = EEBUS_NEW(uc_lc_cs_entity_t);
    if (!a) return SPINE_ERR_NO_MEMORY;
    eebus_mutex_create(&a->mutex);

    a->ent_cfg = *ent;
    if (!a->ent_cfg.heartbeat_interval_s)
        a->ent_cfg.heartbeat_interval_s = 60;

    /* Feature addresses (entity address + feature number) */
    uint8_t lc_nr          = ent->lc_feature_nr          ? ent->lc_feature_nr          : 1;
    uint8_t cfg_nr         = ent->cfg_feature_nr         ? ent->cfg_feature_nr         : 2;
    uint8_t diag_nr        = ent->diag_feature_nr        ? ent->diag_feature_nr        : 3;
    uint8_t diag_client_nr = ent->diag_client_feature_nr ? ent->diag_client_feature_nr : 4;
    uint8_t ec_nr          = ent->ec_feature_nr          ? ent->ec_feature_nr          : 5;
    a->lc_server_addr           = ent->cs_entity_addr;
    a->lc_server_addr.feature   = lc_nr;
    a->cfg_server_addr          = ent->cs_entity_addr;
    a->cfg_server_addr.feature  = cfg_nr;
    a->diag_server_addr         = ent->cs_entity_addr;
    a->diag_server_addr.feature = diag_nr;
    a->diag_client_addr         = ent->cs_entity_addr;
    a->diag_client_addr.feature = diag_client_nr;
    a->ec_server_addr           = ent->cs_entity_addr;
    a->ec_server_addr.feature   = ec_nr;

    /* ID assignment + per-direction setup. IDs are assigned sequentially in
     * direction order (LPC first); the duration key is shared. */
    uint32_t duration_key = (lpc_cfg && lpp_cfg) ? 3u : 2u;
    uint32_t next_limit_id = 1u, next_value_key = 1u, next_ec_id = 1u;

    if (lpc_cfg) {
        cs_dir_setup(a, &a->lpc, LC_DIRECTION_CONSUME, lpc_cfg,
                     next_limit_id++, next_value_key++, duration_key, next_ec_id++);
    }
    if (lpp_cfg) {
        cs_dir_setup(a, &a->lpp, LC_DIRECTION_PRODUCE, lpp_cfg,
                     next_limit_id++, next_value_key++, duration_key, next_ec_id++);
    }
    cs_add_duration(a, duration_key,
                    lpc_cfg ? &lpc_cfg->initial_failsafe
                            : &lpp_cfg->initial_failsafe);

    /* electricalConnectionDescriptionListData: one entry whose
     * electrical_connection_id matches the EC characteristic. AC and
     * positive=consume are fixed; other description fields are
     * intentionally omitted (spec-optional). */
    uint32_t ec_id = 0;
    if (lpc_cfg) ec_id = lpc_cfg->initial_ec_characteristic.electrical_connection_id;
    if (!ec_id && lpp_cfg) ec_id = lpp_cfg->initial_ec_characteristic.electrical_connection_id;
    if (ec_id) {
        ec_description_data_t* d = &a->ec_descs.items[a->ec_descs.count++];
        memset(d, 0, sizeof(*d));
        d->electrical_connection_id = ec_id;
        snprintf(d->power_supply_type, sizeof(d->power_supply_type), "ac");
        d->power_supply_type_present = true;
        snprintf(d->positive_energy_direction,
                 sizeof(d->positive_energy_direction), "consume");
        d->positive_energy_direction_present = true;
    }

    /* Register the entity for discovery */
    spine_device_add_entity(ent->device, ent->cs_entity_addr.entity,
                             ent->cs_entity_addr.entity_depth,
                             ent->entity_type ? ent->entity_type : "");

    /* Register all CS features (handlers are stubs until later steps).
     * ud = entity: one handler serves both directions. */
    spine_device_add_feature(ent->device, &a->lc_server_addr,
                             "LoadControl", "server", cs_lc_server_handler, a);
    spine_device_add_feature_function(ent->device, &a->lc_server_addr,
        "loadControlLimitListData", true, true, true, true);
    spine_device_add_feature_function(ent->device, &a->lc_server_addr,
        "loadControlLimitDescriptionListData", true, true, false, false);

    spine_device_add_feature(ent->device, &a->cfg_server_addr,
                             "DeviceConfiguration", "server",
                             cs_cfg_server_handler, a);
    spine_device_add_feature_function(ent->device, &a->cfg_server_addr,
        "deviceConfigurationKeyValueListData", true, true, true, true);
    spine_device_add_feature_function(ent->device, &a->cfg_server_addr,
        "deviceConfigurationKeyValueDescriptionListData", true, true, false, false);

    spine_device_add_feature(ent->device, &a->diag_server_addr,
                             "DeviceDiagnosis", "server", cs_diag_handler, a);
    spine_device_add_feature_function(ent->device, &a->diag_server_addr,
        "deviceDiagnosisHeartbeatData", true, false, false, false);

    spine_device_add_feature(ent->device, &a->diag_client_addr,
                             "DeviceDiagnosis", "client", cs_diag_handler, a);

    spine_device_add_feature(ent->device, &a->ec_server_addr,
                             "ElectricalConnection", "server", cs_ec_handler, a);
    spine_device_add_feature_function(ent->device, &a->ec_server_addr,
        "electricalConnectionCharacteristicListData", true, true, false, false);
    spine_device_add_feature_function(ent->device, &a->ec_server_addr,
        "electricalConnectionDescriptionListData", true, true, false, false);

    /* Register one use case per enabled direction */
    if (lpc_cfg) cs_register_usecase(a, LC_DIRECTION_CONSUME);
    if (lpp_cfg) cs_register_usecase(a, LC_DIRECTION_PRODUCE);

    /* Discover the EG and start the heartbeat once it appears */
    spine_device_add_peer_ready_cb(ent->device, lc_cs_peer_ready, a);

    EEBUS_LOG_INFO(LOG_TAG, "CS entity created (lpc=%d lpp=%d)",
                   a->lpc.enabled, a->lpp.enabled);

    if (lpc_out) *lpc_out = a->lpc.enabled ? &a->lpc : NULL;
    if (lpp_out) *lpp_out = a->lpp.enabled ? &a->lpp : NULL;
    return SPINE_OK;
}

void uc_lc_cs_destroy(uc_lc_cs_t* cs) {
    if (!cs || !cs->entity) return;
    uc_lc_cs_entity_t* a = cs->entity;
    EEBUS_LOG_INFO(LOG_TAG, "CS destroyed");
    cs_dir_destroy_timers(&a->lpc);
    cs_dir_destroy_timers(&a->lpp);
    eebus_timer_destroy(a->hb_timer);
    eebus_timer_destroy(a->eg_hb_watchdog);
    eebus_mutex_destroy(a->mutex);
    EEBUS_FREE(a);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Operational API helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* Locate an entry in a merged list by its identifier. */
static lc_limit_data_t* cs_find_limit(uc_lc_cs_entity_t* a, uint32_t limit_id) {
    for (uint8_t i = 0; i < a->limit_list.count; i++)
        if (a->limit_list.items[i].limit_id == limit_id)
            return &a->limit_list.items[i];
    return NULL;
}

static dev_cfg_kv_data_t* cs_find_kv(uc_lc_cs_entity_t* a, uint32_t key_id) {
    for (uint8_t i = 0; i < a->failsafe_kv.count; i++)
        if (a->failsafe_kv.items[i].key_id == key_id)
            return &a->failsafe_kv.items[i];
    return NULL;
}

static ec_characteristic_data_t* cs_find_ec(uc_lc_cs_entity_t* a,
                                              uint32_t characteristic_id) {
    for (uint8_t i = 0; i < a->ec_chars.count; i++)
        if (a->ec_chars.items[i].characteristic_id == characteristic_id)
            return &a->ec_chars.items[i];
    return NULL;
}

/* Failsafe codec KV (value entry + shared duration entry) → UC. NULL-tolerant. */
static void failsafe_from_kv(const dev_cfg_kv_data_t* ve,
                              const dev_cfg_kv_data_t* de,
                              uc_lc_failsafe_t* out) {
    memset(out, 0, sizeof(*out));
    if (ve && ve->value_present && ve->value_type == DEV_CFG_VALUE_SCALED_NUM) {
        out->power_limit_w =
            spine_scaled_number_to_double(&ve->value.scaled_num_val);
        out->power_limit_changeable = ve->is_value_changeable;
    }
    if (de && de->value_present && de->value_type == DEV_CFG_VALUE_DURATION) {
        out->duration_s = uc_lc_parse_duration_s(de->value.duration_val);
        out->duration_changeable = de->is_value_changeable;
    }
}

/* Write UC failsafe into the value entry + shared duration entry. The entries'
 * key_id fields are left untouched (assigned at create). */
static void failsafe_to_kv(const uc_lc_failsafe_t* fs,
                            dev_cfg_kv_data_t* ve, dev_cfg_kv_data_t* de) {
    ve->value_type = DEV_CFG_VALUE_SCALED_NUM;
    spine_scaled_number_from_double(fs->power_limit_w, &ve->value.scaled_num_val);
    ve->value_present = true;
    ve->is_value_changeable = fs->power_limit_changeable;
    ve->is_value_changeable_present = true;

    de->value_type = DEV_CFG_VALUE_DURATION;
    snprintf(de->value.duration_val, sizeof(de->value.duration_val),
             "PT%uS", fs->duration_s);
    de->value_present = true;
    de->is_value_changeable = fs->duration_changeable;
    de->is_value_changeable_present = true;
}

/* Encode the changed entry/entries as a one/two-item list — the payload of a
 * partial notification. */
static cJSON* cs_encode_limit_partial(const lc_limit_data_t* e) {
    lc_limit_list_t t = {0};
    t.items[0] = *e;
    t.count = 1;
    return lc_limit_list_encode(&t);
}

static cJSON* cs_encode_failsafe_partial(const dev_cfg_kv_data_t* ve,
                                          const dev_cfg_kv_data_t* de) {
    dev_cfg_kv_list_t t = {0};
    t.items[0] = *ve;
    t.items[1] = *de;
    t.count = 2;
    return dev_cfg_kv_list_encode(&t);
}

static cJSON* cs_encode_ec_partial(const ec_characteristic_data_t* e) {
    ec_characteristic_list_t t = {0};
    t.items[0] = *e;
    t.count = 1;
    return ec_characteristic_list_encode(&t);
}

/* Send `data` as a (partial) notification on `addr`, then free `data`. */
static spine_err_t cs_notify(uc_lc_cs_entity_t* a, const spine_addr_t* addr,
                              const char* fn_name, cJSON* data, bool partial) {
    if (!data) return SPINE_ERR_NO_MEMORY;
    spine_msg_t ntf = {0};
    snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name), "%s", fn_name);
    ntf.payload.fn_data = data;
    ntf.payload.partial.present = partial;
    spine_err_t rc = spine_device_notify(a->ent_cfg.device, addr, &ntf);
    cJSON_Delete(data);
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Operational API
 * ════════════════════════════════════════════════════════════════════════════ */

void uc_lc_cs_get_limit(const uc_lc_cs_t* cs, uc_lc_limit_t* out) {
    if (!cs || !out) return;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    limit_from_codec(cs_find_limit(a, cs->limit_id), out);
    eebus_mutex_unlock(a->mutex);
}

spine_err_t uc_lc_cs_set_limit(uc_lc_cs_t* cs, const uc_lc_limit_t* in) {
    if (!cs || !in) return SPINE_ERR_INVALID;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    lc_limit_data_t* e = cs_find_limit(a, cs->limit_id);
    spine_err_t rc;
    if (e) {
        limit_to_codec(in, cs->limit_id, e);
        rc = cs_notify(a, &a->lc_server_addr, "loadControlLimitListData",
                       cs_encode_limit_partial(e), true);
    } else {
        rc = SPINE_ERR_INVALID;
    }
    eebus_mutex_unlock(a->mutex);
    return rc;
}

void uc_lc_cs_get_failsafe(const uc_lc_cs_t* cs, uc_lc_failsafe_t* out) {
    if (!cs || !out) return;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    failsafe_from_kv(cs_find_kv(a, cs->failsafe_value_key_id),
                     cs_find_kv(a, cs->failsafe_duration_key_id), out);
    eebus_mutex_unlock(a->mutex);
}

spine_err_t uc_lc_cs_set_failsafe(uc_lc_cs_t* cs, const uc_lc_failsafe_t* in) {
    if (!cs || !in) return SPINE_ERR_INVALID;
    uc_lc_cs_entity_t* a = cs->entity;
    uc_lc_failsafe_t fs = *in;
    if (fs.duration_s < 7200)  fs.duration_s = 7200;
    if (fs.duration_s > 86400) fs.duration_s = 86400;
    eebus_mutex_lock(a->mutex);
    dev_cfg_kv_data_t* ve = cs_find_kv(a, cs->failsafe_value_key_id);
    dev_cfg_kv_data_t* de = cs_find_kv(a, cs->failsafe_duration_key_id);
    spine_err_t rc;
    if (ve && de) {
        failsafe_to_kv(&fs, ve, de);
        rc = cs_notify(a, &a->cfg_server_addr,
                       "deviceConfigurationKeyValueListData",
                       cs_encode_failsafe_partial(ve, de), true);
    } else {
        rc = SPINE_ERR_INVALID;
    }
    eebus_mutex_unlock(a->mutex);
    return rc;
}

void uc_lc_cs_get_ec_characteristic(const uc_lc_cs_t* cs,
                                      uc_lc_ec_characteristic_t* out) {
    if (!cs || !out) return;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    ec_char_from_codec(cs_find_ec(a, cs->ec_char_id), out);
    eebus_mutex_unlock(a->mutex);
}

spine_err_t uc_lc_cs_set_ec_characteristic(uc_lc_cs_t* cs,
                                      const uc_lc_ec_characteristic_t* in) {
    if (!cs || !in || in->type == LC_POWER_MAX_NONE) return SPINE_ERR_INVALID;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    ec_characteristic_data_t* e = cs_find_ec(a, cs->ec_char_id);
    spine_err_t rc;
    if (e) {
        ec_char_to_codec(in, cs->direction, cs->ec_char_id, e);
        rc = cs_notify(a, &a->ec_server_addr,
                       "electricalConnectionCharacteristicListData",
                       cs_encode_ec_partial(e), true);
    } else {
        rc = SPINE_ERR_INVALID;   /* EC not declared at create time */
    }
    eebus_mutex_unlock(a->mutex);
    return rc;
}

lc_cs_state_t uc_lc_cs_get_state(const uc_lc_cs_t* cs) {
    if (!cs) return LC_STATE_INIT;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    lc_cs_state_t s = cs->state;
    eebus_mutex_unlock(a->mutex);
    return s;
}

bool uc_lc_cs_get_eg_heartbeat(const uc_lc_cs_t* cs, time_t* received_at,
                               bool* active) {
    if (!cs) return false;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    time_t t = a->eg_hb_received_time;
    if (received_at) *received_at = t;
    if (active)      *active = a->eg_hb_received;
    eebus_mutex_unlock(a->mutex);
    return t != 0;   /* 0 = never received (watchdog never clears the time) */
}

spine_err_t uc_lc_cs_set_available(uc_lc_cs_t* cs, bool available) {
    if (!cs) return SPINE_ERR_INVALID;
    const char* uc_name = (cs->direction == LC_DIRECTION_CONSUME)
        ? "limitationOfPowerConsumption" : "limitationOfPowerProduction";
    return spine_device_set_usecase_available(cs->entity->ent_cfg.device,
                                               uc_name, available);
}

/* ════════════════════════════════════════════════════════════════════════════
 * LoadControl handler — state machine helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* States in which a limit write requires a prior heartbeat ([LPC-916]). */
static bool cs_state_needs_hb(lc_cs_state_t s) {
    return s == LC_STATE_INIT || s == LC_STATE_FAILSAFE
        || s == LC_STATE_UNLIMITED_AUTONOMOUS;
}

/* Advance one direction's state machine on a limit write. `accepted` selects
 * the rule; touched directions transition whether accepted or not
 * ([LPC-918/919/920]). */
static void cs_advance_on_write(uc_lc_cs_t* cs, bool accepted,
                                 const uc_lc_limit_t* eff) {
    /* A limit write was received: cancel the INIT / no-write / failsafe timers. */
    if (cs->init_timer)         eebus_timer_stop(cs->init_timer);
    if (cs->limit_write_timer)  eebus_timer_stop(cs->limit_write_timer);
    if (cs->failsafe_dur_timer) eebus_timer_stop(cs->failsafe_dur_timer);
    lc_cs_state_t s = cs->state;
    if (accepted) {
        if (cs_state_needs_hb(s)) {
            cs->state = eff->is_limit_active ? LC_STATE_LIMITED
                                             : LC_STATE_UNLIMITED_CONTROLLED;
        } else if (s == LC_STATE_UNLIMITED_CONTROLLED) {
            if (eff->is_limit_active) cs->state = LC_STATE_LIMITED;
        } else if (s == LC_STATE_LIMITED) {
            if (!eff->is_limit_active) cs->state = LC_STATE_UNLIMITED_CONTROLLED;
        }
    } else if (cs_state_needs_hb(s)) {
        cs->state = LC_STATE_UNLIMITED_CONTROLLED;
    }
    if (cs->state != s)
        EEBUS_LOG_INFO(LOG_TAG, "CS[%s] state: %s -> %s",
            cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp",
            lc_state_str(s), lc_state_str(cs->state));
}

/* True if on_state_change should fire: a real transition, or a value/timePeriod
 * replacement while staying LIMITED. */
static bool cs_state_event(lc_cs_state_t old, lc_cs_state_t neu, bool accepted) {
    if (neu != old) return true;
    return accepted && old == LC_STATE_LIMITED;
}

/* Extract one direction's limit from a decoded effective list (NULL-tolerant:
 * a miss yields a zeroed result). */
static void cs_extract_limit(const lc_limit_list_t* list, uint32_t id,
                              uc_lc_limit_t* out) {
    for (uint8_t i = 0; i < list->count; i++)
        if (list->items[i].limit_id == id) {
            limit_from_codec(&list->items[i], out);
            return;
        }
    limit_from_codec(NULL, out);
}

/* Surgically commit one direction's effective limit into the merged cache —
 * the other direction's entry is never touched. */
static void cs_commit_limit(uc_lc_cs_entity_t* a, uc_lc_cs_t* cs,
                             const uc_lc_limit_t* eff) {
    lc_limit_data_t* e = cs_find_limit(a, cs->limit_id);
    if (e) limit_to_codec(eff, cs->limit_id, e);
    /* limit_duration_timer: arm on LIMITED + timePeriod endTime, else stop. */
    if (cs->state == LC_STATE_LIMITED && eff->time_period.end_time_present) {
        uint32_t ms = uc_lc_parse_time_period_ms(&eff->time_period);
        if (ms > 0) {
            if (!cs->limit_duration_timer)
                eebus_timer_create(&cs->limit_duration_timer,
                                    cs_limit_duration_cb, cs);
            eebus_timer_start(cs->limit_duration_timer, ms, false);
        }
    } else if (cs->limit_duration_timer) {
        eebus_timer_stop(cs->limit_duration_timer);
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * LoadControl handler — read / write
 * ════════════════════════════════════════════════════════════════════════════ */

static void cs_lc_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);
    const char* fn = msg->payload.fn_name;
    cJSON* data = NULL;
    const char* reply_fn = fn;
    bool reply_partial = false;

    if (!fn[0] || strcmp(fn, "loadControlLimitListData") == 0) {
        cJSON* full = lc_limit_list_encode(&a->limit_list);
        if (msg->payload.partial.present) {
            data = spine_rfe_filter_read(full, "limitId",
                msg->payload.partial.selector, msg->payload.partial.elements);
            cJSON_Delete(full);
            reply_partial = true;
        } else {
            data = full;
        }
        reply_fn = "loadControlLimitListData";
    } else if (strcmp(fn, "loadControlLimitDescriptionListData") == 0) {
        cJSON* full = lc_limit_desc_list_encode(&a->limit_descs);
        if (msg->payload.partial.present) {
            data = spine_rfe_filter_read(full, "limitId",
                msg->payload.partial.selector, msg->payload.partial.elements);
            cJSON_Delete(full);
            reply_partial = true;
        } else {
            data = full;
        }
        reply_fn = "loadControlLimitDescriptionListData";
    }

    if (data) {
        spine_msg_t reply;
        spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                           spine_device_next_msg_counter(dev), reply_fn, data);
        if (reply_partial) reply.payload.partial.present = true;
        spine_device_send(dev, &reply);
        cJSON_Delete(data);
    }
    eebus_mutex_unlock(a->mutex);
}

static void cs_lc_handle_write(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                 const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);

    /* 1. RFE apply (full or partial routed uniformly) + touched ids */
    spine_rfe_touched_t touched;
    cJSON* existing = lc_limit_list_encode(&a->limit_list);
    cJSON* eff = spine_rfe_apply_with_touched(
        existing, msg->payload.fn_data, "limitId",
        msg->payload.partial.present ? &msg->payload.partial : NULL,
        msg->payload.delete_.present ? &msg->payload.delete_ : NULL,
        &touched);
    cJSON_Delete(existing);
    lc_limit_list_t eff_list;
    if (!eff || lc_limit_list_decode(eff, &eff_list) != SPINE_OK) {
        if (eff) cJSON_Delete(eff);
        EEBUS_LOG_WARN(LOG_TAG, "%s LC write: apply/decode failed",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_GENERAL_ERROR, "apply/decode failed");
        eebus_mutex_unlock(a->mutex);
        return;
    }
    cJSON_Delete(eff);

    /* 2. Classify touched ids into directions */
    bool lpc_touched = false, lpp_touched = false, unknown = false;
    for (uint8_t i = 0; i < touched.count; i++) {
        int64_t id = touched.ids[i];
        if (a->lpc.enabled && id == (int64_t)a->lpc.limit_id)      lpc_touched = true;
        else if (a->lpp.enabled && id == (int64_t)a->lpp.limit_id) lpp_touched = true;
        else                                                       unknown = true;
    }
    if (touched.overflow) {
        EEBUS_LOG_WARN(LOG_TAG, "%s LC write: too many touched limitIds",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_GENERAL_ERROR, "too many touched limitIds");
        eebus_mutex_unlock(a->mutex);
        return;
    }
    if (!lpc_touched && !lpp_touched) {
        EEBUS_LOG_WARN(LOG_TAG, "%s LC write rejected: no applicable limitId",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_CMD_REJECTED, "no applicable limitId");
        eebus_mutex_unlock(a->mutex);
        return;
    }

    /* 3. Heartbeat precondition [LPC-916] — only for touched directions
     *    currently in INIT / FAILSAFE / UNLIMITED_AUTONOMOUS. */
    bool need_hb = (lpc_touched && cs_state_needs_hb(a->lpc.state))
                || (lpp_touched && cs_state_needs_hb(a->lpp.state));
    if (need_hb) {
        if (!a->eg_hb_received) {
            EEBUS_LOG_WARN(LOG_TAG, "%s LC write rejected: no prior heartbeat",
                           spine_device_get_session_id(dev));
            spine_device_send_result(dev, &msg->header,
                SPINE_RESULT_CMD_REJECTED, "no prior heartbeat");
            eebus_mutex_unlock(a->mutex);
            return;
        }
        if ((time(NULL) - a->eg_hb_received_time) > 60) {
            EEBUS_LOG_WARN(LOG_TAG,
                "%s LC write rejected: write too late after heartbeat",
                spine_device_get_session_id(dev));
            spine_device_send_result(dev, &msg->header,
                SPINE_RESULT_CMD_REJECTED, "write too late after heartbeat");
            eebus_mutex_unlock(a->mutex);
            return;
        }
    }

    /* 4. Extract each touched direction's effective limit */
    uc_lc_limit_t lpc_eff = {0}, lpp_eff = {0};
    if (lpc_touched) cs_extract_limit(&eff_list, a->lpc.limit_id, &lpc_eff);
    if (lpp_touched) cs_extract_limit(&eff_list, a->lpp.limit_id, &lpp_eff);

    /* 5. SDK direction-sign validation (consume >= 0, produce <= 0) */
    bool sign_ok = true;
    if (lpc_touched && lpc_eff.value_w < 0.0) sign_ok = false;
    if (lpp_touched && lpp_eff.value_w > 0.0) sign_ok = false;

    /* 6. Application callback — once, atomic (without the mutex held) */
    bool accepted;
    const char* reject_reason = "rejected by application";
    if (unknown) {
        accepted = false;
        reject_reason = "unknown limitId";
    } else if (!sign_ok) {
        accepted = false;
        reject_reason = "invalid limit direction";
    } else if (!a->ent_cfg.on_limit_write) {
        accepted = false;
        reject_reason = "no limit write handler";
    } else {
        eebus_mutex_unlock(a->mutex);
        accepted = a->ent_cfg.on_limit_write(
            lpc_touched ? &lpc_eff : NULL,
            lpp_touched ? &lpp_eff : NULL,
            a->ent_cfg.userdata);
        eebus_mutex_lock(a->mutex);
    }

    /* 7. State machine — per touched direction, accepted or not */
    lc_cs_state_t lpc_old = a->lpc.state, lpp_old = a->lpp.state;
    if (lpc_touched) cs_advance_on_write(&a->lpc, accepted, &lpc_eff);
    if (lpp_touched) cs_advance_on_write(&a->lpp, accepted, &lpp_eff);

    /* 8. Commit (accepted only; surgical per-direction update) */
    if (accepted) {
        if (lpc_touched) cs_commit_limit(a, &a->lpc, &lpc_eff);
        if (lpp_touched) cs_commit_limit(a, &a->lpp, &lpp_eff);
    }

    /* 9. Result + notify */
    if (accepted) {
        EEBUS_LOG_DEBUG(LOG_TAG, "%s LC write accepted (lpc=%d lpp=%d)",
                        spine_device_get_session_id(dev),
                        lpc_touched, lpp_touched);
        spine_device_send_result(dev, &msg->header, SPINE_RESULT_NO_ERROR, "");
        spine_device_notify(a->ent_cfg.device, &a->lc_server_addr, msg);
    } else {
        EEBUS_LOG_WARN(LOG_TAG, "%s LC write rejected: %s",
                       spine_device_get_session_id(dev), reject_reason);
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_CMD_REJECTED, reject_reason);
    }

    /* 10. on_state_change — fire per changed direction after unlocking */
    bool lpc_evt = lpc_touched && cs_state_event(lpc_old, a->lpc.state, accepted);
    bool lpp_evt = lpp_touched && cs_state_event(lpp_old, a->lpp.state, accepted);
    lc_cs_state_t lpc_new = a->lpc.state, lpp_new = a->lpp.state;
    eebus_mutex_unlock(a->mutex);

    if (lpc_evt && a->lpc.on_state_change)
        a->lpc.on_state_change(&a->lpc, lpc_old, lpc_new, a->ent_cfg.userdata);
    if (lpp_evt && a->lpp.on_state_change)
        a->lpp.on_state_change(&a->lpp, lpp_old, lpp_new, a->ent_cfg.userdata);
}

/* ════════════════════════════════════════════════════════════════════════════
 * DeviceConfiguration (failsafe) handler — helpers
 * ════════════════════════════════════════════════════════════════════════════ */

/* States in which a failsafe write is permitted ([LPC-003/2]). */
static bool cs_state_allows_failsafe(lc_cs_state_t s) {
    return s == LC_STATE_LIMITED || s == LC_STATE_UNLIMITED_CONTROLLED;
}

/* Extract a direction's effective failsafe: prefer the just-written entry from
 * `eff`, fall back to the current cache for fields the write omitted (handles
 * a full write that carries only a subset of keys). */
static void cs_extract_failsafe(uc_lc_cs_entity_t* a,
                                  const dev_cfg_kv_list_t* eff,
                                  const uc_lc_cs_t* cs, uc_lc_failsafe_t* out) {
    const dev_cfg_kv_data_t* ve = NULL;
    const dev_cfg_kv_data_t* de = NULL;
    for (uint8_t i = 0; i < eff->count; i++) {
        if (eff->items[i].key_id == cs->failsafe_value_key_id)
            ve = &eff->items[i];
        else if (eff->items[i].key_id == cs->failsafe_duration_key_id)
            de = &eff->items[i];
    }
    if (!ve) ve = cs_find_kv(a, cs->failsafe_value_key_id);
    if (!de) de = cs_find_kv(a, cs->failsafe_duration_key_id);
    failsafe_from_kv(ve, de, out);
}

/* Surgically commit one direction's failsafe (value entry + shared duration
 * entry) into the merged cache. */
static void cs_commit_failsafe(uc_lc_cs_entity_t* a, const uc_lc_cs_t* cs,
                                const uc_lc_failsafe_t* fs) {
    dev_cfg_kv_data_t* ve = cs_find_kv(a, cs->failsafe_value_key_id);
    dev_cfg_kv_data_t* de = cs_find_kv(a, cs->failsafe_duration_key_id);
    if (ve && de) failsafe_to_kv(fs, ve, de);
}

/* ════════════════════════════════════════════════════════════════════════════
 * DeviceConfiguration (failsafe) handler — read / write
 * ════════════════════════════════════════════════════════════════════════════ */

static void cs_cfg_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                 const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);
    const char* fn = msg->payload.fn_name;
    cJSON* data = NULL;
    const char* reply_fn = fn;
    bool reply_partial = false;

    if (!fn[0] || strcmp(fn, "deviceConfigurationKeyValueListData") == 0) {
        cJSON* full = dev_cfg_kv_list_encode(&a->failsafe_kv);
        if (msg->payload.partial.present) {
            data = spine_rfe_filter_read(full, "keyId",
                msg->payload.partial.selector, msg->payload.partial.elements);
            cJSON_Delete(full);
            reply_partial = true;
        } else {
            data = full;
        }
        reply_fn = "deviceConfigurationKeyValueListData";
    } else if (strcmp(fn, "deviceConfigurationKeyValueDescriptionListData") == 0) {
        cJSON* full = dev_cfg_kv_desc_list_encode(&a->cfg_descs);
        if (msg->payload.partial.present) {
            data = spine_rfe_filter_read(full, "keyId",
                msg->payload.partial.selector, msg->payload.partial.elements);
            cJSON_Delete(full);
            reply_partial = true;
        } else {
            data = full;
        }
        reply_fn = "deviceConfigurationKeyValueDescriptionListData";
    }

    if (data) {
        spine_msg_t reply;
        spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                           spine_device_next_msg_counter(dev), reply_fn, data);
        if (reply_partial) reply.payload.partial.present = true;
        spine_device_send(dev, &reply);
        cJSON_Delete(data);
    }
    eebus_mutex_unlock(a->mutex);
}

static void cs_cfg_handle_write(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                  const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);

    /* 1. Reject delete operations (failsafe KV has no deletable elements) */
    if (msg->payload.delete_.present) {
        EEBUS_LOG_WARN(LOG_TAG, "%s failsafe write: delete not supported",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_CMD_REJECTED, "delete not supported for failsafe");
        eebus_mutex_unlock(a->mutex);
        return;
    }

    /* 2. RFE apply (full or partial routed uniformly) + touched keyIds */
    spine_rfe_touched_t touched;
    cJSON* existing = dev_cfg_kv_list_encode(&a->failsafe_kv);
    cJSON* eff = spine_rfe_apply_with_touched(
        existing, msg->payload.fn_data, "keyId",
        msg->payload.partial.present ? &msg->payload.partial : NULL,
        NULL, &touched);
    cJSON_Delete(existing);
    dev_cfg_kv_list_t eff_kv;
    if (!eff || dev_cfg_kv_list_decode(eff, &eff_kv) != SPINE_OK) {
        if (eff) cJSON_Delete(eff);
        EEBUS_LOG_WARN(LOG_TAG, "%s failsafe write: apply/decode failed",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_GENERAL_ERROR, "apply/decode failed");
        eebus_mutex_unlock(a->mutex);
        return;
    }
    cJSON_Delete(eff);

    /* 3. Classify touched keyIds. A touched shared-duration key marks both
     *    directions (the duration is part of each direction's failsafe). */
    uint32_t dur_key = a->lpc.enabled ? a->lpc.failsafe_duration_key_id
                                      : a->lpp.failsafe_duration_key_id;
    bool lpc_touched = false, lpp_touched = false;
    bool unknown = false, dur_touched = false;
    for (uint8_t i = 0; i < touched.count; i++) {
        int64_t id = touched.ids[i];
        if (a->lpc.enabled && id == (int64_t)a->lpc.failsafe_value_key_id)
            lpc_touched = true;
        else if (a->lpp.enabled && id == (int64_t)a->lpp.failsafe_value_key_id)
            lpp_touched = true;
        else if (id == (int64_t)dur_key)
            dur_touched = true;
        else
            unknown = true;
    }
    if (dur_touched) {
        if (a->lpc.enabled) lpc_touched = true;
        if (a->lpp.enabled) lpp_touched = true;
    }
    if (touched.overflow) {
        EEBUS_LOG_WARN(LOG_TAG, "%s failsafe write: too many touched keyIds",
                       spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_GENERAL_ERROR, "too many touched keyIds");
        eebus_mutex_unlock(a->mutex);
        return;
    }
    if (!lpc_touched && !lpp_touched) {
        EEBUS_LOG_WARN(LOG_TAG,
            "%s failsafe write rejected: no applicable keyId",
            spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_CMD_REJECTED, "no applicable keyId");
        eebus_mutex_unlock(a->mutex);
        return;
    }

    /* 4. [LPC-003/2] state precondition: each touched direction must be in
     *    LIMITED or UNLIMITED_CONTROLLED. */
    if ((lpc_touched && !cs_state_allows_failsafe(a->lpc.state)) ||
        (lpp_touched && !cs_state_allows_failsafe(a->lpp.state))) {
        EEBUS_LOG_WARN(LOG_TAG,
            "%s failsafe write rejected: not in limited/unlimited-controlled",
            spine_device_get_session_id(dev));
        spine_device_send_result(dev, &msg->header, SPINE_RESULT_CMD_REJECTED,
            "failsafe write allowed only in limited/unlimited-controlled");
        eebus_mutex_unlock(a->mutex);
        return;
    }

    /* 5. Extract each touched direction's effective failsafe */
    uc_lc_failsafe_t lpc_fs = {0}, lpp_fs = {0};
    if (lpc_touched) cs_extract_failsafe(a, &eff_kv, &a->lpc, &lpc_fs);
    if (lpp_touched) cs_extract_failsafe(a, &eff_kv, &a->lpp, &lpp_fs);

    /* 6. Range checks: [LPC-022] duration >= 7200 (reject if below);
     *    duration > 86400 is clamped; [LPC-021] power >= 0 (reject if not). */
    bool ok = true;
    if (lpc_touched) {
        if (lpc_fs.duration_s < 7200)  ok = false;
        if (lpc_fs.duration_s > 86400) lpc_fs.duration_s = 86400;
        if (lpc_fs.power_limit_w < 0.0) ok = false;
    }
    if (lpp_touched) {
        if (lpp_fs.duration_s < 7200)  ok = false;
        if (lpp_fs.duration_s > 86400) lpp_fs.duration_s = 86400;
        if (lpp_fs.power_limit_w < 0.0) ok = false;
    }

    /* 7. Application callback — once, atomic (without the mutex held) */
    bool accepted;
    const char* reject_reason = "rejected by application";
    if (unknown) {
        accepted = false;
        reject_reason = "unknown keyId";
    } else if (!ok) {
        accepted = false;
        reject_reason = "failsafe out of range (power >= 0 and duration >= 7200 required)";
    } else if (!a->ent_cfg.on_failsafe_write) {
        accepted = false;
        reject_reason = "no failsafe write handler";
    } else {
        eebus_mutex_unlock(a->mutex);
        accepted = a->ent_cfg.on_failsafe_write(
            lpc_touched ? &lpc_fs : NULL,
            lpp_touched ? &lpp_fs : NULL,
            a->ent_cfg.userdata);
        eebus_mutex_lock(a->mutex);
    }

    /* 8. Commit (accepted only; surgical per-direction update) */
    if (accepted) {
        if (lpc_touched) cs_commit_failsafe(a, &a->lpc, &lpc_fs);
        if (lpp_touched) cs_commit_failsafe(a, &a->lpp, &lpp_fs);
    }

    /* 9. Result + notify */
    if (accepted) {
        EEBUS_LOG_DEBUG(LOG_TAG, "%s failsafe write accepted (lpc=%d lpp=%d)",
                        spine_device_get_session_id(dev),
                        lpc_touched, lpp_touched);
        spine_device_send_result(dev, &msg->header, SPINE_RESULT_NO_ERROR, "");
        spine_device_notify(a->ent_cfg.device, &a->cfg_server_addr, msg);
    } else {
        EEBUS_LOG_WARN(LOG_TAG, "%s failsafe write rejected: %s",
                       spine_device_get_session_id(dev), reject_reason);
        spine_device_send_result(dev, &msg->header,
            SPINE_RESULT_CMD_REJECTED, reject_reason);
    }
    eebus_mutex_unlock(a->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Heartbeat / timers / peer-ready
 * ════════════════════════════════════════════════════════════════════════════ */

static void fill_utc_timestamp(char* buf, size_t sz) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

static const char* lc_state_str(lc_cs_state_t s) {
    switch (s) {
    case LC_STATE_INIT:                 return "init";
    case LC_STATE_UNLIMITED_CONTROLLED: return "unlimited/controlled";
    case LC_STATE_LIMITED:              return "limited";
    case LC_STATE_FAILSAFE:             return "failsafe";
    case LC_STATE_UNLIMITED_AUTONOMOUS: return "unlimited/autonomous";
    }
    return "unknown";
}

/* Transition one direction's state and fire on_state_change. Must be called
 * with the mutex held; unlocks for the callback and relocks. */
static void cs_set_state(uc_lc_cs_t* cs, lc_cs_state_t new_state) {
    if (cs->state == new_state) return;
    lc_cs_state_t old = cs->state;
    cs->state = new_state;
    EEBUS_LOG_INFO(LOG_TAG, "CS[%s] state: %s -> %s",
                   cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp",
                   lc_state_str(old), lc_state_str(new_state));
    if (cs->on_state_change) {
        eebus_mutex_unlock(cs->entity->mutex);
        cs->on_state_change(cs, old, new_state, cs->entity->ent_cfg.userdata);
        eebus_mutex_lock(cs->entity->mutex);
    }
}

/* INIT timeout (120s, no heartbeat+limit): INIT → UNLIMITED_AUTONOMOUS [LPC-906] */
static void cs_init_timer_cb(void* ud) {
    uc_lc_cs_t* cs = (uc_lc_cs_t*)ud;
    eebus_mutex_lock(cs->entity->mutex);
    if (cs->state == LC_STATE_INIT) {
        EEBUS_LOG_WARN(LOG_TAG, "CS[%s] init timeout (120s): no heartbeat+limit",
            cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp");
        cs_set_state(cs, LC_STATE_UNLIMITED_AUTONOMOUS);
    }
    eebus_mutex_unlock(cs->entity->mutex);
}

/* Failsafe duration elapsed: FAILSAFE → UNLIMITED_AUTONOMOUS [LPC-922] */
static void cs_failsafe_dur_cb(void* ud) {
    uc_lc_cs_t* cs = (uc_lc_cs_t*)ud;
    eebus_mutex_lock(cs->entity->mutex);
    if (cs->state == LC_STATE_FAILSAFE) {
        EEBUS_LOG_INFO(LOG_TAG, "CS[%s] failsafe duration elapsed",
            cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp");
        cs_set_state(cs, LC_STATE_UNLIMITED_AUTONOMOUS);
        if (cs->limit_write_timer) eebus_timer_stop(cs->limit_write_timer);
    }
    eebus_mutex_unlock(cs->entity->mutex);
}

/* Heartbeat resumed in FAILSAFE but no limit write within 120s:
 * FAILSAFE → UNLIMITED_AUTONOMOUS [LPC-921] */
static void cs_limit_write_timer_cb(void* ud) {
    uc_lc_cs_t* cs = (uc_lc_cs_t*)ud;
    eebus_mutex_lock(cs->entity->mutex);
    if (cs->state == LC_STATE_FAILSAFE) {
        EEBUS_LOG_WARN(LOG_TAG,
            "CS[%s] no limit write within 120s after heartbeat",
            cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp");
        cs_set_state(cs, LC_STATE_UNLIMITED_AUTONOMOUS);
        if (cs->failsafe_dur_timer) eebus_timer_stop(cs->failsafe_dur_timer);
    }
    eebus_mutex_unlock(cs->entity->mutex);
}

/* Limit timePeriod elapsed: LIMITED → UNLIMITED_CONTROLLED [LPC-908] */
static void cs_limit_duration_cb(void* ud) {
    uc_lc_cs_t* cs = (uc_lc_cs_t*)ud;
    uc_lc_cs_entity_t* a = cs->entity;
    eebus_mutex_lock(a->mutex);
    if (cs->state == LC_STATE_LIMITED) {
        EEBUS_LOG_INFO(LOG_TAG,
            "CS[%s] limit timePeriod elapsed, limit deactivated",
            cs->direction == LC_DIRECTION_CONSUME ? "lpc" : "lpp");
        lc_limit_data_t* e = cs_find_limit(a, cs->limit_id);
        cJSON* data = NULL;
        if (e) {
            e->is_limit_active = false;
            e->is_limit_active_present = true;
            memset(&e->time_period, 0, sizeof(spine_time_period_t));
            data = cs_encode_limit_partial(e);
        }
        cs_set_state(cs, LC_STATE_UNLIMITED_CONTROLLED);
        if (data)
            cs_notify(a, &a->lc_server_addr,
                      "loadControlLimitListData", data, true);
    }
    eebus_mutex_unlock(a->mutex);
}

/* If `cs` is an enabled direction currently in LIMITED or UNLIMITED_CONTROLLED,
 * transition it to FAILSAFE: stop its limit-duration timer, apply the failsafe
 * limit (inactive, value = failsafe power) and arm its failsafe-duration timer.
 * Otherwise does nothing. Must hold the mutex; returns a partial-notify payload
 * for the caller to send after unlocking, or NULL if no transition happened. */
static cJSON* cs_enter_failsafe(uc_lc_cs_entity_t* a, uc_lc_cs_t* cs) {
    if (!cs->enabled) return NULL;
    if (cs->state != LC_STATE_LIMITED &&
        cs->state != LC_STATE_UNLIMITED_CONTROLLED)
        return NULL;

    if (cs->limit_duration_timer)
        eebus_timer_stop(cs->limit_duration_timer);
    cs_set_state(cs, LC_STATE_FAILSAFE);

    uc_lc_failsafe_t fs;
    failsafe_from_kv(cs_find_kv(a, cs->failsafe_value_key_id),
                     cs_find_kv(a, cs->failsafe_duration_key_id), &fs);

    /* Deactivate the EG's limit in place (value left untouched), consistent
     * with cs_limit_duration_cb. The failsafe magnitude lives in DeviceConfig. */
    lc_limit_data_t* e = cs_find_limit(a, cs->limit_id);
    cJSON* data = NULL;
    if (e) {
        e->is_limit_active = false;
        e->is_limit_active_present = true;
        memset(&e->time_period, 0, sizeof(spine_time_period_t));
        data = cs_encode_limit_partial(e);
    }

    if (!cs->failsafe_dur_timer)
        eebus_timer_create(&cs->failsafe_dur_timer, cs_failsafe_dur_cb, cs);
    eebus_timer_start(cs->failsafe_dur_timer, fs.duration_s * 1000, false);
    return data;
}

/* EG heartbeat watchdog (120s) elapsed: every enabled direction in
 * LIMITED / UNLIMITED_CONTROLLED → FAILSAFE [LPC-911/912]. */
static void cs_hb_watchdog_cb(void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    eebus_mutex_lock(a->mutex);
    a->eg_hb_received = false;
    EEBUS_LOG_WARN(LOG_TAG, "EG heartbeat timeout (120s)");
    cJSON* lpc_data = cs_enter_failsafe(a, &a->lpc);
    cJSON* lpp_data = cs_enter_failsafe(a, &a->lpp);
    if (lpc_data)
        cs_notify(a, &a->lc_server_addr, "loadControlLimitListData", lpc_data, true);
    if (lpp_data)
        cs_notify(a, &a->lc_server_addr, "loadControlLimitListData", lpp_data, true);
    eebus_mutex_unlock(a->mutex);
}

/* Periodic: the CS emits its own heartbeat. */
static void cs_hb_timer_cb(void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;
    eebus_mutex_lock(a->mutex);
    a->hb_counter++;
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    fill_utc_timestamp(hb.timestamp, sizeof(hb.timestamp));
    hb.timestamp_present = true;
    hb.heartbeat_counter = a->hb_counter;
    hb.heartbeat_counter_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout),
             "PT%uS", a->ent_cfg.heartbeat_interval_s);
    hb.heartbeat_timeout_present = true;
    cJSON* data = dev_diag_heartbeat_encode(&hb);

    if (data) {
        spine_msg_t ntf = {0};
        snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name),
                 "deviceDiagnosisHeartbeatData");
        ntf.payload.fn_data = data;
        spine_device_notify(a->ent_cfg.device, &a->diag_server_addr, &ntf);
        cJSON_Delete(data);
        EEBUS_LOG_DEBUG(LOG_TAG, "CS heartbeat #%llu sent",
                        (unsigned long long)a->hb_counter);
    }
    eebus_mutex_unlock(a->mutex);
}

/* DeviceDiagnosis READ: respond with the CS's own heartbeat. */
static void cs_diag_handle_read(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                  const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    fill_utc_timestamp(hb.timestamp, sizeof(hb.timestamp));
    hb.timestamp_present = true;
    hb.heartbeat_counter = a->hb_counter;
    hb.heartbeat_counter_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout),
             "PT%uS", a->ent_cfg.heartbeat_interval_s);
    hb.heartbeat_timeout_present = true;
    cJSON* data = dev_diag_heartbeat_encode(&hb);

    if (data) {
        spine_msg_t reply;
        spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                           spine_device_next_msg_counter(dev),
                           "deviceDiagnosisHeartbeatData", data);
        spine_device_send(dev, &reply);
        cJSON_Delete(data);
    }
    eebus_mutex_unlock(a->mutex);
}

/* DeviceDiagnosis NOTIFY: an EG heartbeat arrived. */
static void cs_diag_handle_notify(uc_lc_cs_entity_t* a, spine_device_t* dev,
                                    const spine_msg_t* msg) {
    eebus_mutex_lock(a->mutex);

    /* Only accept heartbeats from an EG bound to our LoadControl server. */
    if (!spine_device_has_binding_to_server(dev, &a->lc_server_addr)) {
        EEBUS_LOG_WARN(LOG_TAG,
            "%s heartbeat ignored: no binding to LC server",
            spine_device_get_session_id(dev));
        eebus_mutex_unlock(a->mutex);
        return;
    }
    dev_diag_heartbeat_t hb;
    if (dev_diag_heartbeat_decode(msg->payload.fn_data, &hb) != SPINE_OK) {
        EEBUS_LOG_WARN(LOG_TAG, "%s EG heartbeat decode failed",
                       spine_device_get_session_id(dev));
        eebus_mutex_unlock(a->mutex);
        return;
    }

    a->eg_hb_received = true;
    a->eg_hb_received_time = time(NULL);
    EEBUS_LOG_DEBUG(LOG_TAG, "%s EG heartbeat #%llu",
                    spine_device_get_session_id(dev),
                    (unsigned long long)hb.heartbeat_counter);

    /* (Re)arm the shared 120s heartbeat watchdog. */
    if (!a->eg_hb_watchdog)
        eebus_timer_create(&a->eg_hb_watchdog, cs_hb_watchdog_cb, a);
    eebus_timer_start(a->eg_hb_watchdog, 120000, false);

    /* Any direction still in FAILSAFE: arm its 120s no-write timer [LPC-921]. */
    for (int i = 0; i < 2; i++) {
        uc_lc_cs_t* cs = (i == 0) ? &a->lpc : &a->lpp;
        if (cs->enabled && cs->state == LC_STATE_FAILSAFE) {
            if (!cs->limit_write_timer)
                eebus_timer_create(&cs->limit_write_timer,
                                    cs_limit_write_timer_cb, cs);
            eebus_timer_start(cs->limit_write_timer, 120000, false);
        }
    }
    eebus_mutex_unlock(a->mutex);
}

/* Node-management peer-ready: an EG appeared. */
static void lc_cs_peer_ready(spine_device_t* remote_dev,
                               const spine_nm_uc_entry_t* uc_infos,
                               uint8_t uc_count, void* ud) {
    uc_lc_cs_entity_t* a = (uc_lc_cs_entity_t*)ud;

    /* 1. Scan the EG's LoadControl use cases. A direction counts only if the
     *    EG declares it AND this CS has it enabled. */
    uc_lc_eg_dir_t eg_support = 0, eg_available = 0;
    const spine_nm_uc_entry_t* eg_uc = NULL;
    for (uint8_t i = 0; i < uc_count; i++) {
        if (strcmp(uc_infos[i].actor, "EnergyGuard") != 0) continue;
        uc_lc_eg_dir_t bit = 0;
        if (strcmp(uc_infos[i].use_case_name,
                   "limitationOfPowerConsumption") == 0 && a->lpc.enabled)
            bit = UC_LC_EG_DIR_LPC;
        else if (strcmp(uc_infos[i].use_case_name,
                        "limitationOfPowerProduction") == 0 && a->lpp.enabled)
            bit = UC_LC_EG_DIR_LPP;
        else
            continue;
        eg_support |= bit;
        if (uc_infos[i].available) eg_available |= bit;
        if (!eg_uc) eg_uc = &uc_infos[i];
    }
    if (!eg_uc) return;   /* no direction both ends agree on */

    /* 2. Subscribe to the EG's DeviceDiagnosis server and read its heartbeat. */
    spine_addr_t eg_diag = {0};
    bool found = false;
    for (uint8_t i = 0; i < spine_device_feature_count(remote_dev); i++) {
        const spine_feature_info_t* f = spine_device_get_feature(remote_dev, i);
        if (!f || strcmp(f->role, "server") != 0) continue;
        if (!spine_addr_entity_equal(&f->addr, &eg_uc->address)) continue;
        if (strcmp(f->feature_type, "DeviceDiagnosis") == 0) {
            eg_diag = f->addr;
            found = true;
            break;
        }
    }
    if (found) {
        spine_device_subscribe(remote_dev, &a->diag_client_addr, &eg_diag);
        spine_msg_t req;
        spine_make_msg_req(&req, &a->diag_client_addr, &eg_diag, SPINE_CMD_READ,
                           spine_device_next_msg_counter(remote_dev),
                           false, "deviceDiagnosisHeartbeatData", NULL);
        spine_device_send(remote_dev, &req);
    }

    /* 3. Start the CS heartbeat timer (shared, once). */
    eebus_mutex_lock(a->mutex);
    if (!a->hb_timer) {
        eebus_timer_create(&a->hb_timer, cs_hb_timer_cb, a);
        eebus_timer_start(a->hb_timer,
                          a->ent_cfg.heartbeat_interval_s * 1000, true);
    }
    eebus_mutex_unlock(a->mutex);

    /* 4. Notify the application. */
    EEBUS_LOG_INFO(LOG_TAG, "%s EG ready (support=0x%x available=0x%x)",
                   spine_device_get_session_id(remote_dev),
                   (unsigned)eg_support, (unsigned)eg_available);
    if (a->ent_cfg.on_eg_ready) {
        a->ent_cfg.on_eg_ready(
            spine_device_get_session_id(remote_dev),
            spine_device_get_entity_type(remote_dev, &eg_uc->address),
            eg_support, eg_available, a->ent_cfg.userdata);
    }
}
