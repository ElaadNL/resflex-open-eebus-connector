// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_dev_cfg.c
 *
 * DeviceConfigurationKeyValueListDataType xs:sequence:
 *   fn_data = {"deviceConfigurationKeyValueListData": [
 *     {"deviceConfigurationKeyValueData": [item_seq1, item_seq2, ...]}
 *   ]}
 *
 * DeviceConfigurationKeyValueDataType xs:sequence:
 *   [{"keyId":N}, {"value":[...]}, {"isValueChangeable":B}]
 *
 * DeviceConfigurationKeyValueValueType is xs:choice -> Array
 * (each value type is identified by tag name)
 */

#include "spine_codec_dev_cfg.h"
#include "spine_codec_load_ctrl.h"  /* spine_scaled_number_encode/decode */
#include <string.h>
#include <stdio.h>

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

/* Encode value into xs:choice Array */
static cJSON* encode_value(const dev_cfg_kv_data_t* kv) {
    cJSON* val_arr = cJSON_CreateArray();
    if (!val_arr) return NULL;

    switch (kv->value_type) {
        case DEV_CFG_VALUE_BOOLEAN:
            if (!seq_add_bool(val_arr, "boolean", kv->value.boolean_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_DURATION:
            if (!seq_add_str(val_arr, "duration", kv->value.duration_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_STRING:
            if (!seq_add_str(val_arr, "string", kv->value.string_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_SCALED_NUM: {
            cJSON* sn = spine_scaled_number_encode(&kv->value.scaled_num_val);
            if (!sn) goto fail;
            if (!seq_add_arr(val_arr, "scaledNumber", sn)) goto fail;
            break;
        }
        case DEV_CFG_VALUE_INTEGER:
            if (!seq_add_num(val_arr, "integer", (double)kv->value.integer_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_DATE:
            if (!seq_add_str(val_arr, "date", kv->value.string_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_DATE_TIME:
            if (!seq_add_str(val_arr, "dateTime", kv->value.string_val))
                goto fail;
            break;
        case DEV_CFG_VALUE_TIME:
            if (!seq_add_str(val_arr, "time", kv->value.string_val))
                goto fail;
            break;
        default: break;
    }
    return val_arr;
fail:
    cJSON_Delete(val_arr);
    return NULL;
}

/* ── encode ─────────────────────────────────────────────────────────────── */

cJSON* dev_cfg_kv_list_encode(const dev_cfg_kv_list_t* in) {
    if (!in) return NULL;

    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const dev_cfg_kv_data_t* kv = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "keyId", (double)kv->key_id)) goto oom;

        if (kv->value_present) {
            cJSON* val_arr = encode_value(kv);
            if (!val_arr) goto oom;
            if (!seq_add_arr(item_seq, "value", val_arr)) goto oom;
        }

        if (kv->is_value_changeable_present)
            if (!seq_add_bool(item_seq, "isValueChangeable",
                              kv->is_value_changeable)) goto oom;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom:
        cJSON_Delete(item_seq);
        cJSON_Delete(list_arr);
        return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "deviceConfigurationKeyValueData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "deviceConfigurationKeyValueListData", fn_seq);
    return fn_data;
}

/* ── description list encode ──────────────────────────────────────────────── */

cJSON* dev_cfg_kv_desc_list_encode(const dev_cfg_kv_desc_list_t* in) {
    if (!in) return NULL;
    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const dev_cfg_kv_desc_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "keyId", (double)d->key_id)) goto oom_d;
        if (d->key_name_present)
            if (!seq_add_str(item_seq, "keyName", d->key_name)) goto oom_d;
        if (d->value_type_present)
            if (!seq_add_str(item_seq, "valueType", d->value_type)) goto oom_d;
        if (d->unit_present)
            if (!seq_add_str(item_seq, "unit", d->unit)) goto oom_d;
        if (d->label_present)
            if (!seq_add_str(item_seq, "label", d->label)) goto oom_d;
        if (d->description_present)
            if (!seq_add_str(item_seq, "description", d->description)) goto oom_d;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom_d:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "deviceConfigurationKeyValueDescriptionData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data,
        "deviceConfigurationKeyValueDescriptionListData", fn_seq);
    return fn_data;
}

/* ── description list decode ──────────────────────────────────────────────── */

spine_err_t dev_cfg_kv_desc_list_decode(const cJSON* fn_data,
                                         dev_cfg_kv_desc_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data,
        "deviceConfigurationKeyValueDescriptionListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq,
        "deviceConfigurationKeyValueDescriptionData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_DEV_CFG_KV_MAX) break;
        dev_cfg_kv_desc_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* kid = seq_get(item_seq, "keyId");
        if (cJSON_IsNumber(kid)) d->key_id = (uint32_t)kid->valuedouble;

#define COPY_STR_D(jk, df, pf) do { \
    const cJSON* _v = seq_get(item_seq, jk); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        snprintf(d->df, sizeof(d->df), "%s", _v->valuestring); d->pf = true; } \
} while(0)
        COPY_STR_D("keyName",     key_name,     key_name_present);
        COPY_STR_D("valueType",   value_type,   value_type_present);
        COPY_STR_D("unit",        unit,          unit_present);
        COPY_STR_D("label",       label,         label_present);
        COPY_STR_D("description", description,   description_present);
#undef COPY_STR_D
        out->count++;
    }
    return SPINE_OK;
}

/* ── kv list decode ──────────────────────────────────────────────────────── */

spine_err_t dev_cfg_kv_list_decode(const cJSON* fn_data,
                                    dev_cfg_kv_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "deviceConfigurationKeyValueListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq, "deviceConfigurationKeyValueData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_DEV_CFG_KV_MAX) break;
        dev_cfg_kv_data_t* kv = &out->items[out->count];
        memset(kv, 0, sizeof(*kv));

        const cJSON* kid = seq_get(item_seq, "keyId");
        if (cJSON_IsNumber(kid)) kv->key_id = (uint32_t)kid->valuedouble;

        const cJSON* val_arr = seq_get(item_seq, "value");
        if (cJSON_IsArray(val_arr)) {
            kv->value_present = true;
            /* val_arr is an xs:choice Array - determine type by the first element's key */
            const cJSON* choice = cJSON_GetArrayItem(val_arr, 0);
            if (choice && cJSON_IsObject(choice) && choice->child) {
                const char* vk = choice->child->string;
                const cJSON* vv = choice->child;
                if (strcmp(vk, "boolean") == 0 && cJSON_IsBool(vv)) {
                    kv->value_type      = DEV_CFG_VALUE_BOOLEAN;
                    kv->value.boolean_val  = cJSON_IsTrue(vv);
                } else if (strcmp(vk, "duration") == 0 && cJSON_IsString(vv)) {
                    kv->value_type = DEV_CFG_VALUE_DURATION;
                    snprintf(kv->value.duration_val,
                             sizeof(kv->value.duration_val),
                             "%s", vv->valuestring);
                } else if (strcmp(vk, "string") == 0 && cJSON_IsString(vv)) {
                    kv->value_type = DEV_CFG_VALUE_STRING;
                    snprintf(kv->value.string_val,
                             sizeof(kv->value.string_val),
                             "%s", vv->valuestring);
                } else if (strcmp(vk, "scaledNumber") == 0) {
                    kv->value_type = DEV_CFG_VALUE_SCALED_NUM;
                    spine_scaled_number_decode(vv, &kv->value.scaled_num_val);
                } else if (strcmp(vk, "integer") == 0 && cJSON_IsNumber(vv)) {
                    kv->value_type          = DEV_CFG_VALUE_INTEGER;
                    kv->value.integer_val   = (int64_t)vv->valuedouble;
                } else if (strcmp(vk, "date") == 0 && cJSON_IsString(vv)) {
                    kv->value_type = DEV_CFG_VALUE_DATE;
                    snprintf(kv->value.string_val,
                             sizeof(kv->value.string_val),
                             "%s", vv->valuestring);
                } else if (strcmp(vk, "dateTime") == 0 && cJSON_IsString(vv)) {
                    kv->value_type = DEV_CFG_VALUE_DATE_TIME;
                    snprintf(kv->value.string_val,
                             sizeof(kv->value.string_val),
                             "%s", vv->valuestring);
                } else if (strcmp(vk, "time") == 0 && cJSON_IsString(vv)) {
                    kv->value_type = DEV_CFG_VALUE_TIME;
                    snprintf(kv->value.string_val,
                             sizeof(kv->value.string_val),
                             "%s", vv->valuestring);
                }
            }
        }

        const cJSON* ivc = seq_get(item_seq, "isValueChangeable");
        if (cJSON_IsBool(ivc)) {
            kv->is_value_changeable         = cJSON_IsTrue(ivc);
            kv->is_value_changeable_present = true;
        }
        out->count++;
    }
    return SPINE_OK;
}
