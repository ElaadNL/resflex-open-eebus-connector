// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_measurement.c
 *
 * measurementListData codec            (Resource Spec §5.3.15.2, Table 244)
 * measurementDescriptionListData codec (Resource Spec §5.3.15.5, Table 251)
 */

#include "spine_codec_measurement.h"
#include "spine_codec_load_ctrl.h"
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
static bool seq_add_arr(cJSON* a, const char* k, cJSON* inner) {
    cJSON* o = cJSON_CreateObject(); if (!o) { cJSON_Delete(inner); return false; }
    cJSON_AddItemToObject(o, k, inner); cJSON_AddItemToArray(a, o); return true;
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

/* ── measurementListData (Table 244) ─────────────────────────────────────── */

spine_err_t meas_list_decode(const cJSON* fn_data, meas_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "measurementListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq, "measurementData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_MEAS_MAX) break;
        meas_data_t* m = &out->items[out->count];
        memset(m, 0, sizeof(*m));

        const cJSON* mid = seq_get(item_seq, "measurementId");
        if (cJSON_IsNumber(mid)) m->measurement_id = (uint32_t)mid->valuedouble;

        const cJSON* ts = seq_get(item_seq, "timestamp");
        if (cJSON_IsString(ts) && ts->valuestring) {
            snprintf(m->timestamp, sizeof(m->timestamp), "%s", ts->valuestring);
            m->timestamp_present = true;
        }

        const cJSON* vt = seq_get(item_seq, "valueType");
        if (cJSON_IsString(vt) && vt->valuestring) {
            snprintf(m->value_type, sizeof(m->value_type), "%s", vt->valuestring);
            m->value_type_present = true;
        }

        const cJSON* val = seq_get(item_seq, "value");
        if (val && cJSON_IsArray(val)) {
            spine_scaled_number_decode(val, &m->value);
            m->value_present = true;
        }

        const cJSON* ep = seq_get(item_seq, "evaluationPeriod");
        if (cJSON_IsArray(ep)) {
            const cJSON* es = seq_get(ep, "startTime");
            if (cJSON_IsString(es) && es->valuestring) {
                snprintf(m->evaluation_period.start_time,
                         sizeof(m->evaluation_period.start_time),
                         "%s", es->valuestring);
                m->evaluation_period.start_time_present = true;
            }
            const cJSON* ee = seq_get(ep, "endTime");
            if (cJSON_IsString(ee) && ee->valuestring) {
                snprintf(m->evaluation_period.end_time,
                         sizeof(m->evaluation_period.end_time),
                         "%s", ee->valuestring);
                m->evaluation_period.end_time_present = true;
            }
        }

        const cJSON* vs = seq_get(item_seq, "valueSource");
        if (cJSON_IsString(vs) && vs->valuestring) {
            snprintf(m->value_source, sizeof(m->value_source),
                     "%s", vs->valuestring);
            m->value_source_present = true;
        }
        const cJSON* vtd = seq_get(item_seq, "valueTendency");
        if (cJSON_IsString(vtd) && vtd->valuestring) {
            snprintf(m->value_tendency, sizeof(m->value_tendency),
                     "%s", vtd->valuestring);
            m->value_tendency_present = true;
        }
        const cJSON* vst = seq_get(item_seq, "valueState");
        if (cJSON_IsString(vst) && vst->valuestring) {
            snprintf(m->value_state, sizeof(m->value_state),
                     "%s", vst->valuestring);
            m->value_state_present = true;
        }
        out->count++;
    }
    return SPINE_OK;
}

