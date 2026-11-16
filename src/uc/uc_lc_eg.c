// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc_eg.c — Generic LoadControl EG role implementation.
 *
 * Merges LPC EG and LPP EG into a single implementation.
 * One EG instance handles both directions if enabled.
 * Data is cached per remote CS in spine_device cache slots.
 */

#include "uc_lc_eg.h"
#include "../spine/codec/spine_codec_load_ctrl.h"
#include "../spine/codec/spine_codec_dev_cfg.h"
#include "../spine/codec/spine_codec_dev_diag.h"
#include "../spine/codec/spine_codec_elec_conn.h"
#include "../spine/codec/spine_codec_result.h"
#include "../spine/codec/spine_codec_msg.h"
#include "../spine/codec/spine_rfe.h"
#include "../log/eebus_log.h"
#include "../common/eebus_memory.h"
#include "../platform/eebus_mutex.h"
#include "../platform/eebus_timer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define LOG_TAG "uc.lc.eg"

static void fill_utc_timestamp(char* buf, size_t sz) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &utc);
}


/* ── Init-read completion bitmask ────────────────────────────────────────── */

#define READ_BIT_LIMIT_DESC  0x01
#define READ_BIT_LIMIT_DATA  0x02
#define READ_BIT_CFG_DESC    0x04
#define READ_BIT_CFG_DATA    0x08
#define READ_BIT_DIAG_HB     0x10
#define READ_BIT_EC_CHAR     0x20
#define READ_ALL_DONE        0x3F

/* ── Per-remote-CS cache (stored in SPINE_CACHE_LC_EG slot) ──────────────── */

typedef struct {
    eebus_mutex_t*           mutex;

    /* Remote CS UC info */
    spine_addr_t             cs_entity_addr;
    char                     cs_entity_type[64];
    spine_addr_t             cs_lc_addr;
    spine_addr_t             cs_cfg_addr;
    spine_addr_t             cs_diag_addr;
    spine_addr_t             cs_ec_addr;
    bool                     lpc_supported;
    bool                     lpp_supported;
    bool                     lpc_available;
    bool                     lpp_available;

    /* Scenario 1: limit data */
    lc_limit_desc_list_t     limit_descs;
    lc_limit_list_t          limits;

    /* Scenario 2: failsafe data (key_id resolved from cfg_descs at runtime) */
    dev_cfg_kv_desc_list_t   cfg_descs;
    dev_cfg_kv_list_t        failsafe_kv;

    /* Scenario 3: CS heartbeat */
    uint64_t                 cs_hb_counter;

    /* Scenario 4: EC characteristics */
    ec_characteristic_list_t ec_chars;

    /* Init-scenario read completion tracking (6 bits) */
    uint8_t                  init_read_done;
} lc_eg_cache_t;

/* ── EG-side write retry: pending table ──────────────────────────────────── */

typedef enum {
    EG_PENDING_LIMIT,
    EG_PENDING_FS_POWER,
    EG_PENDING_FS_DURATION,
} eg_pending_kind_t;

#define EG_PENDING_MAX      64
#define EG_MAX_RETRY         3
#define EG_SESSION_ID_MAX   64    /* matches spine_device session_id */

typedef struct {
    eg_pending_kind_t kind;
    bool              is_lpc;        /* FS_DURATION 时存 false 占位 */
    char              session_id[EG_SESSION_ID_MAX];
    uint64_t          msg_counter;
    time_t            sent_at;
    uint8_t           retry_count;
    union {
        uc_lc_limit_t limit;
        double        fs_power_w;
        uint32_t      fs_duration_s;
    } payload;
} eg_pending_t;

/* ── struct ──────────────────────────────────────────────────────────────── */

struct uc_lc_eg {
    uc_lc_eg_config_t cfg;
    bool              lpc_enabled;
    bool              lpp_enabled;
    spine_addr_t      lc_client_addr;
    spine_addr_t      cfg_client_addr;
    spine_addr_t      ec_client_addr;
    spine_addr_t      diag_server_addr;
    spine_addr_t      diag_client_addr;
    uint64_t          hb_counter;
    eebus_timer_t*    hb_timer;
    eebus_mutex_t*    mutex;

    /* Outgoing write retry — compact array, [0, pending_count) valid,
     * guarded by eg->mutex. See eg_pending_* helpers below. */
    eg_pending_t      pending[EG_PENDING_MAX];
    uint8_t           pending_count;
};

/* Forward declarations for retry helpers (implementations after the
 * existing send paths). */
static void eg_pending_record(uc_lc_eg_t* eg, const char* sid, bool is_lpc,
                                eg_pending_kind_t kind, const void* payload,
                                uint64_t msg_counter);
static void eg_pending_complete(uc_lc_eg_t* eg, uint64_t ref,
                                  uint32_t error_number);
static void eg_pending_scan_and_retry(uc_lc_eg_t* eg);

/* ── Resolve + cache helpers ──────────────────────────────────────────────── */

static spine_device_t* resolve_remote(const uc_lc_eg_t* eg,
                                        const char* session_id) {
    if (!eg || !session_id) return NULL;
    return spine_device_find_remote(eg->cfg.device, session_id);
}

static lc_eg_cache_t* get_cache(spine_device_t* remote_dev) {
    return (lc_eg_cache_t*)spine_device_get_cache(remote_dev, SPINE_CACHE_LC_EG);
}

static lc_eg_cache_t* get_cache_by_session(const uc_lc_eg_t* eg,
                                             const char* session_id) {
    spine_device_t* dev = resolve_remote(eg, session_id);
    return dev ? get_cache(dev) : NULL;
}

static void cache_destroy(void* p) {
    lc_eg_cache_t* cache = (lc_eg_cache_t*)p;
    if (cache) {
        eebus_mutex_destroy(cache->mutex);
        EEBUS_FREE(cache);
    }
}

/* Direction validation: check EG enabled + CS supported */
static bool direction_valid(const uc_lc_eg_t* eg,
                             const lc_eg_cache_t* cache,
                             lc_direction_t direction) {
    if (direction == LC_DIRECTION_CONSUME)
        return eg->lpc_enabled && cache->lpc_supported;
    else
        return eg->lpp_enabled && cache->lpp_supported;
}

