// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_node_mgmt.h
 *
 * Encode / decode for SPINE NodeManagement functions:
 *   - nodeManagementDetailedDiscoveryData  (Protocol Spec §7.1.2, Table 11)
 *   - nodeManagementUseCaseData            (Protocol Spec §7.5.2, Table 20)
 *
 * These are the two functions used during the Pre-Scenario communication:
 *   1. READ nodeManagementUseCaseData  → REPLY
 *   2. READ nodeManagementDetailedDiscoveryData → REPLY
 *      (parse reply to find peer's feature addresses)
 */

#ifndef SPINE_CODEC_NODE_MGMT_H_
#define SPINE_CODEC_NODE_MGMT_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Size limits ──────────────────────────────────────────────────────────── */

#define NM_ENTITY_MAX_DEPTH       8
#define NM_ENTITY_INFO_MAX       32   /* entities per discovery reply      */
#define NM_FEATURE_INFO_MAX      64   /* features per discovery reply      */
#define NM_SUPPORTED_FN_MAX       8   /* supportedFunctions per feature    */
#define NM_UC_INFO_MAX           16   /* useCaseInformation entries        */
#define NM_UC_SUPPORT_MAX         8   /* useCaseSupport entries per info   */
#define NM_SCENARIO_SUPPORT_MAX   8   /* scenarios per useCaseSupport      */

/* ── Address as used inside discovery data ───────────────────────────────── */
/*
 * Inside nodeManagementDetailedDiscoveryData the device field is often
 * absent (simple mode). We store "" to mean absent.
 */
typedef struct {
    char    device[SPINE_DEVICE_ID_MAX];          /* "" = absent            */
    uint8_t entity[NM_ENTITY_MAX_DEPTH];
    uint8_t entity_depth;
    uint16_t feature;
    bool     feature_present;
} nm_addr_t;

/** Full equality comparison: device + entity path + feature (+ feature_present). */
bool nm_addr_equal(const nm_addr_t* a, const nm_addr_t* b);

/* ── lastStateChange values ──────────────────────────────────────────────── */
/* Protocol Spec §7.1.5: "added" | "removed" | "modified"                   */
typedef enum {
    NM_STATE_CHANGE_NONE     = 0,
    NM_STATE_CHANGE_ADDED    = 1,
    NM_STATE_CHANGE_REMOVED  = 2,
    NM_STATE_CHANGE_MODIFIED = 3,
} nm_state_change_t;

/* ── possibleOperations (per supportedFunction) ──────────────────────────── */

typedef struct {
    bool read_present;
    bool read_partial;
    bool write_present;
    bool write_partial;
} nm_possible_ops_t;

/* ── supportedFunction entry ─────────────────────────────────────────────── */

typedef struct {
    char             function_name[SPINE_FUNCTION_NAME_MAX]; /* fn name */
    nm_possible_ops_t ops;
} nm_supported_fn_t;

/* ── featureInformation.description ─────────────────────────────────────── */

typedef struct {
    nm_addr_t         feature_addr;             /* entity[] + feature       */
    char              feature_type[SPINE_FEATURE_TYPE_MAX];
    char              role[SPINE_ROLE_MAX];      /* "server"/"client"/"special" */
    nm_state_change_t last_state_change;

    char              label[SPINE_LABEL_MAX];
    bool              label_present;
    char              description[SPINE_DESCRIPTION_MAX];
    bool              description_present;

    nm_supported_fn_t supported_fns[NM_SUPPORTED_FN_MAX];
    uint8_t           supported_fn_count;
} nm_feature_info_t;

/* ── entityInformation.description ──────────────────────────────────────── */

typedef struct {
    nm_addr_t         entity_addr;              /* entity[] only (no feature) */
    char              entity_type[SPINE_ENTITY_TYPE_MAX];
    nm_state_change_t last_state_change;
    char              label[SPINE_LABEL_MAX];
    bool              label_present;
    char              description[SPINE_DESCRIPTION_MAX];
    bool              description_present;
} nm_entity_info_t;

/* ── nodeManagementDetailedDiscoveryData ─────────────────────────────────── */

typedef struct {
    /* specificationVersionList (M) */
    char spec_versions[4][SPINE_UC_VERSION_MAX];  /* e.g. "1.3.0" */
    uint8_t spec_version_count;

    /* deviceInformation.description */
    char device_address[SPINE_DEVICE_ID_MAX]; /* M: device address string */
    char device_type[SPINE_ENTITY_TYPE_MAX];  /* M: e.g. "EnergyManagementSystem" */
    char network_feature_set[16];             /* M: "simple"|"smart"|"gateway" (SPINE §7.1.1.5.3) */
    nm_state_change_t device_last_state_change; /* NM_STATE_CHANGE_NONE = omit */
    char device_label[SPINE_LABEL_MAX];
    bool device_label_present;
    char device_description[SPINE_DESCRIPTION_MAX];
    bool device_description_present;

    nm_entity_info_t  entities[NM_ENTITY_INFO_MAX];
    uint8_t           entity_count;

    nm_feature_info_t features[NM_FEATURE_INFO_MAX];
    uint8_t           feature_count;
} nm_discovery_data_t;

/* ── useCaseSupport entry ─────────────────────────────────────────────────── */

typedef struct {
    char    use_case_name[SPINE_UC_NAME_MAX];
    char    use_case_version[SPINE_UC_VERSION_MAX];
    char    use_case_doc_sub_revision[32];     /* "release", "draft", …    */
    bool    use_case_available;                /* default true if absent   */
    uint8_t scenario_support[NM_SCENARIO_SUPPORT_MAX];
    uint8_t scenario_count;
} nm_uc_support_t;

/* ── useCaseInformation entry ────────────────────────────────────────────── */

typedef struct {
    nm_addr_t       address;                   /* entity or device address */
    char            actor[SPINE_ACTOR_MAX];    /* "EnergyGuard" etc.       */
    nm_uc_support_t uc_supports[NM_UC_SUPPORT_MAX];
    uint8_t         uc_support_count;
} nm_uc_info_t;

/* ── nodeManagementUseCaseData ───────────────────────────────────────────── */

typedef struct {
    nm_uc_info_t items[NM_UC_INFO_MAX];
    uint8_t      count;
} nm_uc_data_t;

/* ── Codec: nodeManagementDetailedDiscoveryData ───────────────────────────── */

/**
 * @brief Decode nodeManagementDetailedDiscoveryData from payload array.
 *
 * Used to parse the REPLY to our READ request to the peer's NodeManagement
 * feature, in order to discover the peer's entity and feature addresses.
 */
spine_err_t nm_discovery_data_decode(const cJSON*         payload,
                                     nm_discovery_data_t* out);

/**
 * @brief Encode nodeManagementDetailedDiscoveryData to payload array.
 *
 * Used to respond to a READ from a peer that wants to discover us.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* nm_discovery_data_encode(const nm_discovery_data_t* in);

/* ── Codec: nodeManagementUseCaseData ────────────────────────────────────── */

/**
 * @brief Decode nodeManagementUseCaseData from payload array.
 */
spine_err_t nm_uc_data_decode(const cJSON*  payload,
                              nm_uc_data_t* out);

/**
 * @brief Encode nodeManagementUseCaseData to payload array.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* nm_uc_data_encode(const nm_uc_data_t* in);

/* ── Codec: nodeManagementSubscriptionData / nodeManagementBindingData ──── */

#define NM_SB_ENTRY_MAX  64

typedef struct {
    uint32_t  id;
    nm_addr_t client_addr;
    nm_addr_t server_addr;
} nm_sb_entry_t;

typedef struct {
    nm_sb_entry_t items[NM_SB_ENTRY_MAX];
    uint16_t      count;
} nm_sub_data_t;

typedef struct {
    nm_sb_entry_t items[NM_SB_ENTRY_MAX];
    uint16_t      count;
} nm_bind_data_t;

spine_err_t nm_sub_data_decode(const cJSON* payload, nm_sub_data_t* out);
cJSON*      nm_sub_data_encode(const nm_sub_data_t* in);

spine_err_t nm_bind_data_decode(const cJSON* payload, nm_bind_data_t* out);
cJSON*      nm_bind_data_encode(const nm_bind_data_t* in);

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/**
 * @brief Parse a lastStateChange string into enum.
 */
nm_state_change_t nm_parse_state_change(const char* str);

/**
 * @brief Convert lastStateChange enum to string.
 */
const char* nm_state_change_str(nm_state_change_t sc);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_NODE_MGMT_H_ */
