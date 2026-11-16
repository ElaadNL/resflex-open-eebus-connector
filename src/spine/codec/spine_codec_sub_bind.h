// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_sub_bind.h
 *
 * Encode / decode for SPINE NodeManagement subscription and binding
 * request CALL payloads.
 * Protocol Spec v1.3.0, §7.3 (Binding) and §7.4 (Subscription).
 *
 * Wire format (xs:sequence → Array):
 *   fn_data = {"nodeManagementSubscriptionRequestCall": [
 *     {"subscriptionRequest": [
 *       {"clientAddress": [{"device":"..."}, {"entity":[0]}, {"feature":0}]},
 *       {"serverAddress": [{"device":"..."}, {"entity":[0]}, {"feature":0}]},
 *       {"serverFeatureType": "LoadControl"}
 *     ]}
 *   ]}
 */

#ifndef SPINE_CODEC_SUB_BIND_H_
#define SPINE_CODEC_SUB_BIND_H_

#include "spine_codec_common.h"
#include "spine_codec_node_mgmt.h"   /* nm_addr_t */
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Decoded form of a sub/bind request ─────────────────────────────────── */

typedef enum {
    SPINE_SB_REQUEST_SUBSCRIBE = 0,
    SPINE_SB_REQUEST_BIND      = 1,
} spine_sb_request_type_t;

typedef struct {
    spine_sb_request_type_t type;
    nm_addr_t client_addr;
    nm_addr_t server_addr;
    char      server_feature_type[64];
    bool      server_feature_type_present;
} spine_sb_request_t;

/* ── Decode ──────────────────────────────────────────────────────────────── */

/**
 * @brief Decode a subscription or binding request from fn_data.
 *
 * fn_data = {"nodeManagementSubscriptionRequestCall":[{"subscriptionRequest":[...]}]}
 * The function automatically identifies subscribe/bind internally.
 *
 * @param fn_data  msg->payload.fn_data
 * @param out      Decoded result
 * @return SPINE_OK or SPINE_ERR_PARSE
 */
spine_err_t spine_sb_request_decode(const cJSON*        fn_data,
                                    spine_sb_request_t* out);

/* ── Encode ──────────────────────────────────────────────────────────────── */

/**
 * @brief Encode a nodeManagementSubscriptionRequestCall fn_data.
 *
 * @param client_addr         Our client feature address.
 * @param server_addr         Peer server feature address.
 * @param server_feature_type Feature type string (e.g. "LoadControl"), or NULL.
 * @return New cJSON object (caller cJSON_Delete), or NULL on OOM.
 */
cJSON* spine_sb_encode_subscribe(const nm_addr_t* client_addr,
                                  const nm_addr_t* server_addr,
                                  const char*      server_feature_type);

/**
 * @brief Encode a nodeManagementBindingRequestCall fn_data.
 */
cJSON* spine_sb_encode_bind(const nm_addr_t* client_addr,
                             const nm_addr_t* server_addr,
                             const char*      server_feature_type);

/* ── Delete ─────────────────────────────────────────────────────────────── */

/** Decoded form of a subscription/binding delete CALL (§7.4.3, §7.3.3). */
typedef struct {
    spine_sb_request_type_t type;
    nm_addr_t client_addr;
    nm_addr_t server_addr;
} spine_sb_delete_t;

spine_err_t spine_sb_delete_decode(const cJSON* fn_data, spine_sb_delete_t* out);

cJSON* spine_sb_encode_delete_subscribe(const nm_addr_t* client_addr,
                                         const nm_addr_t* server_addr);

cJSON* spine_sb_encode_delete_bind(const nm_addr_t* client_addr,
                                    const nm_addr_t* server_addr);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_SUB_BIND_H_ */