/* Find limitId by direction from cached limit descriptions */
static const lc_limit_desc_t* find_limit_desc_by_direction(
    const lc_eg_cache_t* cache, lc_direction_t direction)
{
    const char* dir_str = (direction == LC_DIRECTION_CONSUME) ? "consume" : "produce";
    for (uint8_t i = 0; i < cache->limit_descs.count; i++) {
        if (cache->limit_descs.items[i].limit_direction_present &&
            strcmp(cache->limit_descs.items[i].limit_direction, dir_str) == 0)
            return &cache->limit_descs.items[i];
    }
    return NULL;
}

/* Find limit data by limitId */
static lc_limit_data_t* find_limit_by_id(lc_limit_list_t* limits, uint32_t limit_id) {
    for (uint8_t i = 0; i < limits->count; i++) {
        if (limits->items[i].limit_id == limit_id)
            return &limits->items[i];
    }
    return NULL;
}

/* Find cfg kv by keyName */
static const dev_cfg_kv_desc_data_t* find_cfg_desc_by_name(
    const dev_cfg_kv_desc_list_t* descs, const char* key_name)
{
    for (uint8_t i = 0; i < descs->count; i++) {
        if (descs->items[i].key_name_present &&
            strcmp(descs->items[i].key_name, key_name) == 0)
            return &descs->items[i];
    }
    return NULL;
}

static const dev_cfg_kv_data_t* find_kv_by_id(
    const dev_cfg_kv_list_t* kv, uint32_t key_id)
{
    for (uint8_t i = 0; i < kv->count; i++) {
        if (kv->items[i].key_id == key_id)
            return &kv->items[i];
    }
    return NULL;
}

/* Failsafe keyName for direction */
static const char* failsafe_power_key_name(lc_direction_t d) {
    return (d == LC_DIRECTION_CONSUME)
        ? DEV_CFG_KEY_FAILSAFE_CONSUMPTION_ACTIVE_POWER_LIMIT
        : DEV_CFG_KEY_FAILSAFE_PRODUCTION_ACTIVE_POWER_LIMIT;
}

/* EC characteristicType patterns for direction */
static bool ec_char_matches_direction(const ec_characteristic_data_t* c,
                                       lc_direction_t direction) {
    if (!c->characteristic_type_present) return false;
    if (direction == LC_DIRECTION_CONSUME)
        return strstr(c->characteristic_type, "onsumption") != NULL;
    else
        return strstr(c->characteristic_type, "roduction") != NULL;
}

/* ── Send helper ─────────────────────────────────────────────────────────── */

static spine_err_t send_read(uc_lc_eg_t* eg,
                              spine_device_t* remote_dev,
                              const spine_addr_t* src,
                              const spine_addr_t* dst,
                              const char* fn_name) {
    spine_msg_t msg;
    spine_make_msg_req(&msg, src, dst, SPINE_CMD_READ,
                       spine_device_next_msg_counter(remote_dev),
                       false, fn_name, NULL);
    return spine_device_send(remote_dev, &msg);
}

/* ── Check init-read completion and fire callback ────────────────────────── */

/* Called with cache->mutex held. Unlocks before callback, relocks after. */
/* Send a targeted heartbeat to a specific remote CS only. */
static void send_heartbeat_to(uc_lc_eg_t* eg, spine_device_t* remote_dev) {
    eebus_mutex_lock(eg->mutex);
    eg->hb_counter++;
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    fill_utc_timestamp(hb.timestamp, sizeof(hb.timestamp));
    hb.timestamp_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout),
             "PT%uS", eg->cfg.heartbeat_interval_s);
    hb.heartbeat_timeout_present = true;
    hb.heartbeat_counter = eg->hb_counter;
    hb.heartbeat_counter_present = true;
    cJSON* data = dev_diag_heartbeat_encode(&hb);
    eebus_mutex_unlock(eg->mutex);

    if (data) {
        spine_msg_t ntf = {0};
        snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name),
                 "deviceDiagnosisHeartbeatData");
        ntf.payload.fn_data = data;
        spine_device_notify_to(remote_dev, &eg->diag_server_addr, &ntf);
        cJSON_Delete(data);
    }
}

