// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_measurement.h
 *
 * Encode / decode for SPINE Measurement functions:
 *   - measurementListData             (Resource Spec §5.3.15.2, Table 244)
 *   - measurementDescriptionListData  (Resource Spec §5.3.15.5, Table 251)
 *
 * Used by the MPC Use Case.
 */

#ifndef SPINE_CODEC_MEASUREMENT_H_
#define SPINE_CODEC_MEASUREMENT_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "spine_codec_load_ctrl.h"   /* spine_scaled_number_t */
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Max list size ────────────────────────────────────────────────────────── */

#define SPINE_MEAS_MAX  16

/* ── measurementData (Resource Spec §5.3.15.2, Table 244) ───────────────── */

typedef struct {
    uint32_t              measurement_id;

    /* valueType: "value" | "averageValue" | "minValue" | "maxValue" */
    char                  value_type[SPINE_ENUM_STR_MAX];
    bool                  value_type_present;

    char                  timestamp[SPINE_TIME_STR_MAX];
    bool                  timestamp_present;

    spine_scaled_number_t value;
    bool                  value_present;

    /* evaluationPeriod (TimePeriodType) */
    spine_time_period_t   evaluation_period;

    /* valueSource: "measuredValue"|"calculatedValue"|"empiricalValue" */
    char                  value_source[SPINE_ENUM_STR_MAX];
    bool                  value_source_present;

    /* valueTendency: "rising"|"stable"|"falling" */
    char                  value_tendency[SPINE_ENUM_STR_MAX];
    bool                  value_tendency_present;

    /* valueState: "normal"|"outOfRange"|"error" (MPC spec [MPC-003]) */
    char                  value_state[SPINE_ENUM_STR_MAX];
    bool                  value_state_present;
} meas_data_t;

typedef struct {
    meas_data_t items[SPINE_MEAS_MAX];
    uint8_t     count;
} meas_list_t;

/* ── measurementDescriptionData (Resource Spec §5.3.15.5, Table 251) ────── */

typedef struct {
    uint32_t measurement_id;

    /* measurementType: "power" | "energy" | "current" | "voltage" | … */
    char     measurement_type[SPINE_ENUM_STR_MAX];
    bool     measurement_type_present;

    /* commodityType: "electricity"|"gas"|"oil"|"water"|… */
    char     commodity_type[SPINE_ENUM_STR_MAX];
    bool     commodity_type_present;

    /* unit: "W" | "Wh" | "A" | "V" | "Hz" */
    char     unit[SPINE_UNIT_STR_MAX];
    bool     unit_present;

    /* scopeType: "acPowerTotal" | "dcPower" | … */
    char     scope_type[SPINE_SCOPE_STR_MAX];
    bool     scope_type_present;
} meas_desc_data_t;

typedef struct {
    meas_desc_data_t items[SPINE_MEAS_MAX];
    uint8_t          count;
} meas_desc_list_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

spine_err_t meas_list_decode(const cJSON* payload, meas_list_t* out);
cJSON*      meas_list_encode(const meas_list_t* in);

spine_err_t meas_desc_list_decode(const cJSON* payload, meas_desc_list_t* out);
cJSON*      meas_desc_list_encode(const meas_desc_list_t* in);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_MEASUREMENT_H_ */
