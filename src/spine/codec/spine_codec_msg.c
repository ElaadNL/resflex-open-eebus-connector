// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_msg.c
 *
 * SPINE header encode/decode and message construction functions.
 * Protocol Spec v1.3.0, §5.2.
 */

#include "spine_codec_msg.h"

#include <stdio.h>
#include <string.h>

/* ── cmdClassifier string conversion ──────────────────────────────────────── */

const char* spine_cmd_str(spine_cmd_t cmd) {
    switch (cmd) {
        case SPINE_CMD_READ:   return "read";
        case SPINE_CMD_REPLY:  return "reply";
        case SPINE_CMD_WRITE:  return "write";
        case SPINE_CMD_CALL:   return "call";
        case SPINE_CMD_NOTIFY: return "notify";
        case SPINE_CMD_RESULT: return "result";
        default:               return "unknown";
    }
}

static spine_cmd_t cmd_from_str(const char* s) {
    if (!s)                      return SPINE_CMD_READ;
    if (!strcmp(s, "read"))      return SPINE_CMD_READ;
    if (!strcmp(s, "reply"))     return SPINE_CMD_REPLY;
    if (!strcmp(s, "write"))     return SPINE_CMD_WRITE;
    if (!strcmp(s, "call"))      return SPINE_CMD_CALL;
    if (!strcmp(s, "notify"))    return SPINE_CMD_NOTIFY;
    if (!strcmp(s, "result"))    return SPINE_CMD_RESULT;
    return SPINE_CMD_READ;
}

/* ── Header encode ────────────────────────────────────────────────────────── */
/*
 * HeaderType is xs:sequence -> Array.
 * Each field goes into the array as an independent {"key": val} object.
 * [{"specificationVersion":"1.3.0"}, {"addressSource":[...]}, ...]
 */
cJSON* spine_header_encode(const spine_header_t* hdr) {
    if (!hdr) return NULL;

    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;

#define HDR_ADD_STR(key, val) do { \
    cJSON* o = cJSON_CreateObject(); \
    if (!o) { cJSON_Delete(arr); return NULL; } \
    cJSON_AddStringToObject(o, (key), (val)); \
    cJSON_AddItemToArray(arr, o); } while(0)

#define HDR_ADD_NUM(key, val) do { \
    cJSON* o = cJSON_CreateObject(); \
    if (!o) { cJSON_Delete(arr); return NULL; } \
    cJSON_AddNumberToObject(o, (key), (double)(val)); \
    cJSON_AddItemToArray(arr, o); } while(0)

#define HDR_ADD_BOOL(key, val) do { \
    cJSON* o = cJSON_CreateObject(); \
    if (!o) { cJSON_Delete(arr); return NULL; } \
    cJSON_AddBoolToObject(o, (key), (val)); \
    cJSON_AddItemToArray(arr, o); } while(0)

#define HDR_ADD_ADDR(key, addr, inc_dev) do { \
    cJSON* a = spine_addr_encode((addr), (inc_dev)); \
    if (!a) { cJSON_Delete(arr); return NULL; } \
    cJSON* o = cJSON_CreateObject(); \
    if (!o) { cJSON_Delete(a); cJSON_Delete(arr); return NULL; } \
    cJSON_AddItemToObject(o, (key), a); \
    cJSON_AddItemToArray(arr, o); } while(0)

    HDR_ADD_STR("specificationVersion", hdr->spec_version);
    HDR_ADD_ADDR("addressSource",      &hdr->src, true);
    HDR_ADD_ADDR("addressDestination", &hdr->dst, true);
    HDR_ADD_NUM("msgCounter", hdr->msg_counter);
    if (hdr->msg_counter_ref_present)
        HDR_ADD_NUM("msgCounterReference", hdr->msg_counter_ref);
    HDR_ADD_STR("cmdClassifier", spine_cmd_str(hdr->cmd));
    if (hdr->ack_request)
        HDR_ADD_BOOL("ackRequest", true);

#undef HDR_ADD_STR
#undef HDR_ADD_NUM
#undef HDR_ADD_BOOL
#undef HDR_ADD_ADDR

    return arr;
}

/* ── Header decode ────────────────────────────────────────────────────────── */
/*
 * Input is an Array: [{"specificationVersion":"..."}, {"addressSource":[...]}, ...]
 * Iterate through each element and dispatch by key name.
 */
