// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc_eg.h — Generic LoadControl Energy Guard (EG) role.
 *
 * Merges LPC EG (consume) and LPP EG (produce) into a single
 * implementation. One EG instance handles both directions if enabled.
 *
 * Features are registered once on the EG entity. When both LPC and LPP
 * are enabled, two UC entries are registered under the same entity.
 *
 * Data is cached per remote CS in spine_device cache slots
 * (SPINE_CACHE_LC_EG). Get/write APIs take a direction parameter;
 * uc_lpc_eg.h / uc_lpp_eg.h provide convenience wrappers.
 *
 * All get/write APIs identify the remote CS by session_id (string),
 * consistent with SHIP/SPINE layer conventions.
 */

#ifndef UC_LC_EG_H_
#define UC_LC_EG_H_

#include "uc_lc.h"
#include "../spine/spine_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uc_lc_eg uc_lc_eg_t;

/* ── Callback ────────────────────────────────────────────────────────────── */

/**
 * Remote CS discovered, pre-scenario complete, initial reads done.
 * Application may now call write APIs using the given session_id.
 *
 * IMPORTANT — [LPC-913]: The SDK has sent the heartbeat to the remote
 * CS, but the application MUST send at least one limit write (active
 * or inactive) within 60 seconds, otherwise the CS will progress to
 * UNLIMITED_AUTONOMOUS once its 120s init timer expires. Use
 * uc_lpc_eg_write_limit() / uc_lpp_eg_write_limit() with the desired
 * value; pass is_limit_active=false to explicitly leave the CS
 * unlimited.
 */
typedef void (*uc_lc_eg_cs_ready_fn)(
    uc_lc_eg_t* eg,
    const char* session_id,
    const char* entity_type,
    bool        lpc_supported,
    bool        lpp_supported,
    bool        lpc_available,
    bool        lpp_available,
    void*       userdata);

/* ── Config ──────────────────────────────────────────────────────────────── */

typedef struct {
    spine_device_t*        device;
    spine_addr_t           eg_entity_addr;
    const char*            entity_type;          /* "CEM" or "GridGuard" */
    uc_lc_eg_cs_ready_fn   on_cs_ready;
    void*                  userdata;
    uint32_t               heartbeat_interval_s; /* default 60, <=60 per spec */
    bool                   initial_available;
    /* Feature numbers (0 = default) */
    uint8_t                lc_feature_nr;          /* default 1 */
    uint8_t                cfg_feature_nr;         /* default 2 */
    uint8_t                diag_feature_nr;        /* default 3 */
    uint8_t                diag_client_feature_nr; /* default 4 */
    uint8_t                ec_feature_nr;          /* default 5 */
} uc_lc_eg_config_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

spine_err_t uc_lc_eg_create(uc_lc_eg_t** out, const uc_lc_eg_config_t* cfg,
                             bool lpc_enabled, bool lpp_enabled);
void        uc_lc_eg_destroy(uc_lc_eg_t* eg);

/* ── Get API (read from per-CS cache, filtered by direction) ─────────────── */

void uc_lc_eg_get_limit(const uc_lc_eg_t* eg,
                         const char* session_id,
                         lc_direction_t direction,
                         uc_lc_limit_t* out);

void uc_lc_eg_get_failsafe(const uc_lc_eg_t* eg,
                             const char* session_id,
                             lc_direction_t direction,
                             uc_lc_failsafe_t* out);

void uc_lc_eg_get_ec_characteristic(const uc_lc_eg_t* eg,
                                     const char* session_id,
                                     lc_direction_t direction,
                                     uc_lc_ec_characteristic_t* out);

/* ── Write API (partial write, auto endTime delete handling) ─────────────── */

spine_err_t uc_lc_eg_write_limit(uc_lc_eg_t* eg,
                                  const char* session_id,
                                  lc_direction_t direction,
                                  const uc_lc_limit_t* limit);

spine_err_t uc_lc_eg_write_failsafe_power(uc_lc_eg_t* eg,
                                            const char* session_id,
                                            lc_direction_t direction,
                                            double power_limit_w);

/* Duration is shared LPC/LPP per [LPC/LPP-022]; direction param kept for
 * API symmetry with power, validated for the given CS but otherwise
 * not encoded on the wire (the kv key is direction-agnostic). */
spine_err_t uc_lc_eg_write_failsafe_duration(uc_lc_eg_t* eg,
                                                const char* session_id,
                                                lc_direction_t direction,
                                                uint32_t duration_s);

/* ── Availability ────────────────────────────────────────────────────────── */

spine_err_t uc_lc_eg_set_available(uc_lc_eg_t* eg,
                                    lc_direction_t direction,
                                    bool available);

#ifdef __cplusplus
}
#endif

#endif /* UC_LC_EG_H_ */
