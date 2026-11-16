// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_load_ctrl.h
 *
 * Encode / decode for SPINE LoadControl functions:
 *   - loadControlLimitListData       (Resource Spec §5.3.14.5)
 *   - loadControlLimitDescriptionListData (Resource Spec §5.3.14.7)
 *
 * These are the core data types for LPC and LPP use cases.
 */

#ifndef SPINE_CODEC_LOAD_CTRL_H_
#define SPINE_CODEC_LOAD_CTRL_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Max list sizes ───────────────────────────────────────────────────────── */

#define SPINE_LC_LIMIT_MAX        16
#define SPINE_LC_LIMIT_DESC_MAX   16

/* ── loadControlLimitData (Resource Spec §5.3.14.5, Table 239) ────────────── */

typedef struct {
    uint32_t              limit_id;           /* PRIMARY IDENTIFIER         */

    bool                  is_limit_active;
    bool                  is_limit_active_present;

    bool                  is_limit_changeable;
    bool                  is_limit_changeable_present;

    spine_time_period_t   time_period;        /* both fields "" = absent    */

    spine_scaled_number_t value;
    bool                  value_present;
} lc_limit_data_t;

/* ── Convenience aliases used throughout the UC layer ────────────────────── */
/* aliases added after both structs are defined — see below */

/* ── loadControlLimitListData ─────────────────────────────────────────────── */

typedef struct {
    lc_limit_data_t items[SPINE_LC_LIMIT_MAX];
    uint8_t         count;
} lc_limit_list_t;

/* ── loadControlLimitDescriptionData (Resource Spec §5.3.14.7, Table 241) ─── */

typedef struct {
    uint32_t limit_id;                             /* PRIMARY IDENTIFIER   */

    /* limitType: "minValueLimit" | "maxValueLimit" | "signDependentAbsValueLimit" */
    char     limit_type[SPINE_ENUM_STR_MAX];
    bool     limit_type_present;

    /* limitCategory: "obligation" | "recommendation" | "optimization"     */
    char     limit_category[SPINE_ENUM_STR_MAX];
    bool     limit_category_present;

    /* limitDirection: "consume" | "produce"                               */
    char     limit_direction[SPINE_ENUM_STR_MAX];
    bool     limit_direction_present;

    uint32_t measurement_id;
    bool     measurement_id_present;

    char     unit[SPINE_UNIT_STR_MAX];        /* "W", "Wh" …              */
    bool     unit_present;

    char     scope_type[SPINE_SCOPE_STR_MAX]; /* "activePowerLimit" …     */
    bool     scope_type_present;

    char     label[SPINE_LABEL_MAX];
    bool     label_present;

    char     description[SPINE_DESCRIPTION_MAX];
    bool     description_present;
} lc_limit_desc_data_t;

/* ── loadControlLimitDescriptionListData ─────────────────────────────────── */

typedef struct {
    lc_limit_desc_data_t items[SPINE_LC_LIMIT_DESC_MAX];
    uint8_t              count;
} lc_limit_desc_list_t;

/* ── Convenience aliases (used throughout the UC layer) ──────────────────── */
typedef lc_limit_data_t      lc_limit_t;
typedef lc_limit_desc_data_t lc_limit_desc_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

/**
 * @brief Decode loadControlLimitListData from payload array.
 */
spine_err_t lc_limit_list_decode(const cJSON*     payload,
                                 lc_limit_list_t* out);

/**
 * @brief Encode loadControlLimitListData to payload array.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* lc_limit_list_encode(const lc_limit_list_t* in);

/**
 * @brief Decode loadControlLimitDescriptionListData from payload array.
 */
spine_err_t lc_limit_desc_list_decode(const cJSON*          payload,
                                      lc_limit_desc_list_t* out);

/**
 * @brief Encode loadControlLimitDescriptionListData to payload array.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* lc_limit_desc_list_encode(const lc_limit_desc_list_t* in);

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/**
 * @brief Encode a ScaledNumber to a cJSON object {"number":N,"scale":S}.
 * @return New cJSON object (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* spine_scaled_number_encode(const spine_scaled_number_t* v);

/**
 * @brief Decode a ScaledNumber from a cJSON object.
 */
spine_err_t spine_scaled_number_decode(const cJSON*           obj,
                                       spine_scaled_number_t* out);

/**
 * @brief Convert ScaledNumber to double for convenience.
 */
static inline double spine_scaled_number_to_double(
        const spine_scaled_number_t* v) {
    double result = (double)v->number;
    int    s      = v->scale;
    while (s > 0) { result *= 10.0; s--; }
    while (s < 0) { result /= 10.0; s++; }
    return result;
}

/**
 * @brief Fill a ScaledNumber from a watt value (scale=0 for whole watts).
 */
static inline void spine_scaled_number_from_double(
        double val, spine_scaled_number_t* out) {
    out->number = (int64_t)val;
    out->scale  = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_LOAD_CTRL_H_ */
