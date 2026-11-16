// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_load_ctrl.c
 *
 * LoadControl XSD is entirely xs:sequence -> Array format.
 *
 * ScaledNumberType xs:sequence:
 *   [{"number":N}, {"scale":S}]
 *
 * LoadControlLimitDataType xs:sequence:
 *   [{"limitId":1}, {"isLimitChangeable":true}, {"isLimitActive":false},
 *    {"value":[{"number":5000}]}, ...]
 *
 * LoadControlLimitListDataType xs:sequence(loadControlLimitData maxOccurs=unbounded):
 *   fn_data = {"loadControlLimitListData": [
 *     {"loadControlLimitData": [item_arr1, item_arr2, ...]}
 *   ]}
 */

#include "spine_codec_load_ctrl.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── seq helpers ─────────────────────────────────────────────────────────── */

static bool seq_add_str(cJSON* a, const char* k, const char* v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddStringToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_num(cJSON* a, const char* k, double v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddNumberToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_bool(cJSON* a, const char* k, bool v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddBoolToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
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

/* ── ScaledNumber ─────────────────────────────────────────────────────────── */

cJSON* spine_scaled_number_encode(const spine_scaled_number_t* v) {
    if (!v) return NULL;
    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;
    if (!seq_add_num(arr, "number", (double)v->number))
        { cJSON_Delete(arr); return NULL; }
    if (v->scale != 0)
        if (!seq_add_num(arr, "scale", (double)v->scale))
            { cJSON_Delete(arr); return NULL; }
    return arr;
}

spine_err_t spine_scaled_number_decode(const cJSON* arr,
                                        spine_scaled_number_t* out) {
    if (!arr || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    const cJSON* n = seq_get(arr, "number");
    if (cJSON_IsNumber(n)) out->number = (int64_t)n->valuedouble;
    const cJSON* s = seq_get(arr, "scale");
    if (cJSON_IsNumber(s)) out->scale = (int8_t)s->valuedouble;
    return SPINE_OK;
}



/* ── LoadControlLimitListData encode ─────────────────────────────────────── */

cJSON* lc_limit_list_encode(const lc_limit_list_t* in) {
    if (!in) return NULL;

    /* list_arr = [item_seq1, item_seq2, ...] */
    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const lc_limit_t* lim = &in->items[i];

        /* item_seq = [{"limitId":N}, {"isLimitChangeable":B}, ...] */
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "limitId", (double)lim->limit_id))
            goto oom;
        if (lim->is_limit_changeable_present)
            if (!seq_add_bool(item_seq, "isLimitChangeable", lim->is_limit_changeable))
                goto oom;
        if (lim->is_limit_active_present)
            if (!seq_add_bool(item_seq, "isLimitActive", lim->is_limit_active))
                goto oom;
        if (lim->time_period.start_time_present || lim->time_period.end_time_present) {
            cJSON* tp_seq = cJSON_CreateArray();
            if (!tp_seq) goto oom;
            if (lim->time_period.start_time_present)
                if (!seq_add_str(tp_seq, "startTime", lim->time_period.start_time))
                    { cJSON_Delete(tp_seq); goto oom; }
            if (lim->time_period.end_time_present)
                if (!seq_add_str(tp_seq, "endTime", lim->time_period.end_time))
                    { cJSON_Delete(tp_seq); goto oom; }
            if (!seq_add_arr(item_seq, "timePeriod", tp_seq)) goto oom;
        }
        if (lim->value_present) {
            cJSON* val_arr = spine_scaled_number_encode(&lim->value);
            if (!val_arr) goto oom;
            if (!seq_add_arr(item_seq, "value", val_arr)) goto oom;
        }

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom:
        cJSON_Delete(item_seq);
        cJSON_Delete(list_arr);
        return NULL;
    }

    /* fn_seq = [{"loadControlLimitData": list_arr}] */
    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "loadControlLimitData", list_arr)) {
        cJSON_Delete(fn_seq); return NULL;
    }

    /* fn_data = {"loadControlLimitListData": fn_seq} */
    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "loadControlLimitListData", fn_seq);
    return fn_data;
}

/* ── LoadControlLimitListData decode ─────────────────────────────────────── */

