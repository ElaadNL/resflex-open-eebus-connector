// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_dev_diag.c
 *
 * DeviceDiagnosisHeartbeatDataType xs:sequence:
 *   [{"timestamp":"..."}, {"heartbeatCounter":N}, {"heartbeatTimeout":"PT60S"}]
 * DeviceDiagnosisStateDataType xs:sequence:
 *   [{"timestamp":"..."}, {"operatingState":"..."}, ...]
 */

#include "spine_codec_dev_diag.h"
#include <string.h>
#include <stdio.h>

static bool seq_add_str(cJSON* a, const char* k, const char* v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddStringToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_num(cJSON* a, const char* k, double v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddNumberToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static const cJSON* seq_get(const cJSON* a, const char* k) {
    if (!cJSON_IsArray(a)) return NULL;
    const cJSON* e;
    cJSON_ArrayForEach(e, a) {
        if (!cJSON_IsObject(e) || !e->child) continue;
        if (strcmp(e->child->string, k) == 0) return e->child;
    }
    return NULL;
}

spine_err_t dev_diag_heartbeat_decode(const cJSON* fn_data,
                                       dev_diag_heartbeat_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "deviceDiagnosisHeartbeatData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ts = seq_get(fn_seq, "timestamp");
    if (cJSON_IsString(ts) && ts->valuestring) {
        snprintf(out->timestamp, sizeof(out->timestamp), "%s", ts->valuestring);
        out->timestamp_present = true;
    }
    const cJSON* hc = seq_get(fn_seq, "heartbeatCounter");
    if (cJSON_IsNumber(hc)) {
        out->heartbeat_counter         = (uint64_t)hc->valuedouble;
        out->heartbeat_counter_present = true;
    }
    const cJSON* ht = seq_get(fn_seq, "heartbeatTimeout");
    if (cJSON_IsString(ht) && ht->valuestring) {
        snprintf(out->heartbeat_timeout, sizeof(out->heartbeat_timeout),
                 "%s", ht->valuestring);
        out->heartbeat_timeout_present = true;
    }
    return SPINE_OK;
}

cJSON* dev_diag_heartbeat_encode(const dev_diag_heartbeat_t* in) {
    if (!in) return NULL;

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) return NULL;

    if (in->timestamp_present)
        if (!seq_add_str(fn_seq, "timestamp", in->timestamp))
            { cJSON_Delete(fn_seq); return NULL; }
    if (in->heartbeat_counter_present)
        if (!seq_add_num(fn_seq, "heartbeatCounter",
                         (double)in->heartbeat_counter))
            { cJSON_Delete(fn_seq); return NULL; }
    if (in->heartbeat_timeout_present)
        if (!seq_add_str(fn_seq, "heartbeatTimeout", in->heartbeat_timeout))
            { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "deviceDiagnosisHeartbeatData", fn_seq);
    return fn_data;
}

/* ── deviceDiagnosisStateData ────────────────────────────────────────────── */

spine_err_t dev_diag_state_decode(const cJSON* fn_data, dev_diag_state_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "deviceDiagnosisStateData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

#define DECODE_STR(jk, fld, pf) do { \
    const cJSON* _v = seq_get(fn_seq, jk); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        snprintf(out->fld, sizeof(out->fld), "%s", _v->valuestring); \
        out->pf = true; } \
} while(0)

    DECODE_STR("timestamp",            timestamp,              timestamp_present);
    DECODE_STR("operatingState",       operating_state,        operating_state_present);
    DECODE_STR("vendorStateCode",      vendor_state_code,      vendor_state_code_present);
    DECODE_STR("lastErrorCode",        last_error_code,        last_error_code_present);
    DECODE_STR("upTime",               up_time,                up_time_present);
    DECODE_STR("totalUpTime",          total_up_time,          total_up_time_present);
    DECODE_STR("powerSupplyCondition", power_supply_condition,  power_supply_condition_present);
#undef DECODE_STR

    return SPINE_OK;
}

cJSON* dev_diag_state_encode(const dev_diag_state_t* in) {
    if (!in) return NULL;

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) return NULL;

#define ENCODE_STR(pf, jk, fld) \
    if (in->pf) \
        if (!seq_add_str(fn_seq, jk, in->fld)) \
            { cJSON_Delete(fn_seq); return NULL; }

    ENCODE_STR(timestamp_present,              "timestamp",            timestamp);
    ENCODE_STR(operating_state_present,        "operatingState",       operating_state);
    ENCODE_STR(vendor_state_code_present,      "vendorStateCode",      vendor_state_code);
    ENCODE_STR(last_error_code_present,        "lastErrorCode",        last_error_code);
    ENCODE_STR(up_time_present,                "upTime",               up_time);
    ENCODE_STR(total_up_time_present,          "totalUpTime",          total_up_time);
    ENCODE_STR(power_supply_condition_present,  "powerSupplyCondition", power_supply_condition);
#undef ENCODE_STR

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "deviceDiagnosisStateData", fn_seq);
    return fn_data;
}
