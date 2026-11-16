// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_sub_bind.c
 *
 * NodeManagementSubscriptionRequestCallType xs:sequence → Array:
 * fn_data = {"nodeManagementSubscriptionRequestCall": [
 *   {"subscriptionRequest": [
 *     {"clientAddress": [{"device":"..."}, {"entity":[0]}, {"feature":0}]},
 *     {"serverAddress": [{"device":"..."}, {"entity":[0]}, {"feature":0}]},
 *     {"serverFeatureType": "LoadControl"}
 *   ]}
 * ]}
 *
 * NodeManagementBindingRequestCallType has the same structure, using bindingRequest.
 */

#include "spine_codec_sub_bind.h"

#include <stdio.h>
#include <string.h>

#define FN_SUBSCRIBE        "nodeManagementSubscriptionRequestCall"
#define FN_BIND             "nodeManagementBindingRequestCall"
#define FN_DELETE_SUBSCRIBE "nodeManagementSubscriptionDeleteCall"
#define FN_DELETE_BIND      "nodeManagementBindingDeleteCall"

/* ── seq helpers ─────────────────────────────────────────────────────────── */

static bool seq_add_str(cJSON* a, const char* k, const char* v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddStringToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_arr(cJSON* a, const char* k, cJSON* inner) {
    cJSON* o = cJSON_CreateObject();
    if (!o) { cJSON_Delete(inner); return false; }
    cJSON_AddItemToObject(o, k, inner);
    cJSON_AddItemToArray(a, o);
    return true;
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

/* ── nm_addr_t: FeatureAddressType xs:sequence → Array ───────────────────── */

static cJSON* encode_addr(const nm_addr_t* addr) {
    /* [{"device":"..."}, {"entity":[0]}, {"feature":0}] */
    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;

    if (addr->device[0] != '\0') {
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(arr); return NULL; }
        cJSON_AddStringToObject(o, "device", addr->device);
        cJSON_AddItemToArray(arr, o);
    }

    {
        cJSON* ent_arr = cJSON_CreateArray();
        if (!ent_arr) { cJSON_Delete(arr); return NULL; }
        for (uint8_t i = 0; i < addr->entity_depth; i++)
            cJSON_AddItemToArray(ent_arr, cJSON_CreateNumber(addr->entity[i]));
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(ent_arr); cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToObject(o, "entity", ent_arr);
        cJSON_AddItemToArray(arr, o);
    }

    if (addr->feature_present) {
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(arr); return NULL; }
        cJSON_AddNumberToObject(o, "feature", (double)addr->feature);
        cJSON_AddItemToArray(arr, o);
    }

    return arr;
}

static void decode_addr(const cJSON* arr, nm_addr_t* out) {
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsArray(arr)) return;

    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (!cJSON_IsObject(elem) || !elem->child) continue;
        const char*  k = elem->child->string;
        const cJSON* v = elem->child;

        if (strcmp(k, "device") == 0) {
            if (cJSON_IsString(v) && v->valuestring)
                snprintf(out->device, sizeof(out->device), "%s", v->valuestring);
        } else if (strcmp(k, "entity") == 0) {
            if (cJSON_IsArray(v)) {
                const cJSON* e;
                cJSON_ArrayForEach(e, v) {
                    if (out->entity_depth >= NM_ENTITY_MAX_DEPTH) break;
                    if (cJSON_IsNumber(e))
                        out->entity[out->entity_depth++] = (uint8_t)e->valuedouble;
                }
            }
        } else if (strcmp(k, "feature") == 0) {
            if (cJSON_IsNumber(v)) {
                out->feature         = (uint16_t)v->valuedouble;
                out->feature_present = true;
            }
        }
    }
}

/* ── Common encode ────────────────────────────────────────────────────────── */
/*
 * fn_data = {"nodeManagementSubscriptionRequestCall": [
 *   {"subscriptionRequest": [
 *     {"clientAddress": [...]},
 *     {"serverAddress": [...]},
 *     {"serverFeatureType": "..."}
 *   ]}
 * ]}
 */
