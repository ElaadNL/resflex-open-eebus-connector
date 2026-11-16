// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_common.h
 *
 * Common data types shared across all SPINE codec modules.
 * Strictly based on SPINE Resource Specification v1.3.0, §3.10.
 *
 * Rules for this file:
 *   - Pure data types only. No protocol logic. No logging. No timers.
 *   - All codec functions only take/return cJSON* and C structs.
 *   - Caller owns all cJSON objects (create and delete).
 */

#ifndef SPINE_CODEC_COMMON_H_
#define SPINE_CODEC_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ──────────────────────────────────────────────────────────── */

typedef enum {
    SPINE_OK           = 0,
    SPINE_ERR_INVALID  = 1,   /* null pointer or bad argument */
    SPINE_ERR_PARSE    = 2,   /* JSON parse / structure error */
    SPINE_ERR_OVERFLOW = 3,   /* output buffer too small */
    SPINE_ERR_NOT_FOUND = 4,  /* expected element absent */
    SPINE_ERR_NO_MEMORY = 5,
} spine_err_t;

/* ── ScaledNumber (Resource Spec §3.10.1.8) ──────────────────────────────── */
/*
 * Represents: value = number * 10^scale
 * If scale is absent the default is 0, so value = number.
 */
typedef struct {
    int64_t number;       /* always present per spec §3.10.1.8 rule 3 */
    int8_t  scale;        /* default 0 if absent                       */
} spine_scaled_number_t;

/* ── TimePeriod (Resource Spec §3.10.2.1) ────────────────────────────────── */
/*
 * Both startTime and endTime are ISO 8601 duration strings (relative times)
 * or absolute timestamp strings. We store them as plain strings; the
 * application layer interprets them.
 * Empty string means "absent".
 */
#define SPINE_TIME_STR_MAX 48

typedef struct {
    char start_time[SPINE_TIME_STR_MAX];
    bool start_time_present;
    char end_time[SPINE_TIME_STR_MAX];
    bool end_time_present;
} spine_time_period_t;

/* ── Result error numbers (Resource Spec §3.11, Table 19) ───────────────── */
typedef enum {
    SPINE_RESULT_NO_ERROR                  = 0,
    SPINE_RESULT_GENERAL_ERROR             = 1,
    SPINE_RESULT_TIMEOUT                   = 2,
    SPINE_RESULT_OVERLOAD                  = 3,
    SPINE_RESULT_DEST_UNKNOWN              = 4,
    SPINE_RESULT_DEST_UNREACHABLE          = 5,
    SPINE_RESULT_CMD_NOT_SUPPORTED         = 6,
    SPINE_RESULT_CMD_REJECTED              = 7,
    SPINE_RESULT_RFE_NOT_SUPPORTED         = 8,
    SPINE_RESULT_BINDING_REQUIRED          = 9,
} spine_result_err_t;

/* ── String field sizes ───────────────────────────────────────────────────── */

#define SPINE_FEATURE_TYPE_MAX   48   /* "LoadControl", "NodeManagement" … */
#define SPINE_ENTITY_TYPE_MAX    48   /* "CEM", "ControllableSystem" …     */
#define SPINE_ROLE_MAX           16   /* "server" / "client" / "special"   */
#define SPINE_ENUM_STR_MAX       64   /* generic enum string               */
#define SPINE_UNIT_STR_MAX       16   /* "W", "Wh", "A", "V" …            */
#define SPINE_SCOPE_STR_MAX      48   /* "activePowerLimit" …              */
#define SPINE_LABEL_MAX          64
#define SPINE_DESCRIPTION_MAX   256
#define SPINE_UC_NAME_MAX        64   /* Use Case name                     */
#define SPINE_UC_VERSION_MAX     16
#define SPINE_ACTOR_MAX          32   /* "EnergyGuard", "ControllableSystem" */
#define SPINE_UC_SCENARIO_MAX     8   /* scenarios per useCaseSupport       */
#define SPINE_DEVICE_ID_MAX     256   /* device address string, max 256 per spec §7.1.1.2 */
#define SPINE_FUNCTION_NAME_MAX  96   /* cmd.<FUNCTION> key name            */
#define SPINE_SPEC_VERSION_STR  "1.3.0"  /* current SPINE spec version     */

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_COMMON_H_ */