/* Called with cache->mutex held. Unlocks before callback/sends, relocks after. */
static void check_init_complete(uc_lc_eg_t* eg, spine_device_t* remote_dev,
                                 lc_eg_cache_t* cache) {
    if ((cache->init_read_done & 0x3F) != READ_ALL_DONE) return;
    if (cache->init_read_done & 0x80) return;
    cache->init_read_done |= 0x80;

    EEBUS_LOG_INFO(LOG_TAG, "%s init reads complete, CS ready",
                   spine_device_get_session_id(remote_dev));

    const char* sid = spine_device_get_session_id(remote_dev);
    const char* etype = cache->cs_entity_type;
    bool lpc = cache->lpc_supported;
    bool lpp = cache->lpp_supported;
    bool lpc_avail = cache->lpc_available;
    bool lpp_avail = cache->lpp_available;
    eebus_mutex_unlock(cache->mutex);

    /* Send targeted heartbeat — first half of [LPC-913]. Application is
     * responsible for sending the following limit write within 60s. */
    send_heartbeat_to(eg, remote_dev);

    /* Notify application last */
    if (eg->cfg.on_cs_ready)
        eg->cfg.on_cs_ready(eg, sid, etype, lpc, lpp, lpc_avail, lpp_avail,
                            eg->cfg.userdata);

    eebus_mutex_lock(cache->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Handlers (dev is the remote device)
 * ════════════════════════════════════════════════════════════════════════════ */

/* Update cached list data with full replace or RFE partial/delete merge. */
#define CACHE_UPDATE_LIST(msg, cache_field, encode_fn, decode_fn, id_field) \
    do { \
        bool _rfe = (msg)->payload.partial.present || (msg)->payload.delete_.present; \
        if (_rfe) { \
            cJSON* _ex = encode_fn(&(cache_field)); \
            cJSON* _ef = spine_rfe_apply(_ex, (msg)->payload.fn_data, (id_field), \
                                          &(msg)->payload.partial, \
                                          &(msg)->payload.delete_); \
            cJSON_Delete(_ex); \
            if (_ef) { decode_fn(_ef, &(cache_field)); cJSON_Delete(_ef); } \
        } else { \
            decode_fn((msg)->payload.fn_data, &(cache_field)); \
        } \
    } while (0)

/* LC client: receives limit desc/data reply/notify + write result */
static void eg_lc_handler(spine_device_t* dev, const spine_msg_t* msg, void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;
    lc_eg_cache_t* cache = get_cache(dev);
    if (!cache) return;

    spine_cmd_t cmd = msg->header.cmd;
    const char* fn = msg->payload.fn_name;

    if (cmd == SPINE_CMD_REPLY || cmd == SPINE_CMD_NOTIFY) {
        eebus_mutex_lock(cache->mutex);
        if (strcmp(fn, "loadControlLimitDescriptionListData") == 0) {
            CACHE_UPDATE_LIST(msg, cache->limit_descs,
                lc_limit_desc_list_encode, lc_limit_desc_list_decode, "limitId");
            cache->init_read_done |= READ_BIT_LIMIT_DESC;
            check_init_complete(eg, dev, cache);
        } else if (strcmp(fn, "loadControlLimitListData") == 0) {
            CACHE_UPDATE_LIST(msg, cache->limits,
                lc_limit_list_encode, lc_limit_list_decode, "limitId");
            cache->init_read_done |= READ_BIT_LIMIT_DATA;
            check_init_complete(eg, dev, cache);
        }
        eebus_mutex_unlock(cache->mutex);
        return;
    }

    if (cmd == SPINE_CMD_RESULT && strcmp(fn, "resultData") == 0) {
        spine_result_data_t res;
        if (spine_result_data_decode(msg->payload.fn_data, &res) == SPINE_OK) {
            if (msg->header.msg_counter_ref_present)
                eg_pending_complete(eg, msg->header.msg_counter_ref, res.error_number);
            if (res.error_number != SPINE_RESULT_NO_ERROR)
                EEBUS_LOG_WARN(LOG_TAG, "%s limit write result: error %u",
                               spine_device_get_session_id(dev), res.error_number);
        }
    }
}

/* Cfg client: receives failsafe desc/data reply/notify + write result */
static void eg_cfg_handler(spine_device_t* dev, const spine_msg_t* msg, void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;
    lc_eg_cache_t* cache = get_cache(dev);
    if (!cache) return;

    spine_cmd_t cmd = msg->header.cmd;
    const char* fn = msg->payload.fn_name;

    if (cmd == SPINE_CMD_REPLY || cmd == SPINE_CMD_NOTIFY) {
        eebus_mutex_lock(cache->mutex);
        if (strcmp(fn, "deviceConfigurationKeyValueDescriptionListData") == 0) {
            CACHE_UPDATE_LIST(msg, cache->cfg_descs,
                dev_cfg_kv_desc_list_encode, dev_cfg_kv_desc_list_decode, "keyId");
            cache->init_read_done |= READ_BIT_CFG_DESC;
            check_init_complete(eg, dev, cache);
        } else if (strcmp(fn, "deviceConfigurationKeyValueListData") == 0) {
            CACHE_UPDATE_LIST(msg, cache->failsafe_kv,
                dev_cfg_kv_list_encode, dev_cfg_kv_list_decode, "keyId");
            cache->init_read_done |= READ_BIT_CFG_DATA;
            check_init_complete(eg, dev, cache);
        }
        eebus_mutex_unlock(cache->mutex);
        return;
    }

    if (cmd == SPINE_CMD_RESULT && strcmp(fn, "resultData") == 0) {
        spine_result_data_t res;
        if (spine_result_data_decode(msg->payload.fn_data, &res) == SPINE_OK) {
            if (msg->header.msg_counter_ref_present)
                eg_pending_complete(eg, msg->header.msg_counter_ref, res.error_number);
            if (res.error_number != SPINE_RESULT_NO_ERROR)
                EEBUS_LOG_WARN(LOG_TAG, "%s failsafe write result: error %u",
                               spine_device_get_session_id(dev), res.error_number);
        }
    }
}

/* Diag server: responds to READ of EG heartbeat (counter NOT incremented) */
static void eg_diag_server_handler(spine_device_t* dev,
                                    const spine_msg_t* msg, void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;
    if (msg->header.cmd != SPINE_CMD_READ) return;

    eebus_mutex_lock(eg->mutex);
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    fill_utc_timestamp(hb.timestamp, sizeof(hb.timestamp));
    hb.timestamp_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout),
             "PT%uS", eg->cfg.heartbeat_interval_s);
    hb.heartbeat_timeout_present = true;
    /* Read reply: use current counter WITHOUT incrementing */
    hb.heartbeat_counter = eg->hb_counter;
    hb.heartbeat_counter_present = true;
    eebus_mutex_unlock(eg->mutex);

    cJSON* data = dev_diag_heartbeat_encode(&hb);
    if (!data) return;

    spine_msg_t reply;
    spine_make_msg_res(&reply, &msg->header, SPINE_CMD_REPLY,
                       spine_device_next_msg_counter(dev),
                       "deviceDiagnosisHeartbeatData", data);
    spine_device_send(dev, &reply);
    cJSON_Delete(data);
}

/* Diag client: receives CS heartbeat reply/notify */
static void eg_diag_client_handler(spine_device_t* dev,
                                    const spine_msg_t* msg, void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;
    if (msg->header.cmd != SPINE_CMD_NOTIFY &&
        msg->header.cmd != SPINE_CMD_REPLY) return;
    if (strcmp(msg->payload.fn_name, "deviceDiagnosisHeartbeatData") != 0) return;

    lc_eg_cache_t* cache = get_cache(dev);
    if (!cache) return;

    dev_diag_heartbeat_t hb;
    if (dev_diag_heartbeat_decode(msg->payload.fn_data, &hb) == SPINE_OK) {
        eebus_mutex_lock(cache->mutex);
        cache->cs_hb_counter = hb.heartbeat_counter;
        EEBUS_LOG_DEBUG(LOG_TAG, "%s CS heartbeat #%llu",
                        spine_device_get_session_id(dev),
                        (unsigned long long)hb.heartbeat_counter);
        cache->init_read_done |= READ_BIT_DIAG_HB;
        check_init_complete(eg, dev, cache);
        eebus_mutex_unlock(cache->mutex);
    }
}

