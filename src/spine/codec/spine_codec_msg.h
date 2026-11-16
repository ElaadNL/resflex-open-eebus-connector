// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_msg.h
 *
 * SPINE message types, header, payload, and construction functions.
 * Protocol Spec v1.3.0, §5.2 (header) and §5.3 (payload).
 */

#ifndef SPINE_CODEC_MSG_H_
#define SPINE_CODEC_MSG_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_address.h"
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── SPINE version ─────────────────────────────────────────────────────────── */

#define SPINE_SPEC_VERSION  SPINE_SPEC_VERSION_STR

/* ── cmdClassifier (§5.2.4, Table 2) ─────────────────────────────────────── */

typedef enum {
    SPINE_CMD_READ    = 0,   /* initial  - request data              */
    SPINE_CMD_REPLY   = 1,   /* response - reply to read             */
    SPINE_CMD_WRITE   = 2,   /* initial  - write data                */
    SPINE_CMD_CALL    = 3,   /* initial  - trigger action            */
    SPINE_CMD_NOTIFY  = 4,   /* response - proactive push of changes */
    SPINE_CMD_RESULT  = 5,   /* ack      - confirmation/error receipt*/
} spine_cmd_t;

/* ── spine_header_t (§5.2.7, Table 3) ────────────────────────────────────── */

typedef struct {
    char         spec_version[32];
    spine_addr_t src;
    spine_addr_t dst;
    uint64_t     msg_counter;
    uint64_t     msg_counter_ref;
    bool         msg_counter_ref_present;
    spine_cmd_t  cmd;
    bool         ack_request;
} spine_header_t;

/* ── RFE filter (§5.3.4) ─────────────────────────────────────────────────
 * Each cmd may have at most one "delete" filter and one "partial" filter.
 * If both present, delete SHALL appear before partial (§5.3.4.2 L1384).
 *
 * selector = SELECTORS element, e.g. {"xxxListDataSelectors": [[...]]}
 * elements = ELEMENTS element, e.g. {"xxxDataElements": [{...}, ...]}
 *
 * These pointers reference nodes within the parsed datagram cJSON tree
 * (decode path) or are owned by the caller (send path).  No duplication
 * is performed during decode; the datagram root must stay alive while
 * the msg is in use.
 * ──────────────────────────────────────────────────────────────────────── */
typedef struct {
    bool     present;      /* true if this filter exists in the cmd      */
    cJSON*   selector;     /* SELECTORS pointer (NULL if absent)         */
    cJSON*   elements;     /* ELEMENTS pointer  (NULL if absent)         */
} spine_rfe_filter_t;

/* ── spine_msg_payload_t (§5.3, Table 4) ────────────────────────────────── */
/*
 * cmd.<FUNCTION> is always present (M), fn_data is its value object.
 * For full READ, fn_data may be NULL; encode generates {"fn_name": []}.
 *
 * Ownership rules:
 *   - Decode path: fn_data and RFE filter pointers reference nodes within
 *     the parsed datagram cJSON tree.  The dispatch layer keeps the tree
 *     alive during handler execution; no explicit cleanup is needed.
 *   - Send path: fn_data (and optional selector/elements) are owned by
 *     the caller.  encode() duplicates them internally; caller does
 *     cJSON_Delete after send returns.
 */
typedef struct spine_msg_payload_s {
    char               fn_name[SPINE_FUNCTION_NAME_MAX];
    cJSON*             fn_data;        /* FUNCTION data                    */
    spine_rfe_filter_t partial;        /* cmdControl "partial" filter      */
    spine_rfe_filter_t delete_;        /* cmdControl "delete"  filter      */
} spine_msg_payload_t;

typedef struct spine_msg_s {
    spine_header_t      header;
    spine_msg_payload_t payload;
} spine_msg_t;

/* ── Message construction functions ───────────────────────────────────────── */

/**
 * @brief Construct an actively initiated message (read / write / call / notify).
 *
 * @param out         Output message
 * @param src         Local feature address
 * @param dst         Peer feature address
 * @param req_cmd     SPINE_CMD_READ / WRITE / CALL / NOTIFY
 * @param msg_counter Obtained from spine_device_next_msg_counter()
 * @param ack_request Whether to require peer ACK
 * @param fn_name     cmd.<FUNCTION> key name
 * @param fn_data     cmd.<FUNCTION> value object; NULL -> generates empty {} (full READ)
 *                    Ownership is not transferred; caller does cJSON_Delete after send returns
 */
void spine_make_msg_req(spine_msg_t*        out,
                        const spine_addr_t* src,
                        const spine_addr_t* dst,
                        spine_cmd_t         req_cmd,
                        uint64_t            msg_counter,
                        bool                ack_request,
                        const char*         fn_name,
                        cJSON*              fn_data);

/**
 * @brief Construct a response message (reply / result).
 *
 * Automatically sets:
 *   src = req_header->dst (our address)
 *   dst = req_header->src (peer address)
 *   msg_counter_ref = req_header->msg_counter
 *
 * @param out         Output message
 * @param req_header  The request header being responded to
 * @param res_cmd     SPINE_CMD_REPLY / SPINE_CMD_RESULT
 * @param msg_counter Obtained from spine_device_next_msg_counter()
 * @param fn_name     cmd.<FUNCTION> key name
 * @param fn_data     cmd.<FUNCTION> value object; ownership is not transferred
 */
void spine_make_msg_res(spine_msg_t*          out,
                        const spine_header_t* req_header,
                        spine_cmd_t           res_cmd,
                        uint64_t              msg_counter,
                        const char*           fn_name,
                        cJSON*                fn_data);

/* ── Internal helpers (still used by datagram codec) ──────────────────────── */

cJSON*      spine_header_encode(const spine_header_t* hdr);
spine_err_t spine_header_decode(const cJSON* obj, spine_header_t* out);
const char* spine_cmd_str(spine_cmd_t cmd);

/* Kept for compatibility (internal to datagram codec, do not call externally) */
void spine_make_reply_header(const spine_header_t* req,
                              spine_cmd_t           reply_cmd,
                              spine_header_t*       out);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_MSG_H_ */
