// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc.h — Shared types and utilities for LoadControl UC (LPC/LPP).
 *
 * Both CS and EG roles include this file for common type definitions.
 * Role-specific types (e.g. lc_cs_state_t) remain in their own headers.
 */

#ifndef UC_LC_H_
#define UC_LC_H_

#include "../spine/codec/spine_codec_common.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Direction ────────────────────────────────────────────────────────────── */

typedef enum {
    LC_DIRECTION_CONSUME = 0,
    LC_DIRECTION_PRODUCE = 1,
} lc_direction_t;

/* ── EC characteristic type ──────────────────────────────────────────────── */

/** Scenario 4: EC characteristic type (mutually exclusive per spec §2.6.4).
 *  - NOMINAL: non-CEM devices (charger, heatpump, inverter).
 *  - CONTRACTUAL: CEM devices (HEMS, control box).
 *  - NONE: Scenario 4 not supported. */
typedef enum {
    LC_POWER_MAX_NONE,
    LC_POWER_MAX_NOMINAL,
    LC_POWER_MAX_CONTRACTUAL,
} lc_power_max_type_t;

/* ── Limit ────────────────────────────────────────────────────────────────── */

/** LoadControl limit — UC layer type for application interaction. */
typedef struct {
    bool                is_limit_changeable;
    bool                is_limit_active;
    spine_time_period_t time_period;
    double              value_w;          /* power limit in watts */
} uc_lc_limit_t;

/* ── Failsafe ─────────────────────────────────────────────────────────────── */

/** Failsafe configuration — combined power limit + duration. */
typedef struct {
    double   power_limit_w;
    uint32_t duration_s;
    bool     power_limit_changeable;
    bool     duration_changeable;
} uc_lc_failsafe_t;

/* ── EC characteristic ────────────────────────────────────────────────────── */

/** EC characteristic — UC layer type for application interaction. */
typedef struct {
    lc_power_max_type_t type;     /* NOMINAL or CONTRACTUAL; NONE = not supported */
    double              value_w;  /* power in watts */
    uint32_t            electrical_connection_id;  /* align with MPC EC */
    uint32_t            parameter_id;              /* align with MPC EC */
} uc_lc_ec_characteristic_t;

/* ── Duration parsing utilities ───────────────────────────────────────────── */

/** Parse ISO 8601 duration to seconds.
 *  Supports P[nD]T[nH][nM][nS], e.g. "PT2H", "PT120M", "PT7200S".
 *  Returns 0 if unparsable. */
uint32_t uc_lc_parse_duration_s(const char* iso8601);

/** Parse timePeriod endTime to remaining duration in milliseconds.
 *  Supports ISO 8601 duration (relative) or absolute timestamp.
 *  Returns 0 if absent, unparsable, or already expired. */
uint32_t uc_lc_parse_time_period_ms(const spine_time_period_t* tp);

#ifdef __cplusplus
}
#endif

#endif /* UC_LC_H_ */