spine_err_t lc_limit_list_decode(const cJSON* fn_data, lc_limit_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "loadControlLimitListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    /* fn_seq[0]["loadControlLimitData"] = list_arr */
    const cJSON* ldata = seq_get(fn_seq, "loadControlLimitData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_LC_LIMIT_MAX) break;
        lc_limit_t* lim = &out->items[out->count];
        memset(lim, 0, sizeof(*lim));

        const cJSON* lid = seq_get(item_seq, "limitId");
        if (cJSON_IsNumber(lid)) lim->limit_id = (uint32_t)lid->valuedouble;

        const cJSON* chg = seq_get(item_seq, "isLimitChangeable");
        if (cJSON_IsBool(chg)) {
            lim->is_limit_changeable         = cJSON_IsTrue(chg);
            lim->is_limit_changeable_present = true;
        }
        const cJSON* act = seq_get(item_seq, "isLimitActive");
        if (cJSON_IsBool(act)) {
            lim->is_limit_active         = cJSON_IsTrue(act);
            lim->is_limit_active_present = true;
        }
        const cJSON* val = seq_get(item_seq, "value");
        if (val) {
            spine_scaled_number_decode(val, &lim->value);
            lim->value_present = true;
        }
        /* timePeriod decode */
        const cJSON* tp = seq_get(item_seq, "timePeriod");
        if (tp && cJSON_IsArray(tp)) {
            const cJSON* st = seq_get(tp, "startTime");
            if (cJSON_IsString(st) && st->valuestring) {
                snprintf(lim->time_period.start_time,
                         sizeof(lim->time_period.start_time),
                         "%s", st->valuestring);
                lim->time_period.start_time_present = true;
            }
            const cJSON* et = seq_get(tp, "endTime");
            if (cJSON_IsString(et) && et->valuestring) {
                snprintf(lim->time_period.end_time,
                         sizeof(lim->time_period.end_time),
                         "%s", et->valuestring);
                lim->time_period.end_time_present = true;
            }
        }
        out->count++;
    }
    return SPINE_OK;
}

/* ── LoadControlLimitDescriptionListData encode ───────────────────────────── */

cJSON* lc_limit_desc_list_encode(const lc_limit_desc_list_t* in) {
    if (!in) return NULL;

    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const lc_limit_desc_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "limitId", (double)d->limit_id)) goto oom2;
        if (d->limit_type_present)
            if (!seq_add_str(item_seq, "limitType", d->limit_type)) goto oom2;
        if (d->limit_category_present)
            if (!seq_add_str(item_seq, "limitCategory", d->limit_category)) goto oom2;
        if (d->limit_direction_present)
            if (!seq_add_str(item_seq, "limitDirection", d->limit_direction)) goto oom2;
        if (d->measurement_id_present)
            if (!seq_add_num(item_seq, "measurementId",
                             (double)d->measurement_id)) goto oom2;
        if (d->unit_present)
            if (!seq_add_str(item_seq, "unit", d->unit)) goto oom2;
        if (d->scope_type_present)
            if (!seq_add_str(item_seq, "scopeType", d->scope_type)) goto oom2;
        if (d->label_present)
            if (!seq_add_str(item_seq, "label", d->label)) goto oom2;
        if (d->description_present)
            if (!seq_add_str(item_seq, "description", d->description)) goto oom2;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom2:
        cJSON_Delete(item_seq);
        cJSON_Delete(list_arr);
        return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "loadControlLimitDescriptionData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "loadControlLimitDescriptionListData", fn_seq);
    return fn_data;
}

/* ── LoadControlLimitDescriptionListData decode ───────────────────────────── */

spine_err_t lc_limit_desc_list_decode(const cJSON* fn_data,
                                       lc_limit_desc_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "loadControlLimitDescriptionListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq, "loadControlLimitDescriptionData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_LC_LIMIT_DESC_MAX) break;
        lc_limit_desc_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* lid = seq_get(item_seq, "limitId");
        if (cJSON_IsNumber(lid)) d->limit_id = (uint32_t)lid->valuedouble;

#define COPY_STR(json_key, dest, present_flag) do { \
    const cJSON* _v = seq_get(item_seq, json_key); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        snprintf(d->dest, sizeof(d->dest), "%s", _v->valuestring); \
        d->present_flag = true; } } while(0)

        COPY_STR("limitType",      limit_type,      limit_type_present);
        COPY_STR("limitCategory",  limit_category,  limit_category_present);
        COPY_STR("limitDirection", limit_direction, limit_direction_present);
        COPY_STR("unit",           unit,            unit_present);
        COPY_STR("scopeType",      scope_type,      scope_type_present);
#undef COPY_STR
        const cJSON* mid = seq_get(item_seq, "measurementId");
        if (cJSON_IsNumber(mid)) {
            d->measurement_id         = (uint32_t)mid->valuedouble;
            d->measurement_id_present = true;
        }
        const cJSON* lbl = seq_get(item_seq, "label");
        if (cJSON_IsString(lbl) && lbl->valuestring) {
            snprintf(d->label, sizeof(d->label), "%s", lbl->valuestring);
            d->label_present = true;
        }
        const cJSON* dsc = seq_get(item_seq, "description");
        if (cJSON_IsString(dsc) && dsc->valuestring) {
            snprintf(d->description, sizeof(d->description), "%s", dsc->valuestring);
            d->description_present = true;
        }

        out->count++;
    }
    return SPINE_OK;
}
