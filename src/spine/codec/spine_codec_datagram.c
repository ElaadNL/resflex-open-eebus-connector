// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_datagram.c
 *
 * SPINE datagram JSON encode/decode.
 * Protocol Spec v1.3.0 §5.2 + §5.3. SHIP §11.4 xs:sequence -> Array rules.
 *
 * Full exchange wire format:
 * {"datagram":[
 *   {"header":[...]},
 *   {"payload":[
 *     {"cmd":[
 *       [{"loadControlLimitListData":[...]}]
 *     ]}
 *   ]}
 * ]}
 *
 * RFE wire format (§5.3.4):
 * {"datagram":[
 *   {"header":[...]},
 *   {"payload":[
 *     {"cmd":[
 *       [
 *         {"function":"loadControlLimitListData"},
 *         {"filter":[
 *           [{"cmdControl":[{"partial":{}}]},
 *            {"loadControlLimitListDataSelectors":[[{"limitId":1}]]}]
 *         ]},
 *         {"loadControlLimitListData":[
 *           [{"limitId":1},{"isLimitActive":true},{"value":[{"number":3000},{"scale":0}]}]
 *         ]}
 *       ]
 *     ]}
 *   ]}
 * ]}
 */

#include "spine_codec_datagram.h"
#include "spine_codec_msg.h"

#include <string.h>
#include <stdio.h>

/* ── cmd_item parsing rules (§5.3 + §5.3.4) ─────────────────────────────
 *
 * cmd_item is a CmdType xs:sequence → JSON Array.
 *
 * Full exchange mode (first element key != "function"):
 *   cmd_item = [{"loadControlLimitListData": [...]}]
 *   fn_name  = first element key
 *   fn_data  = first element
 *
 * RFE mode (first element key == "function"):
 *   cmd_item = [
 *     {"function": "loadControlLimitListData"},
 *     {"filter": [[filter1], [filter2]]},
 *     {"loadControlLimitListData": [...]}
 *   ]
 *   fn_name  = "function" value string
 *   fn_data  = element where key == fn_name
 *   filters  = extracted from "filter" element
 *
 * All output pointers reference nodes within cmd_item (no duplication).
 * ──────────────────────────────────────────────────────────────────────── */

/* Extract one filter's cmdControl / selector / elements into the
 * appropriate slot of payload (partial or delete). */
static void decode_one_filter(const cJSON* filter_arr,
                                spine_msg_payload_t* out) {
    bool is_partial = false;
    bool is_delete  = false;
    cJSON* selector = NULL;
    cJSON* elements = NULL;

    const cJSON* fe;
    cJSON_ArrayForEach(fe, filter_arr) {
        if (!cJSON_IsObject(fe) || !fe->child) continue;
        const char* key = fe->child->string;
        if (!key) continue;

        if (strcmp(key, "cmdControl") == 0) {
            /* cmdControl: [{"partial":[]}, {"delete":[]}] */
            const cJSON* ctrl;
            cJSON_ArrayForEach(ctrl, fe->child) {
                if (!cJSON_IsObject(ctrl) || !ctrl->child) continue;
                const char* ck = ctrl->child->string;
                if (ck && strcmp(ck, "partial") == 0) is_partial = true;
                if (ck && strcmp(ck, "delete")  == 0) is_delete  = true;
            }
        } else if (strstr(key, "Selectors")) {
            selector = (cJSON*)fe;
        } else if (strstr(key, "Elements")) {
            elements = (cJSON*)fe;
        }
    }

    if (is_partial) {
        out->partial.present  = true;
        out->partial.selector = selector;
        out->partial.elements = elements;
    } else if (is_delete) {
        out->delete_.present  = true;
        out->delete_.selector = selector;
        out->delete_.elements = elements;
    }
}

static void find_fn(const cJSON* cmd_item,
                    spine_msg_payload_t* out) {
    out->fn_data = NULL;
    out->fn_name[0] = '\0';
    out->partial = (spine_rfe_filter_t){0};
    out->delete_ = (spine_rfe_filter_t){0};

    if (!cJSON_IsArray(cmd_item) || cJSON_GetArraySize(cmd_item) < 1)
        return;

    const cJSON* elem0 = cJSON_GetArrayItem(cmd_item, 0);
    if (!elem0 || !cJSON_IsObject(elem0) || !elem0->child) return;

    const char* key0 = elem0->child->string;
    if (!key0) return;

    if (strcmp(key0, "function") != 0) {
        /* Full exchange mode */
        snprintf(out->fn_name, sizeof(out->fn_name), "%s", key0);
        out->fn_data = (cJSON*)elem0;
        return;
    }

    /* RFE mode */
    if (!cJSON_IsString(elem0->child)) return;
    snprintf(out->fn_name, sizeof(out->fn_name),
             "%s", elem0->child->valuestring);

    const cJSON* e;
    cJSON_ArrayForEach(e, cmd_item) {
        if (!cJSON_IsObject(e) || !e->child) continue;
        const char* key = e->child->string;
        if (!key) continue;

        if (strcmp(key, "filter") == 0) {
            /* filter value: [[filter1], [filter2]] */
            const cJSON* filter_outer = e->child;
            const cJSON* fi;
            cJSON_ArrayForEach(fi, filter_outer) {
                decode_one_filter(fi, out);
            }
        } else if (strcmp(key, out->fn_name) == 0) {
            out->fn_data = (cJSON*)e;
        }
    }
}