/* EC client: receives characteristic reply/notify */
static void eg_ec_handler(spine_device_t* dev, const spine_msg_t* msg, void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;
    if (msg->header.cmd != SPINE_CMD_REPLY &&
        msg->header.cmd != SPINE_CMD_NOTIFY) return;
    if (strcmp(msg->payload.fn_name,
              "electricalConnectionCharacteristicListData") != 0) return;

    lc_eg_cache_t* cache = get_cache(dev);
    if (!cache) return;

    eebus_mutex_lock(cache->mutex);
    CACHE_UPDATE_LIST(msg, cache->ec_chars,
        ec_characteristic_list_encode, ec_characteristic_list_decode,
        "characteristicId");
    cache->init_read_done |= READ_BIT_EC_CHAR;
    check_init_complete(eg, dev, cache);
    eebus_mutex_unlock(cache->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Heartbeat timer callback (notify all subscribers)
 * ════════════════════════════════════════════════════════════════════════════ */

static void eg_hb_timer_cb(void* ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;

    eebus_mutex_lock(eg->mutex);
    eg->hb_counter++;
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    fill_utc_timestamp(hb.timestamp, sizeof(hb.timestamp));
    hb.timestamp_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout),
             "PT%uS", eg->cfg.heartbeat_interval_s);
    hb.heartbeat_timeout_present = true;
    hb.heartbeat_counter = eg->hb_counter;
    hb.heartbeat_counter_present = true;
    cJSON* data = dev_diag_heartbeat_encode(&hb);
    eebus_mutex_unlock(eg->mutex);

    if (data) {
        spine_msg_t ntf = {0};
        snprintf(ntf.payload.fn_name, sizeof(ntf.payload.fn_name),
                 "deviceDiagnosisHeartbeatData");
        ntf.payload.fn_data = data;
        spine_device_notify(eg->cfg.device, &eg->diag_server_addr, &ntf);
        cJSON_Delete(data);
    }

    /* Retry any pending writes that previously failed or are still
     * awaiting a result. Same-CS order: limit before failsafe. */
    eg_pending_scan_and_retry(eg);
}

/* ════════════════════════════════════════════════════════════════════════════
 * peer_ready: discover CS, pre-scenario, init-scenario
 * ════════════════════════════════════════════════════════════════════════════ */

