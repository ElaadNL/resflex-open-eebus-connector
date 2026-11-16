// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_dev_cfg.h
 *
 * Encode / decode for SPINE DeviceConfiguration functions:
 *   - deviceConfigurationKeyValueListData  (Resource Spec §5.3.8.2)
 *
 * Used by LPC Scenario 2 (failsafe values) and LPP.
 */

#ifndef SPINE_CODEC_DEV_CFG_H_
#define SPINE_CODEC_DEV_CFG_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Max list size ────────────────────────────────────────────────────────── */

#define SPINE_DEV_CFG_KV_MAX  16

/* ── Value type discriminator (Resource Spec §5.3.8.2, Table 194) ─────────── */

typedef enum {
    DEV_CFG_VALUE_NONE        = 0,
    DEV_CFG_VALUE_BOOLEAN     = 1,
    DEV_CFG_VALUE_DURATION    = 2,   /* xs:duration string, e.g. "PT7200S" */
    DEV_CFG_VALUE_STRING      = 3,
    DEV_CFG_VALUE_SCALED_NUM  = 4,
    DEV_CFG_VALUE_INTEGER     = 5,
    DEV_CFG_VALUE_DATE        = 6,   /* xs:date string, e.g. "2025-01-15" */
    DEV_CFG_VALUE_DATE_TIME   = 7,   /* xs:dateTime string, e.g. "2025-01-15T12:00:00Z" */
    DEV_CFG_VALUE_TIME        = 8,   /* xs:time string, e.g. "12:00:00" */
} dev_cfg_value_type_t;

/* ── deviceConfigurationKeyValueData (Resource Spec §5.3.8.2, Table 193) ──── */

typedef struct {
    uint32_t             key_id;         /* PRIMARY IDENTIFIER              */

    dev_cfg_value_type_t value_type;     /* discriminator                   */
    union {
        bool                  boolean_val;
        char                  string_val[SPINE_DESCRIPTION_MAX];
        char                  duration_val[SPINE_TIME_STR_MAX]; /* "PT7200S" */
        spine_scaled_number_t scaled_num_val;
        int64_t               integer_val;
    } value;
    bool                 value_present;

    bool                 is_value_changeable;
    bool                 is_value_changeable_present;
} dev_cfg_kv_data_t;

/* ── deviceConfigurationKeyValueListData ─────────────────────────────────── */

typedef struct {
    dev_cfg_kv_data_t items[SPINE_DEV_CFG_KV_MAX];
    uint8_t           count;
} dev_cfg_kv_list_t;

/* ── Well-known key names (Resource Spec §5.3.8.3, Table 196) ───────────── */
/*
 * Applications should compare the keyName from the description list against
 * these constants to identify what each keyId means.
 */
#define DEV_CFG_KEY_FAILSAFE_CONSUMPTION_ACTIVE_POWER_LIMIT \
        "failsafeConsumptionActivePowerLimit"
#define DEV_CFG_KEY_FAILSAFE_PRODUCTION_ACTIVE_POWER_LIMIT \
        "failsafeProductionActivePowerLimit"
#define DEV_CFG_KEY_FAILSAFE_DURATION_MINIMUM \
        "failsafeDurationMinimum"

/* ── deviceConfigurationKeyValueDescriptionData (Resource Spec §5.3.8.3) ──── */

typedef struct {
    uint32_t key_id;
    char     key_name[SPINE_ENUM_STR_MAX];     /* "failsafeConsumptionActivePowerLimit" etc. */
    bool     key_name_present;
    char     value_type[32];                    /* "scaledNumber","duration","boolean","string","integer" */
    bool     value_type_present;
    char     unit[SPINE_UNIT_STR_MAX];
    bool     unit_present;
    char     label[SPINE_LABEL_MAX];
    bool     label_present;
    char     description[SPINE_DESCRIPTION_MAX];
    bool     description_present;
} dev_cfg_kv_desc_data_t;

typedef struct {
    dev_cfg_kv_desc_data_t items[SPINE_DEV_CFG_KV_MAX];
    uint8_t                count;
} dev_cfg_kv_desc_list_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

spine_err_t dev_cfg_kv_list_decode(const cJSON*       payload,
                                   dev_cfg_kv_list_t* out);
cJSON* dev_cfg_kv_list_encode(const dev_cfg_kv_list_t* in);

spine_err_t dev_cfg_kv_desc_list_decode(const cJSON*            payload,
                                         dev_cfg_kv_desc_list_t* out);
cJSON* dev_cfg_kv_desc_list_encode(const dev_cfg_kv_desc_list_t* in);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_DEV_CFG_H_ */
