// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_elec_conn.h
 *
 * Encode / decode for SPINE ElectricalConnection functions:
 *   - electricalConnectionParameterDescriptionListData  (Resource Spec §5.3.11.2)
 *   - electricalConnectionPermittedValueSetListData     (Resource Spec §5.3.11.3)
 *
 * Used by LPC Scenario 4 (constraints) and MPC.
 */

#ifndef SPINE_CODEC_ELEC_CONN_H_
#define SPINE_CODEC_ELEC_CONN_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Max list sizes ───────────────────────────────────────────────────────── */

#define SPINE_EC_PARAM_DESC_MAX       16
#define SPINE_EC_PERMITTED_SET_MAX    16
#define SPINE_EC_PERMITTED_VALUE_MAX   8  /* values per permittedValueSet */
#define SPINE_EC_PERMITTED_RANGE_MAX   4  /* ranges per permittedValueSet */

/* ── electricalConnectionParameterDescriptionData (Resource Spec §5.3.11.2) ─ */

typedef struct {
    uint32_t electrical_connection_id;    /* PRIMARY IDENTIFIER         */
    uint32_t parameter_id;                /* SUB IDENTIFIER             */

    uint32_t measurement_id;
    bool     measurement_id_present;

    /* voltageType: "ac" | "dc" */
    char     voltage_type[SPINE_ENUM_STR_MAX];
    bool     voltage_type_present;

    /* acMeasuredPhases: "a"|"b"|"c"|"ab"|"bc"|"ac"|"abc"|"neutral"|"none" */
    char     ac_measured_phases[SPINE_ENUM_STR_MAX];
    bool     ac_measured_phases_present;

    /* acMeasuredInReferenceTo: same enum as acMeasuredPhases */
    char     ac_measured_in_ref_to[SPINE_ENUM_STR_MAX];
    bool     ac_measured_in_ref_to_present;

    /* acMeasurementType: "real" | "reactive" | "apparent" | "phase" */
    char     ac_measurement_type[SPINE_ENUM_STR_MAX];
    bool     ac_measurement_type_present;

    /* acMeasurementVariant: "amplitude"|"rms"|"instantaneous"|"angle"|"cosPhi" */
    char     ac_measurement_variant[SPINE_ENUM_STR_MAX];
    bool     ac_measurement_variant_present;

    uint8_t  ac_measured_harmonic;   /* 1 = normal value */
    bool     ac_measured_harmonic_present;

    char     scope_type[SPINE_SCOPE_STR_MAX];
    bool     scope_type_present;

    char     label[SPINE_LABEL_MAX];
    bool     label_present;

    char     description[SPINE_DESCRIPTION_MAX];
    bool     description_present;
} ec_param_desc_data_t;

typedef struct {
    ec_param_desc_data_t items[SPINE_EC_PARAM_DESC_MAX];
    uint8_t              count;
} ec_param_desc_list_t;

/* ── ScaledNumberRange — one min..max range ──────────────────────────────── */

typedef struct {
    spine_scaled_number_t min;
    bool                  min_present;
    spine_scaled_number_t max;
    bool                  max_present;
} ec_scaled_number_range_t;

/* ── electricalConnectionPermittedValueSetData (Resource Spec §5.3.11.3) ─── */

typedef struct {
    uint32_t electrical_connection_id;
    uint32_t parameter_id;

    /* permittedValueSet — list of values and/or ranges */
    spine_scaled_number_t  values[SPINE_EC_PERMITTED_VALUE_MAX];
    uint8_t                value_count;

    ec_scaled_number_range_t ranges[SPINE_EC_PERMITTED_RANGE_MAX];
    uint8_t                  range_count;
} ec_permitted_set_data_t;

typedef struct {
    ec_permitted_set_data_t items[SPINE_EC_PERMITTED_SET_MAX];
    uint8_t                 count;
} ec_permitted_set_list_t;

/* ── electricalConnectionCharacteristicData (Resource Spec §5.3.11.6) ────── */
/* Used by LPC/LPP Scenario 4 (constraints: nominal max power)               */

#define SPINE_EC_CHARACTERISTIC_MAX  16

typedef struct {
    uint32_t              electrical_connection_id;
    uint32_t              parameter_id;
    uint32_t              characteristic_id;

    /* characteristicContext: "device"|"entity"|"electricalConnection" */
    char                  characteristic_context[SPINE_ENUM_STR_MAX];
    bool                  characteristic_context_present;

    /* characteristicType: "powerConsumptionNominalMax"|"contractualConsumptionNominalMax"|
       "powerProductionNominalMax"|"contractualProductionNominalMax"|... */
    char                  characteristic_type[SPINE_ENUM_STR_MAX];
    bool                  characteristic_type_present;

    spine_scaled_number_t value;
    bool                  value_present;

    char                  unit[SPINE_UNIT_STR_MAX];
    bool                  unit_present;
} ec_characteristic_data_t;

typedef struct {
    ec_characteristic_data_t items[SPINE_EC_CHARACTERISTIC_MAX];
    uint8_t                  count;
} ec_characteristic_list_t;

/* ── electricalConnectionDescriptionData (Resource Spec §5.3.11.1) ────────── */

#define SPINE_EC_DESC_MAX  8

typedef struct {
    uint32_t electrical_connection_id;

    char     power_supply_type[SPINE_ENUM_STR_MAX];   /* "ac"|"dc" */
    bool     power_supply_type_present;

    uint32_t ac_connected_phases;
    bool     ac_connected_phases_present;

    char     ac_rms_period_duration[SPINE_TIME_STR_MAX]; /* xs:duration */
    bool     ac_rms_period_duration_present;

    char     positive_energy_direction[SPINE_ENUM_STR_MAX]; /* "consume"|"produce" */
    bool     positive_energy_direction_present;

    char     scope_type[SPINE_SCOPE_STR_MAX];
    bool     scope_type_present;

    char     label[SPINE_LABEL_MAX];
    bool     label_present;

    char     description[SPINE_DESCRIPTION_MAX];
    bool     description_present;
} ec_description_data_t;

typedef struct {
    ec_description_data_t items[SPINE_EC_DESC_MAX];
    uint8_t               count;
} ec_description_list_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

/**
 * @brief Decode electricalConnectionParameterDescriptionListData.
 */
spine_err_t ec_param_desc_list_decode(const cJSON*          payload,
                                      ec_param_desc_list_t* out);

/**
 * @brief Encode electricalConnectionParameterDescriptionListData.
 */
cJSON* ec_param_desc_list_encode(const ec_param_desc_list_t* in);

/**
 * @brief Decode electricalConnectionPermittedValueSetListData.
 */
spine_err_t ec_permitted_set_list_decode(const cJSON*             payload,
                                         ec_permitted_set_list_t* out);

/**
 * @brief Encode electricalConnectionPermittedValueSetListData.
 */
cJSON* ec_permitted_set_list_encode(const ec_permitted_set_list_t* in);

/**
 * @brief Decode electricalConnectionCharacteristicListData.
 */
spine_err_t ec_characteristic_list_decode(const cJSON*              payload,
                                           ec_characteristic_list_t* out);

/**
 * @brief Encode electricalConnectionCharacteristicListData.
 */
cJSON* ec_characteristic_list_encode(const ec_characteristic_list_t* in);

spine_err_t ec_description_list_decode(const cJSON* payload, ec_description_list_t* out);
cJSON*      ec_description_list_encode(const ec_description_list_t* in);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_ELEC_CONN_H_ */