static void lc_eg_peer_ready(spine_device_t*            remote_dev,
                               const spine_nm_uc_entry_t* uc_infos,
                               uint8_t                    uc_count,
                               void*                      ud) {
    uc_lc_eg_t* eg = (uc_lc_eg_t*)ud;

    /* 1. Match UC: find LPC CS and/or LPP CS */
    const spine_nm_uc_entry_t* lpc_uc = NULL;
    const spine_nm_uc_entry_t* lpp_uc = NULL;
    for (uint8_t i = 0; i < uc_count; i++) {
        if (strcmp(uc_infos[i].actor, "ControllableSystem") != 0) continue;
        if (eg->lpc_enabled &&
            strcmp(uc_infos[i].use_case_name,
                   "limitationOfPowerConsumption") == 0)
            lpc_uc = &uc_infos[i];
        if (eg->lpp_enabled &&
            strcmp(uc_infos[i].use_case_name,
                   "limitationOfPowerProduction") == 0)
            lpp_uc = &uc_infos[i];
    }
    if (!lpc_uc && !lpp_uc) return;

    /* 2. If both, verify same entity */
    if (lpc_uc && lpp_uc &&
        !spine_addr_entity_equal(&lpc_uc->address, &lpp_uc->address)) {
        EEBUS_LOG_WARN(LOG_TAG, "%s LPC CS and LPP CS not on same entity, skip",
                       spine_device_get_session_id(remote_dev));
        return;
    }

    const spine_nm_uc_entry_t* ref_uc = lpc_uc ? lpc_uc : lpp_uc;

    /* 3. Find server features under CS entity */
    spine_addr_t lc_addr = {0}, cfg_addr = {0}, diag_addr = {0}, ec_addr = {0};
    bool found_lc = false, found_cfg = false, found_diag = false, found_ec = false;
    for (uint8_t i = 0; i < spine_device_feature_count(remote_dev); i++) {
        const spine_feature_info_t* f = spine_device_get_feature(remote_dev, i);
        if (!f || strcmp(f->role, "server") != 0) continue;
        if (!spine_addr_entity_equal(&f->addr, &ref_uc->address)) continue;
        if (!found_lc && strcmp(f->feature_type, "LoadControl") == 0)
            { lc_addr = f->addr; found_lc = true; }
        else if (!found_cfg && strcmp(f->feature_type, "DeviceConfiguration") == 0)
            { cfg_addr = f->addr; found_cfg = true; }
        else if (!found_diag && strcmp(f->feature_type, "DeviceDiagnosis") == 0)
            { diag_addr = f->addr; found_diag = true; }
        else if (!found_ec && strcmp(f->feature_type, "ElectricalConnection") == 0)
            { ec_addr = f->addr; found_ec = true; }
    }
    if (!found_lc) {
        EEBUS_LOG_WARN(LOG_TAG, "%s CS entity has no LoadControl server",
                       spine_device_get_session_id(remote_dev));
        return;
    }

    /* 4. Create and attach cache */
    lc_eg_cache_t* cache = EEBUS_NEW(lc_eg_cache_t);
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
    eebus_mutex_create(&cache->mutex);
    cache->cs_entity_addr = ref_uc->address;
    const char* etype = spine_device_get_entity_type(remote_dev, &ref_uc->address);
    if (etype)
        snprintf(cache->cs_entity_type, sizeof(cache->cs_entity_type), "%s", etype);
    cache->cs_lc_addr = lc_addr;
    cache->cs_cfg_addr = cfg_addr;
    cache->cs_diag_addr = diag_addr;
    cache->cs_ec_addr = ec_addr;
    cache->lpc_supported = (lpc_uc != NULL);
    cache->lpp_supported = (lpp_uc != NULL);
    cache->lpc_available = lpc_uc ? lpc_uc->available : false;
    cache->lpp_available = lpp_uc ? lpp_uc->available : false;
    spine_device_set_cache(remote_dev, SPINE_CACHE_LC_EG, cache, cache_destroy);

    /* 5. Pre-scenario: bind + subscribe */
    /* Scenario 1: bind + subscribe LC */
    spine_device_bind(remote_dev, &eg->lc_client_addr, &lc_addr);
    spine_device_subscribe(remote_dev, &eg->lc_client_addr, &lc_addr);

    /* Scenario 2: bind + subscribe Cfg */
    if (found_cfg) {
        spine_device_bind(remote_dev, &eg->cfg_client_addr, &cfg_addr);
        spine_device_subscribe(remote_dev, &eg->cfg_client_addr, &cfg_addr);
    }

    /* Scenario 3: subscribe Diag (no binding per spec §3.4.3.1) */
    if (found_diag)
        spine_device_subscribe(remote_dev, &eg->diag_client_addr, &diag_addr);

    /* Scenario 4: subscribe EC (no binding per spec §3.4.4.1) */
    if (found_ec)
        spine_device_subscribe(remote_dev, &eg->ec_client_addr, &ec_addr);

    /* 6. Init-scenario: full reads */
    send_read(eg, remote_dev, &eg->lc_client_addr, &lc_addr,
              "loadControlLimitDescriptionListData");
    send_read(eg, remote_dev, &eg->lc_client_addr, &lc_addr,
              "loadControlLimitListData");

    if (found_cfg) {
        send_read(eg, remote_dev, &eg->cfg_client_addr, &cfg_addr,
                  "deviceConfigurationKeyValueDescriptionListData");
        send_read(eg, remote_dev, &eg->cfg_client_addr, &cfg_addr,
                  "deviceConfigurationKeyValueListData");
    } else {
        cache->init_read_done |= (READ_BIT_CFG_DESC | READ_BIT_CFG_DATA);
    }

    if (found_diag) {
        send_read(eg, remote_dev, &eg->diag_client_addr, &diag_addr,
                  "deviceDiagnosisHeartbeatData");
    } else {
        cache->init_read_done |= READ_BIT_DIAG_HB;
    }

    if (found_ec) {
        send_read(eg, remote_dev, &eg->ec_client_addr, &ec_addr,
                  "electricalConnectionCharacteristicListData");
    } else {
        cache->init_read_done |= READ_BIT_EC_CHAR;
    }

    /* 7. Start heartbeat timer (if not already running) */
    eebus_mutex_lock(eg->mutex);
    if (!eg->hb_timer) {
        eebus_timer_create(&eg->hb_timer, eg_hb_timer_cb, eg);
        eebus_timer_start(eg->hb_timer,
                           eg->cfg.heartbeat_interval_s * 1000, true);
        EEBUS_LOG_INFO(LOG_TAG, "%s EG heartbeat timer started",
                       spine_device_get_session_id(remote_dev));
    }
    eebus_mutex_unlock(eg->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t uc_lc_eg_create(uc_lc_eg_t** out, const uc_lc_eg_config_t* cfg,
                             bool lpc_enabled, bool lpp_enabled) {
    if (!out || !cfg || !cfg->device) return SPINE_ERR_INVALID;
    if (!lpc_enabled && !lpp_enabled) return SPINE_ERR_INVALID;

    uc_lc_eg_t* eg = EEBUS_NEW(uc_lc_eg_t);
    if (!eg) return SPINE_ERR_NO_MEMORY;
    eebus_mutex_create(&eg->mutex);

    eg->cfg = *cfg;
    eg->lpc_enabled = lpc_enabled;
    eg->lpp_enabled = lpp_enabled;
    eg->hb_counter = 0;
    eg->hb_timer = NULL;
    if (!eg->cfg.heartbeat_interval_s) eg->cfg.heartbeat_interval_s = 60;

    uint8_t lc_nr   = cfg->lc_feature_nr          ? cfg->lc_feature_nr          : 1;
    uint8_t cfg_nr  = cfg->cfg_feature_nr         ? cfg->cfg_feature_nr         : 2;
    uint8_t diag_nr = cfg->diag_feature_nr        ? cfg->diag_feature_nr        : 3;
    uint8_t dc_nr   = cfg->diag_client_feature_nr ? cfg->diag_client_feature_nr : 4;
    uint8_t ec_nr   = cfg->ec_feature_nr          ? cfg->ec_feature_nr          : 5;

    eg->lc_client_addr          = cfg->eg_entity_addr;
    eg->lc_client_addr.feature  = lc_nr;
    eg->cfg_client_addr         = cfg->eg_entity_addr;
    eg->cfg_client_addr.feature = cfg_nr;
    eg->diag_server_addr          = cfg->eg_entity_addr;
    eg->diag_server_addr.feature  = diag_nr;
    eg->diag_client_addr          = cfg->eg_entity_addr;
    eg->diag_client_addr.feature  = dc_nr;
    eg->ec_client_addr          = cfg->eg_entity_addr;
    eg->ec_client_addr.feature  = ec_nr;

    /* Register entity */
    spine_device_add_entity(cfg->device, cfg->eg_entity_addr.entity,
                             cfg->eg_entity_addr.entity_depth,
                             cfg->entity_type ? cfg->entity_type : "");

    /* Register features (one set, shared by LPC+LPP) */
    spine_device_add_feature(cfg->device, &eg->lc_client_addr,
                              "LoadControl", "client", eg_lc_handler, eg);
    spine_device_add_feature(cfg->device, &eg->cfg_client_addr,
                              "DeviceConfiguration", "client", eg_cfg_handler, eg);
    spine_device_add_feature(cfg->device, &eg->diag_server_addr,
                              "DeviceDiagnosis", "server",
                              eg_diag_server_handler, eg);
    spine_device_add_feature_function(cfg->device, &eg->diag_server_addr,
        "deviceDiagnosisHeartbeatData", true, false, false, false);
    spine_device_add_feature(cfg->device, &eg->diag_client_addr,
                              "DeviceDiagnosis", "client",
                              eg_diag_client_handler, eg);
    spine_device_add_feature(cfg->device, &eg->ec_client_addr,
                              "ElectricalConnection", "client", eg_ec_handler, eg);

    /* Register UC entries */
    if (lpc_enabled) {
        spine_nm_uc_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.actor, sizeof(entry.actor), "EnergyGuard");
        snprintf(entry.use_case_name, sizeof(entry.use_case_name),
                 "limitationOfPowerConsumption");
        snprintf(entry.use_case_version, sizeof(entry.use_case_version), "1.0.0");
        snprintf(entry.use_case_doc_sub_revision,
                 sizeof(entry.use_case_doc_sub_revision), "release");
        for (uint8_t s = 1; s <= 4; s++) entry.scenario_support[s-1] = s;
        entry.scenario_count = 4;
        entry.address = cfg->eg_entity_addr;
        entry.available = cfg->initial_available;
        spine_device_add_usecase(cfg->device, &entry);
    }

    if (lpp_enabled) {
        spine_nm_uc_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.actor, sizeof(entry.actor), "EnergyGuard");
        snprintf(entry.use_case_name, sizeof(entry.use_case_name),
                 "limitationOfPowerProduction");
        snprintf(entry.use_case_version, sizeof(entry.use_case_version), "1.0.0");
        snprintf(entry.use_case_doc_sub_revision,
                 sizeof(entry.use_case_doc_sub_revision), "release");
        for (uint8_t s = 1; s <= 4; s++) entry.scenario_support[s-1] = s;
        entry.scenario_count = 4;
        entry.address = cfg->eg_entity_addr;
        entry.available = cfg->initial_available;
        spine_device_add_usecase(cfg->device, &entry);
    }

    spine_device_add_peer_ready_cb(cfg->device, lc_eg_peer_ready, eg);

    *out = eg;
    return SPINE_OK;
}