spine_err_t spine_header_decode(const cJSON* arr, spine_header_t* out) {
    if (!arr || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    if (!cJSON_IsArray(arr)) return SPINE_ERR_PARSE;

    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (!cJSON_IsObject(elem) || !elem->child) continue;
        const char* key = elem->child->string;
        const cJSON* val = elem->child;
        if (!key) continue;

        if (strcmp(key, "specificationVersion") == 0) {
            if (cJSON_IsString(val) && val->valuestring)
                snprintf(out->spec_version, sizeof(out->spec_version),
                         "%s", val->valuestring);
        } else if (strcmp(key, "addressSource") == 0) {
            spine_addr_decode(val, &out->src);
        } else if (strcmp(key, "addressDestination") == 0) {
            spine_addr_decode(val, &out->dst);
        } else if (strcmp(key, "msgCounter") == 0) {
            if (cJSON_IsNumber(val))
                out->msg_counter = (uint64_t)val->valuedouble;
            else return SPINE_ERR_PARSE;
        } else if (strcmp(key, "msgCounterReference") == 0) {
            if (cJSON_IsNumber(val)) {
                out->msg_counter_ref         = (uint64_t)val->valuedouble;
                out->msg_counter_ref_present = true;
            }
        } else if (strcmp(key, "cmdClassifier") == 0) {
            if (!cJSON_IsString(val)) return SPINE_ERR_PARSE;
            out->cmd = cmd_from_str(val->valuestring);
        } else if (strcmp(key, "ackRequest") == 0) {
            if (cJSON_IsBool(val))
                out->ack_request = cJSON_IsTrue(val);
        }
    }

    if (out->spec_version[0] == '\0')
        snprintf(out->spec_version, sizeof(out->spec_version),
                 "%s", SPINE_SPEC_VERSION);

    return SPINE_OK;
}

/* ── Message construction: internal common header initialization ──────────── */

static void init_header(spine_header_t*     hdr,
                         const spine_addr_t* src,
                         const spine_addr_t* dst,
                         spine_cmd_t         cmd,
                         uint64_t            msg_counter,
                         bool                ack_request) {
    memset(hdr, 0, sizeof(*hdr));
    snprintf(hdr->spec_version, sizeof(hdr->spec_version), "%s", SPINE_SPEC_VERSION);
    hdr->src         = *src;
    hdr->dst         = *dst;
    hdr->msg_counter = msg_counter;
    hdr->cmd         = cmd;
    hdr->ack_request = ack_request;
}

static void init_payload(spine_msg_payload_t* p,
                          const char*          fn_name,
                          cJSON*               fn_data) {
    memset(p, 0, sizeof(*p));
    if (fn_name)
        snprintf(p->fn_name, sizeof(p->fn_name), "%s", fn_name);
    p->fn_data = fn_data;   /* ownership not transferred */
}

/* ── spine_make_msg_req ───────────────────────────────────────────────────── */

void spine_make_msg_req(spine_msg_t*        out,
                        const spine_addr_t* src,
                        const spine_addr_t* dst,
                        spine_cmd_t         req_cmd,
                        uint64_t            msg_counter,
                        bool                ack_request,
                        const char*         fn_name,
                        cJSON*              fn_data) {
    if (!out || !src || !dst) return;
    memset(out, 0, sizeof(*out));
    init_header(&out->header, src, dst, req_cmd, msg_counter, ack_request);
    init_payload(&out->payload, fn_name, fn_data);
}

/* ── spine_make_msg_res ───────────────────────────────────────────────────── */

void spine_make_msg_res(spine_msg_t*          out,
                        const spine_header_t* req_header,
                        spine_cmd_t           res_cmd,
                        uint64_t              msg_counter,
                        const char*           fn_name,
                        cJSON*                fn_data) {
    if (!out || !req_header) return;
    memset(out, 0, sizeof(*out));

    snprintf(out->header.spec_version, sizeof(out->header.spec_version),
             "%s", SPINE_SPEC_VERSION);
    out->header.src                     = req_header->dst; /* our address */
    out->header.dst                     = req_header->src; /* peer address */
    out->header.msg_counter             = msg_counter;
    out->header.msg_counter_ref         = req_header->msg_counter;
    out->header.msg_counter_ref_present = true;
    out->header.cmd                     = res_cmd;
    out->header.ack_request             = false;

    init_payload(&out->payload, fn_name, fn_data);
}

/* ── spine_make_reply_header (backward compatibility) ─────────────────────── */

void spine_make_reply_header(const spine_header_t* req,
                              spine_cmd_t           reply_cmd,
                              spine_header_t*       out) {
    if (!req || !out) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->spec_version, sizeof(out->spec_version), "%s", SPINE_SPEC_VERSION);
    out->src                     = req->dst;
    out->dst                     = req->src;
    out->msg_counter_ref         = req->msg_counter;
    out->msg_counter_ref_present = true;
    out->cmd                     = reply_cmd;
    out->ack_request             = false;
}