cJSON* meas_list_encode(const meas_list_t* in) {
    if (!in) return NULL;

    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const meas_data_t* m = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "measurementId",
                         (double)m->measurement_id)) goto oom;

        if (m->timestamp_present)
            if (!seq_add_str(item_seq, "timestamp", m->timestamp)) goto oom;

        if (m->value_type_present)
            if (!seq_add_str(item_seq, "valueType", m->value_type)) goto oom;

        if (m->value_present) {
            cJSON* val_arr = spine_scaled_number_encode(&m->value);
            if (!val_arr || !seq_add_arr(item_seq, "value", val_arr)) goto oom;
        }

        if (m->evaluation_period.start_time_present ||
            m->evaluation_period.end_time_present) {
            cJSON* ep_seq = cJSON_CreateArray();
            if (!ep_seq) goto oom;
            if (m->evaluation_period.start_time_present)
                if (!seq_add_str(ep_seq, "startTime",
                                 m->evaluation_period.start_time))
                    { cJSON_Delete(ep_seq); goto oom; }
            if (m->evaluation_period.end_time_present)
                if (!seq_add_str(ep_seq, "endTime",
                                 m->evaluation_period.end_time))
                    { cJSON_Delete(ep_seq); goto oom; }
            if (!seq_add_arr(item_seq, "evaluationPeriod", ep_seq)) goto oom;
        }

        if (m->value_source_present)
            if (!seq_add_str(item_seq, "valueSource", m->value_source)) goto oom;
        if (m->value_tendency_present)
            if (!seq_add_str(item_seq, "valueTendency", m->value_tendency)) goto oom;
        if (m->value_state_present)
            if (!seq_add_str(item_seq, "valueState", m->value_state)) goto oom;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "measurementData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "measurementListData", fn_seq);
    return fn_data;
}

/* ── measurementDescriptionListData (Table 251) ──────────────────────────── */

spine_err_t meas_desc_list_decode(const cJSON* fn_data, meas_desc_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "measurementDescriptionListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq, "measurementDescriptionData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_MEAS_MAX) break;
        meas_desc_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* mid = seq_get(item_seq, "measurementId");
        if (cJSON_IsNumber(mid)) d->measurement_id = (uint32_t)mid->valuedouble;

        const cJSON* mt = seq_get(item_seq, "measurementType");
        if (cJSON_IsString(mt) && mt->valuestring) {
            snprintf(d->measurement_type, sizeof(d->measurement_type),
                     "%s", mt->valuestring);
            d->measurement_type_present = true;
        }
        const cJSON* ct = seq_get(item_seq, "commodityType");
        if (cJSON_IsString(ct) && ct->valuestring) {
            snprintf(d->commodity_type, sizeof(d->commodity_type),
                     "%s", ct->valuestring);
            d->commodity_type_present = true;
        }
        const cJSON* unit = seq_get(item_seq, "unit");
        if (cJSON_IsString(unit) && unit->valuestring) {
            snprintf(d->unit, sizeof(d->unit), "%s", unit->valuestring);
            d->unit_present = true;
        }
        const cJSON* sc = seq_get(item_seq, "scopeType");
        if (cJSON_IsString(sc) && sc->valuestring) {
            snprintf(d->scope_type, sizeof(d->scope_type), "%s", sc->valuestring);
            d->scope_type_present = true;
        }
        out->count++;
    }
    return SPINE_OK;
}

cJSON* meas_desc_list_encode(const meas_desc_list_t* in) {
    if (!in) return NULL;

    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const meas_desc_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "measurementId",
                         (double)d->measurement_id)) goto oom_d;
        if (d->measurement_type_present)
            if (!seq_add_str(item_seq, "measurementType",
                             d->measurement_type)) goto oom_d;
        if (d->commodity_type_present)
            if (!seq_add_str(item_seq, "commodityType",
                             d->commodity_type)) goto oom_d;
        if (d->unit_present)
            if (!seq_add_str(item_seq, "unit", d->unit)) goto oom_d;
        if (d->scope_type_present)
            if (!seq_add_str(item_seq, "scopeType", d->scope_type)) goto oom_d;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom_d:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "measurementDescriptionData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "measurementDescriptionListData", fn_seq);
    return fn_data;
}