static cJSON* encode_sb_fn_data(const nm_addr_t* client_addr,
                                 const nm_addr_t* server_addr,
                                 const char*      server_feature_type,
                                 const char*      fn_name,
                                 const char*      req_key) {
    if (!client_addr || !server_addr) return NULL;

    /* req_seq = [{"clientAddress":[...]}, {"serverAddress":[...]}, ...] */
    cJSON* req_seq = cJSON_CreateArray();
    if (!req_seq) return NULL;

    cJSON* ca = encode_addr(client_addr);
    if (!ca || !seq_add_arr(req_seq, "clientAddress", ca))
        { cJSON_Delete(req_seq); return NULL; }

    cJSON* sa = encode_addr(server_addr);
    if (!sa || !seq_add_arr(req_seq, "serverAddress", sa))
        { cJSON_Delete(req_seq); return NULL; }

    if (server_feature_type && server_feature_type[0] != '\0')
        if (!seq_add_str(req_seq, "serverFeatureType", server_feature_type))
            { cJSON_Delete(req_seq); return NULL; }

    /* fn_seq = [{"subscriptionRequest": req_seq}] */
    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(req_seq); return NULL; }
    if (!seq_add_arr(fn_seq, req_key, req_seq))
        { cJSON_Delete(fn_seq); return NULL; }

    /* fn_data = {"nodeManagementSubscriptionRequestCall": fn_seq} */
    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, fn_name, fn_seq);
    return fn_data;
}

/* ── Common decode ────────────────────────────────────────────────────────── */
/*
 * fn_data = {"nodeManagementSubscriptionRequestCall": [
 *   {"subscriptionRequest": [
 *     {"clientAddress":[...]}, {"serverAddress":[...]}, ...
 *   ]}
 * ]}
 */
static spine_err_t decode_sb_fn_data(const cJSON*        fn_data,
                                      const char*         fn_name,
                                      const char*         req_key,
                                      spine_sb_request_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data, fn_name);
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    /* fn_seq[0] = {"subscriptionRequest": req_seq} */
    const cJSON* req_seq = seq_get(fn_seq, req_key);
    if (!cJSON_IsArray(req_seq)) return SPINE_ERR_PARSE;

    const cJSON* ca = seq_get(req_seq, "clientAddress");
    const cJSON* sa = seq_get(req_seq, "serverAddress");
    if (!ca || !sa) return SPINE_ERR_PARSE;

    decode_addr(ca, &out->client_addr);
    decode_addr(sa, &out->server_addr);

    const cJSON* sft = seq_get(req_seq, "serverFeatureType");
    if (cJSON_IsString(sft) && sft->valuestring) {
        snprintf(out->server_feature_type, sizeof(out->server_feature_type),
                 "%s", sft->valuestring);
        out->server_feature_type_present = true;
    }

    return SPINE_OK;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

spine_err_t spine_sb_request_decode(const cJSON*        fn_data,
                                    spine_sb_request_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;

    /* Distinguish subscribe/bind by fn_name. Set out->type AFTER the inner
     * decode runs — the helper memsets `out` at entry and would otherwise
     * clobber the type field (which defaults to SUBSCRIBE == 0). */
    if (cJSON_GetObjectItemCaseSensitive(fn_data, FN_SUBSCRIBE)) {
        spine_err_t rc = decode_sb_fn_data(fn_data, FN_SUBSCRIBE,
                                            "subscriptionRequest", out);
        if (rc == SPINE_OK) out->type = SPINE_SB_REQUEST_SUBSCRIBE;
        return rc;
    }
    if (cJSON_GetObjectItemCaseSensitive(fn_data, FN_BIND)) {
        spine_err_t rc = decode_sb_fn_data(fn_data, FN_BIND,
                                            "bindingRequest", out);
        if (rc == SPINE_OK) out->type = SPINE_SB_REQUEST_BIND;
        return rc;
    }
    return SPINE_ERR_PARSE;
}