void uc_lc_eg_destroy(uc_lc_eg_t* eg) {
    if (!eg) return;
    eebus_timer_destroy(eg->hb_timer);
    eebus_mutex_destroy(eg->mutex);
    EEBUS_FREE(eg);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Get API (read from cache, filtered by direction)
 * ════════════════════════════════════════════════════════════════════════════ */

void uc_lc_eg_get_limit(const uc_lc_eg_t* eg, const char* session_id,
                         lc_direction_t direction, uc_lc_limit_t* out) {
    memset(out, 0, sizeof(*out));
    lc_eg_cache_t* cache = get_cache_by_session(eg, session_id);
    if (!cache || !direction_valid(eg, cache, direction)) return;

    eebus_mutex_lock(cache->mutex);
    const lc_limit_desc_t* desc = find_limit_desc_by_direction(cache, direction);
    if (!desc) { eebus_mutex_unlock(cache->mutex); return; }
    const lc_limit_data_t* lim = find_limit_by_id(&cache->limits, desc->limit_id);
    if (!lim) { eebus_mutex_unlock(cache->mutex); return; }

    if (lim->is_limit_changeable_present) out->is_limit_changeable = lim->is_limit_changeable;
    if (lim->is_limit_active_present) out->is_limit_active = lim->is_limit_active;
    out->time_period = lim->time_period;
    if (lim->value_present) out->value_w = spine_scaled_number_to_double(&lim->value);
    eebus_mutex_unlock(cache->mutex);
}

void uc_lc_eg_get_failsafe(const uc_lc_eg_t* eg, const char* session_id,
                             lc_direction_t direction, uc_lc_failsafe_t* out) {
    memset(out, 0, sizeof(*out));
    lc_eg_cache_t* cache = get_cache_by_session(eg, session_id);
    if (!cache || !direction_valid(eg, cache, direction)) return;

    eebus_mutex_lock(cache->mutex);
    /* Find power key_id by direction */
    const dev_cfg_kv_desc_data_t* power_desc =
        find_cfg_desc_by_name(&cache->cfg_descs, failsafe_power_key_name(direction));
    if (power_desc) {
        const dev_cfg_kv_data_t* kv = find_kv_by_id(&cache->failsafe_kv,
                                                      power_desc->key_id);
        if (kv && kv->value_present && kv->value_type == DEV_CFG_VALUE_SCALED_NUM)
            out->power_limit_w = spine_scaled_number_to_double(&kv->value.scaled_num_val);
        if (kv) out->power_limit_changeable = kv->is_value_changeable;
    }

    /* Duration is shared (keyName = "failsafeDurationMinimum") */
    const dev_cfg_kv_desc_data_t* dur_desc =
        find_cfg_desc_by_name(&cache->cfg_descs, DEV_CFG_KEY_FAILSAFE_DURATION_MINIMUM);
    if (dur_desc) {
        const dev_cfg_kv_data_t* kv = find_kv_by_id(&cache->failsafe_kv,
                                                      dur_desc->key_id);
        if (kv && kv->value_present && kv->value_type == DEV_CFG_VALUE_DURATION)
            out->duration_s = uc_lc_parse_duration_s(kv->value.duration_val);
        if (kv) out->duration_changeable = kv->is_value_changeable;
    }
    eebus_mutex_unlock(cache->mutex);
}

void uc_lc_eg_get_ec_characteristic(const uc_lc_eg_t* eg,
                                     const char* session_id,
                                     lc_direction_t direction,
                                     uc_lc_ec_characteristic_t* out) {
    memset(out, 0, sizeof(*out));
    lc_eg_cache_t* cache = get_cache_by_session(eg, session_id);
    if (!cache || !direction_valid(eg, cache, direction)) return;

    eebus_mutex_lock(cache->mutex);
    for (uint8_t i = 0; i < cache->ec_chars.count; i++) {
        const ec_characteristic_data_t* c = &cache->ec_chars.items[i];
        if (ec_char_matches_direction(c, direction)) {
            /* "contractualConsumptionNominalMax" / "contractualProductionNominalMax"
             * also contain "Nominal", so prefix-match contractual first. */
            if (strncmp(c->characteristic_type, "contractual", 11) == 0)
                out->type = LC_POWER_MAX_CONTRACTUAL;
            else if (strncmp(c->characteristic_type, "power", 5) == 0)
                out->type = LC_POWER_MAX_NOMINAL;
            if (c->value_present)
                out->value_w = spine_scaled_number_to_double(&c->value);
            out->electrical_connection_id = c->electrical_connection_id;
            out->parameter_id = c->parameter_id;
            eebus_mutex_unlock(cache->mutex);
            return;
        }
    }
    eebus_mutex_unlock(cache->mutex);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Write API
 * ════════════════════════════════════════════════════════════════════════════ */

static spine_err_t send_limit_internal(uc_lc_eg_t* eg, const char* session_id,
                                         lc_direction_t direction,
                                         const uc_lc_limit_t* limit,
                                         uint64_t* counter_out) {
    if (!eg || !session_id || !limit) return SPINE_ERR_INVALID;
    spine_device_t* remote_dev = resolve_remote(eg, session_id);
    if (!remote_dev) return SPINE_ERR_NOT_FOUND;
    lc_eg_cache_t* cache = get_cache(remote_dev);
    if (!cache || !direction_valid(eg, cache, direction)) return SPINE_ERR_INVALID;

    eebus_mutex_lock(cache->mutex);
    const lc_limit_desc_t* desc = find_limit_desc_by_direction(cache, direction);
    if (!desc) { eebus_mutex_unlock(cache->mutex); return SPINE_ERR_NOT_FOUND; }
    uint32_t limit_id = desc->limit_id;

    /* Check if old limit has endTime but new one doesn't → need delete */
    lc_limit_data_t* old = find_limit_by_id(&cache->limits, limit_id);
    bool old_has_endtime = old && old->time_period.end_time_present;
    bool new_has_endtime = limit->time_period.end_time_present;
    spine_addr_t cs_lc = cache->cs_lc_addr;
    eebus_mutex_unlock(cache->mutex);

    /* Build codec data */
    lc_limit_list_t lst;
    memset(&lst, 0, sizeof(lst));
    lst.count = 1;
    lst.items[0].limit_id = limit_id;
    lst.items[0].is_limit_changeable = limit->is_limit_changeable;
    lst.items[0].is_limit_changeable_present = true;
    lst.items[0].is_limit_active = limit->is_limit_active;
    lst.items[0].is_limit_active_present = true;
    lst.items[0].time_period = limit->time_period;
    spine_scaled_number_from_double(limit->value_w, &lst.items[0].value);
    lst.items[0].value_present = true;

    cJSON* data = lc_limit_list_encode(&lst);
    if (!data) return SPINE_ERR_NO_MEMORY;

    spine_msg_t msg;
    uint64_t counter = spine_device_next_msg_counter(remote_dev);
    spine_make_msg_req(&msg, &eg->lc_client_addr, &cs_lc,
                       SPINE_CMD_WRITE,
                       counter,
                       true, "loadControlLimitListData", data);
    msg.payload.partial.present = true;
    if (counter_out) *counter_out = counter;

    /* If old had endTime but new doesn't, add delete filter [LPC §3.4.1.4] */
    if (old_has_endtime && !new_has_endtime) {
        const char* sel_keys[] = {"limitId"};
        spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(limit_id)};
        const char* elem_paths[] = {"timePeriod", NULL};
        spine_rfe_build_filter(&msg.payload.delete_,
            "loadControlLimitListData", sel_keys, sel_vals, 1, elem_paths);
    }

    spine_err_t rc = spine_device_send(remote_dev, &msg);
    cJSON_Delete(data);
    spine_rfe_free_filter(&msg.payload.delete_);

    /* Cache update deferred to CS notify (CS may reject or modify) */
    return rc;
}

/* Send a single-kv failsafe write (partial). is_value_changeable is
 * hardcoded true — the EG always advertises the value as changeable. */
static spine_err_t send_failsafe_kv(uc_lc_eg_t* eg, const char* session_id,
                                      lc_direction_t direction,
                                      const dev_cfg_kv_data_t* kv_item,
                                      uint64_t* counter_out) {
    spine_device_t* remote_dev = resolve_remote(eg, session_id);
    if (!remote_dev) return SPINE_ERR_NOT_FOUND;
    lc_eg_cache_t* cache = get_cache(remote_dev);
    if (!cache || !direction_valid(eg, cache, direction)) return SPINE_ERR_INVALID;

    dev_cfg_kv_list_t lst;
    memset(&lst, 0, sizeof(lst));
    lst.items[0] = *kv_item;
    lst.count = 1;

    eebus_mutex_lock(cache->mutex);
    spine_addr_t cs_cfg = cache->cs_cfg_addr;
    eebus_mutex_unlock(cache->mutex);

    cJSON* data = dev_cfg_kv_list_encode(&lst);
    if (!data) return SPINE_ERR_NO_MEMORY;

    spine_msg_t msg;
    uint64_t counter = spine_device_next_msg_counter(remote_dev);
    spine_make_msg_req(&msg, &eg->cfg_client_addr, &cs_cfg,
                       SPINE_CMD_WRITE,
                       counter,
                       true, "deviceConfigurationKeyValueListData", data);
    msg.payload.partial.present = true;
    if (counter_out) *counter_out = counter;

    spine_err_t rc = spine_device_send(remote_dev, &msg);
    cJSON_Delete(data);
    return rc;
}

static spine_err_t send_failsafe_power_internal(uc_lc_eg_t* eg,
        const char* session_id, lc_direction_t direction,
        double power_limit_w, uint64_t* counter_out) {
    if (!eg || !session_id) return SPINE_ERR_INVALID;
    lc_eg_cache_t* cache = get_cache_by_session(eg, session_id);
    if (!cache) return SPINE_ERR_NOT_FOUND;

    eebus_mutex_lock(cache->mutex);
    const dev_cfg_kv_desc_data_t* desc =
        find_cfg_desc_by_name(&cache->cfg_descs, failsafe_power_key_name(direction));
    uint32_t key_id = desc ? desc->key_id : 0;
    eebus_mutex_unlock(cache->mutex);
    if (!desc) return SPINE_ERR_NOT_FOUND;

    dev_cfg_kv_data_t item = {0};
    item.key_id     = key_id;
    item.value_type = DEV_CFG_VALUE_SCALED_NUM;
    spine_scaled_number_from_double(power_limit_w, &item.value.scaled_num_val);
    item.value_present                 = true;
    item.is_value_changeable           = true;
    item.is_value_changeable_present   = true;

    return send_failsafe_kv(eg, session_id, direction, &item, counter_out);
}

static spine_err_t send_failsafe_duration_internal(uc_lc_eg_t* eg,
        const char* session_id, lc_direction_t direction,
        uint32_t duration_s, uint64_t* counter_out) {
    if (!eg || !session_id) return SPINE_ERR_INVALID;
    lc_eg_cache_t* cache = get_cache_by_session(eg, session_id);
    if (!cache) return SPINE_ERR_NOT_FOUND;

    eebus_mutex_lock(cache->mutex);
    const dev_cfg_kv_desc_data_t* desc =
        find_cfg_desc_by_name(&cache->cfg_descs, DEV_CFG_KEY_FAILSAFE_DURATION_MINIMUM);
    uint32_t key_id = desc ? desc->key_id : 0;
    eebus_mutex_unlock(cache->mutex);
    if (!desc) return SPINE_ERR_NOT_FOUND;

    dev_cfg_kv_data_t item = {0};
    item.key_id     = key_id;
    item.value_type = DEV_CFG_VALUE_DURATION;
    snprintf(item.value.duration_val, sizeof(item.value.duration_val),
             "PT%uS", duration_s);
    item.value_present                 = true;
    item.is_value_changeable           = true;
    item.is_value_changeable_present   = true;

    return send_failsafe_kv(eg, session_id, direction, &item, counter_out);
}

/* ── EG pending retry helpers + public write wrappers ────────────────────── */

static void eg_pending_record(uc_lc_eg_t* eg, const char* sid, bool is_lpc,
                                eg_pending_kind_t kind, const void* payload,
                                uint64_t msg_counter) {
    eebus_mutex_lock(eg->mutex);

    eg_pending_t* p = NULL;
    for (uint8_t i = 0; i < eg->pending_count; i++) {
        eg_pending_t* q = &eg->pending[i];
        if (q->kind != kind) continue;
        if (strcmp(q->session_id, sid) != 0) continue;
        /* FS_DURATION is shared LPC/LPP — sid+kind alone is unique. */
        if (kind != EG_PENDING_FS_DURATION && q->is_lpc != is_lpc) continue;
        p = q; break;
    }
    if (!p) {
        if (eg->pending_count >= EG_PENDING_MAX) {
            EEBUS_LOG_WARN(LOG_TAG,
                "EG pending table full — %s kind=%d not tracked",
                sid, (int)kind);
            eebus_mutex_unlock(eg->mutex);
            return;
        }
        p = &eg->pending[eg->pending_count++];
    }
    p->kind   = kind;
    p->is_lpc = is_lpc;
    snprintf(p->session_id, sizeof(p->session_id), "%s", sid);
    p->msg_counter = msg_counter;
    p->sent_at     = time(NULL);
    p->retry_count = 0;
    switch (kind) {
    case EG_PENDING_LIMIT:
        p->payload.limit         = *(const uc_lc_limit_t*)payload; break;
    case EG_PENDING_FS_POWER:
        p->payload.fs_power_w    = *(const double*)payload;        break;
    case EG_PENDING_FS_DURATION:
        p->payload.fs_duration_s = *(const uint32_t*)payload;      break;
    }

    eebus_mutex_unlock(eg->mutex);
}

static void eg_pending_complete(uc_lc_eg_t* eg, uint64_t ref,
                                  uint32_t error_number) {
    eebus_mutex_lock(eg->mutex);
    for (uint8_t i = 0; i < eg->pending_count; i++) {
        eg_pending_t* p = &eg->pending[i];
        if (p->msg_counter != ref) continue;
        if (error_number == SPINE_RESULT_NO_ERROR ||
            error_number == SPINE_RESULT_DEST_UNKNOWN ||
            error_number == SPINE_RESULT_CMD_NOT_SUPPORTED) {
            eg->pending[i] = eg->pending[--eg->pending_count];   /* swap-and-pop */
        }
        /* else keep — heartbeat will retry */
        break;
    }
    eebus_mutex_unlock(eg->mutex);
}

static void eg_pending_scan_pass(uc_lc_eg_t* eg, eg_pending_kind_t kind) {
    eebus_mutex_lock(eg->mutex);
    uint8_t i = 0;
    while (i < eg->pending_count) {
        eg_pending_t* p = &eg->pending[i];
        if (p->kind != kind) { i++; continue; }

        if (p->retry_count >= EG_MAX_RETRY) {
            eg->pending[i] = eg->pending[--eg->pending_count];
            continue;     /* don't i++; new pending[i] still to process */
        }

        lc_direction_t dir = p->is_lpc ? LC_DIRECTION_CONSUME
                                       : LC_DIRECTION_PRODUCE;
        uint64_t new_counter;
        spine_err_t rc = SPINE_ERR_INVALID;
        switch (kind) {
        case EG_PENDING_LIMIT:
            rc = send_limit_internal(eg, p->session_id, dir,
                                       &p->payload.limit, &new_counter);
            break;
        case EG_PENDING_FS_POWER:
            rc = send_failsafe_power_internal(eg, p->session_id, dir,
                                                p->payload.fs_power_w,
                                                &new_counter);
            break;
        case EG_PENDING_FS_DURATION:
            /* dir comes from p->is_lpc — the direction used by the
             * latest write_failsafe_duration caller; that direction
             * passed direction_valid at record time. */
            rc = send_failsafe_duration_internal(eg, p->session_id, dir,
                                                   p->payload.fs_duration_s,
                                                   &new_counter);
            break;
        }

        if (rc == SPINE_OK) {
            p->msg_counter = new_counter;
            p->sent_at     = time(NULL);
            p->retry_count++;
            i++;
        } else {
            /* Peer gone or send failed — drop tracking */
            eg->pending[i] = eg->pending[--eg->pending_count];
            /* don't i++ */
        }
    }
    eebus_mutex_unlock(eg->mutex);
}

static void eg_pending_scan_and_retry(uc_lc_eg_t* eg) {
    /* Same-CS order: limit before failsafe per [LPC-003/2];
     * FS_POWER/FS_DURATION between themselves are order-independent. */
    eg_pending_scan_pass(eg, EG_PENDING_LIMIT);
    eg_pending_scan_pass(eg, EG_PENDING_FS_POWER);
    eg_pending_scan_pass(eg, EG_PENDING_FS_DURATION);
}

spine_err_t uc_lc_eg_write_limit(uc_lc_eg_t* eg, const char* session_id,
                                  lc_direction_t direction,
                                  const uc_lc_limit_t* limit) {
    uint64_t counter;
    spine_err_t rc = send_limit_internal(eg, session_id, direction, limit, &counter);
    if (rc == SPINE_OK) {
        bool is_lpc = (direction == LC_DIRECTION_CONSUME);
        eg_pending_record(eg, session_id, is_lpc, EG_PENDING_LIMIT, limit, counter);
    }
    return rc;
}

spine_err_t uc_lc_eg_write_failsafe_power(uc_lc_eg_t* eg, const char* session_id,
                                            lc_direction_t direction,
                                            double power_limit_w) {
    uint64_t counter;
    spine_err_t rc = send_failsafe_power_internal(eg, session_id, direction,
                                                    power_limit_w, &counter);
    if (rc == SPINE_OK) {
        bool is_lpc = (direction == LC_DIRECTION_CONSUME);
        eg_pending_record(eg, session_id, is_lpc, EG_PENDING_FS_POWER,
                            &power_limit_w, counter);
    }
    return rc;
}

spine_err_t uc_lc_eg_write_failsafe_duration(uc_lc_eg_t* eg, const char* session_id,
                                                lc_direction_t direction,
                                                uint32_t duration_s) {
    uint64_t counter;
    spine_err_t rc = send_failsafe_duration_internal(eg, session_id, direction,
                                                       duration_s, &counter);
    if (rc == SPINE_OK) {
        /* Duration is shared LPC/LPP — dedup ignores is_lpc, but retry
         * needs a valid direction so we remember the caller's. */
        bool is_lpc = (direction == LC_DIRECTION_CONSUME);
        eg_pending_record(eg, session_id, is_lpc, EG_PENDING_FS_DURATION,
                            &duration_s, counter);
    }
    return rc;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Availability
 * ════════════════════════════════════════════════════════════════════════════ */

spine_err_t uc_lc_eg_set_available(uc_lc_eg_t* eg, lc_direction_t direction,
                                    bool available) {
    if (!eg) return SPINE_ERR_INVALID;
    const char* name = (direction == LC_DIRECTION_CONSUME)
        ? "limitationOfPowerConsumption"
        : "limitationOfPowerProduction";
    return spine_device_set_usecase_available(eg->cfg.device, name, available);
}
