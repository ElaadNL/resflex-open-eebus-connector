// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_result.c
 *
 * ResultDataType xs:sequence → fn_data = {"resultData":[items]}
 * items = [{"errorNumber":0}, {"description":"..."}]
 */

#include "spine_codec_result.h"
#include <string.h>
#include <stdio.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

/* Add {"key": str_val} to seq_arr */
static bool seq_add_str(cJSON* arr, const char* key, const char* val) {
    cJSON* o = cJSON_CreateObject();
    if (!o) return false;
    cJSON_AddStringToObject(o, key, val);
    cJSON_AddItemToArray(arr, o);
    return true;
}

/* Add {"key": num_val} to seq_arr */
static bool seq_add_num(cJSON* arr, const char* key, double val) {
    cJSON* o = cJSON_CreateObject();
    if (!o) return false;
    cJSON_AddNumberToObject(o, key, val);
    cJSON_AddItemToArray(arr, o);
    return true;
}

/* Get the value for a specific key from an xs:sequence Array */
static const cJSON* seq_get(const cJSON* arr, const char* key) {
    if (!cJSON_IsArray(arr)) return NULL;
    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (!cJSON_IsObject(elem) || !elem->child) continue;
        if (strcmp(elem->child->string, key) == 0)
            return elem->child;
    }
    return NULL;
}

/* ── decode ─────────────────────────────────────────────────────────────── */

spine_err_t spine_result_data_decode(const cJSON* fn_data,
                                      spine_result_data_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    /* fn_data = {"resultData": [seq_array]} */
    const cJSON* fn_arr = cJSON_GetObjectItemCaseSensitive(fn_data, "resultData");
    if (!cJSON_IsArray(fn_arr)) return SPINE_ERR_PARSE;

    const cJSON* en = seq_get(fn_arr, "errorNumber");
    if (cJSON_IsNumber(en))
        out->error_number = (uint32_t)en->valuedouble;

    const cJSON* desc = seq_get(fn_arr, "description");
    if (cJSON_IsString(desc) && desc->valuestring) {
        snprintf(out->description, sizeof(out->description),
                 "%s", desc->valuestring);
        out->description_present = true;
    }

    return SPINE_OK;
}

/* ── encode ─────────────────────────────────────────────────────────────── */

cJSON* spine_result_data_encode(const spine_result_data_t* in) {
    if (!in) return NULL;

    /* fn_seq = [{"errorNumber":N}, {"description":"..."}] */
    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) return NULL;

    if (!seq_add_num(fn_seq, "errorNumber", (double)in->error_number))
        { cJSON_Delete(fn_seq); return NULL; }

    if (in->description_present)
        if (!seq_add_str(fn_seq, "description", in->description))
            { cJSON_Delete(fn_seq); return NULL; }

    /* fn_data = {"resultData": fn_seq} */
    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "resultData", fn_seq);
    return fn_data;
}
