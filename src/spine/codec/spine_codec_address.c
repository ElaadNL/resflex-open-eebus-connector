// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_address.c
 */

#include "spine_codec_address.h"

#include <stdio.h>
#include <string.h>

/* ── Constructors ─────────────────────────────────────────────────────────── */

void spine_addr_make(spine_addr_t* out,
                     const char*   device_id,
                     uint8_t       entity,
                     uint16_t      feature) {
    if (!out) return;
    spine_addr_clear(out);
    if (device_id && device_id[0] != '\0')
        snprintf(out->device, sizeof(out->device), "%s", device_id);
    out->entity[0]   = entity;
    out->entity_depth = 1;
    out->feature      = feature;
}

void spine_addr_make2(spine_addr_t* out,
                      const char*   device_id,
                      uint8_t       entity1,
                      uint8_t       entity2,
                      uint16_t      feature) {
    if (!out) return;
    spine_addr_clear(out);
    if (device_id && device_id[0] != '\0')
        snprintf(out->device, sizeof(out->device), "%s", device_id);
    out->entity[0]   = entity1;
    out->entity[1]   = entity2;
    out->entity_depth = 2;
    out->feature      = feature;
}

/* ── Compare ──────────────────────────────────────────────────────────────── */

bool spine_addr_equal(const spine_addr_t* a, const spine_addr_t* b) {
    if (!a || !b) return false;
    if (strcmp(a->device, b->device) != 0) return false;
    if (a->entity_depth != b->entity_depth) return false;
    for (uint8_t i = 0; i < a->entity_depth; i++)
        if (a->entity[i] != b->entity[i]) return false;
    return a->feature == b->feature;
}

bool spine_addr_entity_equal(const spine_addr_t* a, const spine_addr_t* b) {
    if (!a || !b) return false;
    if (strcmp(a->device, b->device) != 0) return false;
    if (a->entity_depth != b->entity_depth) return false;
    for (uint8_t i = 0; i < a->entity_depth; i++)
        if (a->entity[i] != b->entity[i]) return false;
    return true;
}

bool spine_addr_device_equal(const spine_addr_t* a, const spine_addr_t* b) {
    if (!a || !b) return false;
    return strcmp(a->device, b->device) == 0;
}

bool spine_addr_entity_is_prefix(const spine_addr_t* a, const spine_addr_t* b) {
    if (!a || !b) return false;
    if (strcmp(a->device, b->device) != 0) return false;
    if (a->entity_depth > b->entity_depth) return false;
    for (uint8_t i = 0; i < a->entity_depth; i++)
        if (a->entity[i] != b->entity[i]) return false;
    return true;
}

/* ── JSON encode ──────────────────────────────────────────────────────────── */
/*
 * FeatureAddressType is xs:complexContent + xs:sequence, so it uses an Array.
 * [{"device":"..."}, {"entity":[0]}, {"feature":0}]
 * When include_device=false, the device element is omitted.
 */
cJSON* spine_addr_encode(const spine_addr_t* addr, bool include_device) {
    if (!addr) return NULL;

    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;

    /* {"device": "..."} */
    if (include_device && addr->device[0] != '\0') {
        cJSON* dev_obj = cJSON_CreateObject();
        if (!dev_obj) { cJSON_Delete(arr); return NULL; }
        cJSON_AddStringToObject(dev_obj, "device", addr->device);
        cJSON_AddItemToArray(arr, dev_obj);
    }

    /* {"entity": [0, 1, ...]} */
    {
        cJSON* ent_obj = cJSON_CreateObject();
        if (!ent_obj) { cJSON_Delete(arr); return NULL; }
        cJSON* ent_arr = cJSON_CreateArray();
        if (!ent_arr) { cJSON_Delete(ent_obj); cJSON_Delete(arr); return NULL; }
        for (uint8_t i = 0; i < addr->entity_depth; i++)
            cJSON_AddItemToArray(ent_arr, cJSON_CreateNumber(addr->entity[i]));
        cJSON_AddItemToObject(ent_obj, "entity", ent_arr);
        cJSON_AddItemToArray(arr, ent_obj);
    }

    /* {"feature": 0} */
    {
        cJSON* feat_obj = cJSON_CreateObject();
        if (!feat_obj) { cJSON_Delete(arr); return NULL; }
        cJSON_AddNumberToObject(feat_obj, "feature", (double)addr->feature);
        cJSON_AddItemToArray(arr, feat_obj);
    }

    return arr;
}

/* ── JSON decode ──────────────────────────────────────────────────────────── */
/*
 * Input is an Array: [{"device":"..."}, {"entity":[0]}, {"feature":0}]
 * Iterate through each element and extract the corresponding fields.
 */
spine_err_t spine_addr_decode(const cJSON* arr, spine_addr_t* out) {
    if (!arr || !out) return SPINE_ERR_INVALID;
    spine_addr_clear(out);

    if (!cJSON_IsArray(arr)) return SPINE_ERR_PARSE;

    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (!cJSON_IsObject(elem) || !elem->child) continue;
        const char* key = elem->child->string;
        if (!key) continue;

        if (strcmp(key, "device") == 0) {
            const cJSON* v = elem->child;
            if (cJSON_IsString(v) && v->valuestring)
                snprintf(out->device, sizeof(out->device), "%s", v->valuestring);
        } else if (strcmp(key, "entity") == 0) {
            const cJSON* v = elem->child;
            if (cJSON_IsArray(v)) {
                const cJSON* e;
                cJSON_ArrayForEach(e, v) {
                    if (out->entity_depth >= SPINE_ENTITY_DEPTH_MAX) break;
                    if (cJSON_IsNumber(e))
                        out->entity[out->entity_depth++] = (uint8_t)e->valuedouble;
                }
            }
        } else if (strcmp(key, "feature") == 0) {
            const cJSON* v = elem->child;
            if (cJSON_IsNumber(v))
                out->feature = (uint16_t)v->valuedouble;
        }
    }

    return SPINE_OK;
}

/* ── Conversion from nm_addr_t ────────────────────────────────────────────── */

void spine_addr_from_nm(spine_addr_t* out, const nm_addr_t* nm,
                        const char* fallback_device) {
    if (!out || !nm) return;
    spine_addr_clear(out);

    if (nm->device[0] != '\0')
        snprintf(out->device, sizeof(out->device), "%s", nm->device);
    else if (fallback_device && fallback_device[0] != '\0')
        snprintf(out->device, sizeof(out->device), "%s", fallback_device);

    uint8_t depth = nm->entity_depth;
    if (depth > SPINE_ENTITY_DEPTH_MAX) depth = SPINE_ENTITY_DEPTH_MAX;
    for (uint8_t i = 0; i < depth; i++)
        out->entity[i] = nm->entity[i];
    out->entity_depth = depth;

    if (nm->feature_present)
        out->feature = nm->feature;
}

/* ── String representation ────────────────────────────────────────────────── */

void spine_addr_to_str(const spine_addr_t* addr, char* buf, size_t buf_sz) {
    if (!addr || !buf || buf_sz == 0) return;

    int n = 0;
    if (addr->device[0] != '\0')
        n += snprintf(buf + n, buf_sz - (size_t)n, "%s", addr->device);

    n += snprintf(buf + n, buf_sz - (size_t)n, "/e[");
    for (uint8_t i = 0; i < addr->entity_depth; i++) {
        if (i > 0)
            n += snprintf(buf + n, buf_sz - (size_t)n, ",");
        n += snprintf(buf + n, buf_sz - (size_t)n, "%u", addr->entity[i]);
    }
    snprintf(buf + n, buf_sz - (size_t)n, "]/f%u", addr->feature);
}