/* ── Decode ───────────────────────────────────────────────────────────────── */

spine_err_t spine_datagram_decode(const cJSON* root, spine_msg_t* out) {
    if (!root || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    /* root = {"datagram": [...]} */
    const cJSON* dg_arr = cJSON_GetObjectItemCaseSensitive(root, "datagram");
    if (!cJSON_IsArray(dg_arr) || cJSON_GetArraySize(dg_arr) < 1)
        return SPINE_ERR_PARSE;

    /* Iterate through datagram elements - find header and payload */
    const cJSON* header_arr  = NULL;
    const cJSON* payload_arr = NULL;

    const cJSON* dg_elem;
    cJSON_ArrayForEach(dg_elem, dg_arr) {
        if (!cJSON_IsObject(dg_elem) || !dg_elem->child) continue;
        const char* key = dg_elem->child->string;
        if (!key) continue;
        if (strcmp(key, "header")  == 0) header_arr  = dg_elem->child;
        if (strcmp(key, "payload") == 0) payload_arr = dg_elem->child;
    }

    if (!header_arr) return SPINE_ERR_PARSE;
    if (spine_header_decode(header_arr, &out->header) != SPINE_OK)
        return SPINE_ERR_PARSE;

    /* When payload is absent (e.g. result), fn_name remains empty */
    if (!payload_arr) return SPINE_OK;

    /* payload[0] = {"cmd": [[cmd_item_array]]} */
    const cJSON* cmd_obj = NULL;
    const cJSON* pl_elem;
    cJSON_ArrayForEach(pl_elem, payload_arr) {
        if (!cJSON_IsObject(pl_elem) || !pl_elem->child) continue;
        if (strcmp(pl_elem->child->string, "cmd") == 0) {
            cmd_obj = pl_elem->child;
            break;
        }
    }
    if (!cmd_obj) return SPINE_OK;

    /* cmd value is an Array (maxOccurs=unbounded); use the first element */
    const cJSON* cmd_val = cmd_obj;  /* = the value of {"cmd": value} */
    if (!cJSON_IsArray(cmd_val) || cJSON_GetArraySize(cmd_val) < 1)
        return SPINE_OK;

    /* cmd[0] = cmd_item Array */
    const cJSON* cmd_item = cJSON_GetArrayItem(cmd_val, 0);

    find_fn(cmd_item, &out->payload);

    return SPINE_OK;
}

/* ── Encode helpers ────────────────────────────────────────────────────── */

/* Build one FilterType xs:sequence array for a single RFE filter.
 * Produces: [{"cmdControl":[{"partial":[]}]}, {"xxxSelectors":...}, ...]
 * ElementTagType = empty xs:sequence → [] (SHIP §11.4.6 Rule 4).
 * selector/elements are duplicated because AddItem transfers ownership. */
static cJSON* encode_one_filter(const spine_rfe_filter_t* f, bool is_delete) {
    cJSON* arr = cJSON_CreateArray();      /* FilterType xs:sequence */
    if (!arr) return NULL;

    /* cmdControl: CmdControlType xs:sequence → Array */
    cJSON* ctrl_arr = cJSON_CreateArray();
    if (!ctrl_arr) { cJSON_Delete(arr); return NULL; }

    cJSON* tag_obj = cJSON_CreateObject();
    if (!tag_obj) { cJSON_Delete(ctrl_arr); cJSON_Delete(arr); return NULL; }

    /* ElementTagType = empty xs:sequence → empty Array [] */
    cJSON* empty_tag = cJSON_CreateArray();
    if (!empty_tag) { cJSON_Delete(tag_obj); cJSON_Delete(ctrl_arr); cJSON_Delete(arr); return NULL; }
    cJSON_AddItemToObject(tag_obj, is_delete ? "delete" : "partial", empty_tag);
    cJSON_AddItemToArray(ctrl_arr, tag_obj);

    cJSON* ctrl_wrap = cJSON_CreateObject();
    if (!ctrl_wrap) { cJSON_Delete(ctrl_arr); cJSON_Delete(arr); return NULL; }
    cJSON_AddItemToObject(ctrl_wrap, "cmdControl", ctrl_arr);
    cJSON_AddItemToArray(arr, ctrl_wrap);

    /* SELECTORS (optional, duplicated) */
    if (f->selector) {
        cJSON* dup = cJSON_Duplicate(f->selector, 1);
        if (!dup) { cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToArray(arr, dup);
    }

    /* ELEMENTS (optional, duplicated) */
    if (f->elements) {
        cJSON* dup = cJSON_Duplicate(f->elements, 1);
        if (!dup) { cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToArray(arr, dup);
    }

    return arr;
}

/* ── Encode ───────────────────────────────────────────────────────────────── */
/*
 * Encode duplicates fn_data (and optional selector/elements) via cJSON_Duplicate
 * and incorporates them into the datagram tree.  The caller retains ownership
 * of all original pointers and may free them after encode returns.
 *
 * When fn_data is NULL -> generates {"fn_name": []} (full READ / delete dc).
 *
 * RFE mode is used when partial or delete_ filter is present.  In that case
 * the cmd_item contains: {"function":...} + {"filter":...} + {fn_data}.
 */
cJSON* spine_datagram_encode(const spine_msg_t* msg) {
    if (!msg || msg->payload.fn_name[0] == '\0') return NULL;

    const spine_msg_payload_t* p = &msg->payload;
    bool is_rfe = p->partial.present || p->delete_.present;

    /* root = {"datagram": [...]} */
    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON* dg_outer = cJSON_CreateArray();
    if (!dg_outer) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "datagram", dg_outer);

    /* {"header": header_arr} */
    cJSON* hdr_arr = spine_header_encode(&msg->header);
    if (!hdr_arr) { cJSON_Delete(root); return NULL; }
    cJSON* hdr_obj = cJSON_CreateObject();
    if (!hdr_obj) { cJSON_Delete(hdr_arr); cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(hdr_obj, "header", hdr_arr);
    cJSON_AddItemToArray(dg_outer, hdr_obj);

    /* {"payload": payload_arr} */
    cJSON* payload_arr = cJSON_CreateArray();
    if (!payload_arr) { cJSON_Delete(root); return NULL; }
    cJSON* payload_obj = cJSON_CreateObject();
    if (!payload_obj) { cJSON_Delete(payload_arr); cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(payload_obj, "payload", payload_arr);
    cJSON_AddItemToArray(dg_outer, payload_obj);

    /* {"cmd": [[cmd_item]]} */
    cJSON* cmd_outer_arr = cJSON_CreateArray();
    if (!cmd_outer_arr) { cJSON_Delete(root); return NULL; }
    cJSON* cmd_wrapper = cJSON_CreateObject();
    if (!cmd_wrapper) { cJSON_Delete(cmd_outer_arr); cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(cmd_wrapper, "cmd", cmd_outer_arr);
    cJSON_AddItemToArray(payload_arr, cmd_wrapper);

    cJSON* cmd_item_arr = cJSON_CreateArray();
    if (!cmd_item_arr) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToArray(cmd_outer_arr, cmd_item_arr);

    /* ── cmd_item content ──────────────────────────────────────────────── */

    if (is_rfe) {
        /* 1. {"function": "loadControlLimitListData"} */
        cJSON* fn_obj = cJSON_CreateObject();
        if (!fn_obj) { cJSON_Delete(root); return NULL; }
        cJSON_AddStringToObject(fn_obj, "function", p->fn_name);
        cJSON_AddItemToArray(cmd_item_arr, fn_obj);

        /* 2. {"filter": [[delete_filter], [partial_filter]]}
         *    delete filter SHALL appear before partial (§5.3.4.2 L1384). */
        cJSON* filter_outer = cJSON_CreateArray();
        if (!filter_outer) { cJSON_Delete(root); return NULL; }

        if (p->delete_.present) {
            cJSON* f = encode_one_filter(&p->delete_, true);
            if (!f) { cJSON_Delete(filter_outer); cJSON_Delete(root); return NULL; }
            cJSON_AddItemToArray(filter_outer, f);
        }
        if (p->partial.present) {
            cJSON* f = encode_one_filter(&p->partial, false);
            if (!f) { cJSON_Delete(filter_outer); cJSON_Delete(root); return NULL; }
            cJSON_AddItemToArray(filter_outer, f);
        }

        cJSON* filter_wrap = cJSON_CreateObject();
        if (!filter_wrap) { cJSON_Delete(filter_outer); cJSON_Delete(root); return NULL; }
        cJSON_AddItemToObject(filter_wrap, "filter", filter_outer);
        cJSON_AddItemToArray(cmd_item_arr, filter_wrap);
    }

    /* 3. fn_data (duplicated; caller retains original) */
    cJSON* fn_dup = p->fn_data ? cJSON_Duplicate(p->fn_data, 1) : NULL;
    if (!fn_dup) {
        /* NULL fn_data → empty FUNCTION: {"fn_name": []} */
        fn_dup = cJSON_CreateObject();
        if (!fn_dup) { cJSON_Delete(root); return NULL; }
        cJSON* empty_arr = cJSON_CreateArray();
        if (!empty_arr) { cJSON_Delete(fn_dup); cJSON_Delete(root); return NULL; }
        cJSON_AddItemToObject(fn_dup, p->fn_name, empty_arr);
    }
    cJSON_AddItemToArray(cmd_item_arr, fn_dup);

    return root;
}