cJSON* spine_sb_encode_subscribe(const nm_addr_t* client_addr,
                                  const nm_addr_t* server_addr,
                                  const char*      server_feature_type) {
    return encode_sb_fn_data(client_addr, server_addr, server_feature_type,
                             FN_SUBSCRIBE, "subscriptionRequest");
}

cJSON* spine_sb_encode_bind(const nm_addr_t* client_addr,
                             const nm_addr_t* server_addr,
                             const char*      server_feature_type) {
    return encode_sb_fn_data(client_addr, server_addr, server_feature_type,
                             FN_BIND, "bindingRequest");
}

/* ── Delete encode ────────────────────────────────────────────────────────── */

static cJSON* encode_sb_delete(const nm_addr_t* client_addr,
                                const nm_addr_t* server_addr,
                                const char*      fn_name,
                                const char*      del_key) {
    if (!client_addr || !server_addr) return NULL;

    cJSON* del_seq = cJSON_CreateArray();
    if (!del_seq) return NULL;

    cJSON* ca = encode_addr(client_addr);
    if (!ca || !seq_add_arr(del_seq, "clientAddress", ca))
        { cJSON_Delete(del_seq); return NULL; }

    cJSON* sa = encode_addr(server_addr);
    if (!sa || !seq_add_arr(del_seq, "serverAddress", sa))
        { cJSON_Delete(del_seq); return NULL; }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(del_seq); return NULL; }
    if (!seq_add_arr(fn_seq, del_key, del_seq))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, fn_name, fn_seq);
    return fn_data;
}

cJSON* spine_sb_encode_delete_subscribe(const nm_addr_t* client_addr,
                                         const nm_addr_t* server_addr) {
    return encode_sb_delete(client_addr, server_addr,
                             FN_DELETE_SUBSCRIBE, "subscriptionDelete");
}

cJSON* spine_sb_encode_delete_bind(const nm_addr_t* client_addr,
                                    const nm_addr_t* server_addr) {
    return encode_sb_delete(client_addr, server_addr,
                             FN_DELETE_BIND, "bindingDelete");
}

/* ── Delete decode ────────────────────────────────────────────────────────── */

static spine_err_t decode_sb_delete(const cJSON* fn_data,
                                     const char*  fn_name,
                                     const char*  del_key,
                                     spine_sb_delete_t* out) {
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data, fn_name);
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* del_seq = seq_get(fn_seq, del_key);
    if (!cJSON_IsArray(del_seq)) return SPINE_ERR_PARSE;

    const cJSON* ca = seq_get(del_seq, "clientAddress");
    const cJSON* sa = seq_get(del_seq, "serverAddress");
    if (!ca || !sa) return SPINE_ERR_PARSE;

    decode_addr(ca, &out->client_addr);
    decode_addr(sa, &out->server_addr);
    return SPINE_OK;
}

spine_err_t spine_sb_delete_decode(const cJSON* fn_data, spine_sb_delete_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;

    /* Set out->type AFTER the inner decode runs — the helper memsets `out`
     * at entry and would otherwise clobber the type field (which defaults
     * to SUBSCRIBE == 0, causing binding deletes to be processed as
     * subscription deletes). */
    if (cJSON_GetObjectItemCaseSensitive(fn_data, FN_DELETE_SUBSCRIBE)) {
        spine_err_t rc = decode_sb_delete(fn_data, FN_DELETE_SUBSCRIBE,
                                           "subscriptionDelete", out);
        if (rc == SPINE_OK) out->type = SPINE_SB_REQUEST_SUBSCRIBE;
        return rc;
    }
    if (cJSON_GetObjectItemCaseSensitive(fn_data, FN_DELETE_BIND)) {
        spine_err_t rc = decode_sb_delete(fn_data, FN_DELETE_BIND,
                                           "bindingDelete", out);
        if (rc == SPINE_OK) out->type = SPINE_SB_REQUEST_BIND;
        return rc;
    }
    return SPINE_ERR_PARSE;
}
