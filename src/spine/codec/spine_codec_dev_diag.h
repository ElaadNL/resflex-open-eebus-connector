// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_dev_diag.h
 *
 * Encode / decode for SPINE DeviceDiagnosis functions:
 *   - deviceDiagnosisHeartbeatData  (Resource Spec §5.3.9.3)
 *   - deviceDiagnosisStateData      (Resource Spec §5.3.9.2)
 *
 * Used by LPC/LPP Scenario 3 (heartbeat) and operating state reporting.
 */

#ifndef SPINE_CODEC_DEV_DIAG_H_
#define SPINE_CODEC_DEV_DIAG_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── deviceDiagnosisHeartbeatData (Resource Spec §5.3.9.3, Table 202) ─────── */

typedef struct {
    /*
     * timestamp — ISO 8601 string, "" = absent
     * Per spec: "The time of creation of the data."
     */
    char     timestamp[SPINE_TIME_STR_MAX];
    bool     timestamp_present;

    /*
     * heartbeatCounter — incrementing counter
     * "SHALL be increased after every heartbeatTimeout"
     */
    uint64_t heartbeat_counter;
    bool     heartbeat_counter_present;

    /*
     * heartbeatTimeout — xs:duration string, e.g. "PT60S"
     * "Contains the period in which the function is sent (NOT the remaining
     *  time till the next sending)."
     */
    char     heartbeat_timeout[SPINE_TIME_STR_MAX];
    bool     heartbeat_timeout_present;
} dev_diag_heartbeat_t;

/* ── deviceDiagnosisStateData (Resource Spec §5.3.9.2, Table 200) ──────── */

typedef struct {
    char     timestamp[SPINE_TIME_STR_MAX];
    bool     timestamp_present;

    /* operatingState: "normalOperation"|"standby"|"failure"|"serviceNeeded"|
       "overrideDetected"|"inAlarm"|"notReachable"|"finished"|"temporarilyNotReady" */
    char     operating_state[SPINE_ENUM_STR_MAX];
    bool     operating_state_present;

    char     vendor_state_code[SPINE_DESCRIPTION_MAX];
    bool     vendor_state_code_present;

    char     last_error_code[SPINE_DESCRIPTION_MAX];
    bool     last_error_code_present;

    char     up_time[SPINE_TIME_STR_MAX];           /* xs:duration e.g. "PT3600S" */
    bool     up_time_present;

    char     total_up_time[SPINE_TIME_STR_MAX];     /* xs:duration */
    bool     total_up_time_present;

    /* powerSupplyCondition: "good"|"low"|"critical"|"unknown"|"error" */
    char     power_supply_condition[SPINE_ENUM_STR_MAX];
    bool     power_supply_condition_present;
} dev_diag_state_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

/**
 * @brief Decode deviceDiagnosisHeartbeatData from payload array.
 */
spine_err_t dev_diag_heartbeat_decode(const cJSON*          payload,
                                      dev_diag_heartbeat_t* out);

/**
 * @brief Encode deviceDiagnosisHeartbeatData to payload array.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* dev_diag_heartbeat_encode(const dev_diag_heartbeat_t* in);

spine_err_t dev_diag_state_decode(const cJSON* payload, dev_diag_state_t* out);
cJSON*      dev_diag_state_encode(const dev_diag_state_t* in);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_DEV_DIAG_H_ */
