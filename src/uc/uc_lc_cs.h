// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lc_cs.h — LoadControl Controllable System (CS): shared base types.
 *
 * Defines the per-direction context handle, callbacks and config structures
 * shared by the LPC-only / LPP-only / combined use-case headers. Application
 * code includes uc_lpc_cs.h, uc_lpp_cs.h, or uc_lpc_lpp_cs.h — not this file.
 */
#ifndef UC_LC_CS_H_
#define UC_LC_CS_H_

#include "uc_lc.h"
#include "../spine/spine_device.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── CS state machine (LPC §2.3 / LPP §2.3) ──────────────────────────────── */
typedef enum {
    LC_STATE_INIT = 0,                 /* After (re)start, limited by failsafe   */
    LC_STATE_UNLIMITED_CONTROLLED,     /* EG in control, no active limit         */
    LC_STATE_LIMITED,                  /* EG set an active limit                 */
    LC_STATE_FAILSAFE,                 /* EG heartbeat timeout, failsafe applied */
    LC_STATE_UNLIMITED_AUTONOMOUS,     /* No EG control, CS operates autonomously*/
} lc_cs_state_t;

/* ── Opaque handle ───────────────────────────────────────────────────────── */
/* One direction's context. uc_lpc_cs_t / uc_lpp_cs_t alias this. The entity
 * container that owns one or two of these is internal to uc_lc_cs.c. */
typedef struct uc_lc_cs uc_lc_cs_t;

/* ── LoadControl directions (bitmask) ────────────────────────────────────── */
/* Used by uc_lc_cs_eg_ready_fn to report, per direction, which LoadControl
 * use-cases the remote EG declared and which are currently available — so a
 * combined LPC+LPP CS knows whether the connected EG will actually control
 * both directions. */
typedef enum {
    UC_LC_EG_DIR_LPC = 1u << 0,
    UC_LC_EG_DIR_LPP = 1u << 1,
} uc_lc_eg_dir_t;

/* ── Callbacks (all invoked WITHOUT the UC mutex held) ────────────────────── */

/* EG wrote limit(s). Invoked ONCE per write message, even when it touches
 * both directions — the application makes a single atomic accept/reject.
 * lpc_limit / lpp_limit is NULL if that direction was not touched by this
 * write. Return true = accept ALL, false = reject ALL. A NULL callback
 * auto-rejects every write. */
typedef bool (*uc_lc_cs_limit_write_fn)(
    const uc_lc_limit_t* lpc_limit,
    const uc_lc_limit_t* lpp_limit,
    void*                userdata);

/* EG wrote failsafe config. Invoked ONCE per write message. A change to the
 * shared failsafe duration counts as touching BOTH directions. lpc_failsafe /
 * lpp_failsafe is NULL if that direction was not touched. Return true =
 * accept ALL. A NULL callback auto-rejects. */
typedef bool (*uc_lc_cs_failsafe_write_fn)(
    const uc_lc_failsafe_t* lpc_failsafe,
    const uc_lc_failsafe_t* lpp_failsafe,
    void*                   userdata);

/* Remote EG peer became ready / unavailable. eg_support is a bitmask of the
 * LoadControl directions both the EG declared and this CS has enabled;
 * eg_available is the subset of those currently available (eg_available is
 * always a subset of eg_support). */
typedef void (*uc_lc_cs_eg_ready_fn)(
    const char*    session_id,
    const char*    entity_type,
    uc_lc_eg_dir_t eg_support,
    uc_lc_eg_dir_t eg_available,
    void*          userdata);

/* CS state changed for one direction. Invoked per direction; `cs` is the
 * handle the create function returned for that direction. Also fires when the
 * direction stays LIMITED but the limit value/timePeriod was replaced
 * (old_state == new_state == LC_STATE_LIMITED). */
typedef void (*uc_lc_cs_state_change_fn)(
    uc_lc_cs_t*   cs,
    lc_cs_state_t old_state,
    lc_cs_state_t new_state,
    void*         userdata);

/* ── Entity-level config (shared across directions) ──────────────────────── */
typedef struct {
    spine_device_t* device;

    /* CS entity address — LoadControl/DeviceConfig/DeviceDiag/EC features
     * are registered under this entity. */
    spine_addr_t    cs_entity_addr;
    const char*     entity_type;

    /* Feature numbers within the CS entity */
    uint8_t         lc_feature_nr;          /* default 1 */
    uint8_t         cfg_feature_nr;         /* default 2 */
    uint8_t         diag_feature_nr;        /* default 3 */
    uint8_t         diag_client_feature_nr; /* default 4 */
    uint8_t         ec_feature_nr;          /* default 5 */

    /* CS heartbeat interval (<=60 s per [LPC-006] / [LPP-006]) */
    uint32_t        heartbeat_interval_s;
    bool            initial_available;

    /* Cross-reference to MPC measurementId (0 = not set) */
    uint32_t        measurement_id;

    /* Callbacks (all invoked WITHOUT holding the UC mutex) */
    uc_lc_cs_limit_write_fn    on_limit_write;
    uc_lc_cs_failsafe_write_fn on_failsafe_write;
    uc_lc_cs_eg_ready_fn       on_eg_ready;
    void*                      userdata;
} uc_lc_cs_entity_config_t;

/* ── Per-direction config ────────────────────────────────────────────────── */
typedef struct {
    uc_lc_limit_t             initial_limit;

    /* Initial failsafe values. duration_s / duration_changeable are shared
     * across directions ([LPC-022]): in combined LPC+LPP mode they MUST match
     * between the two directions or create fails. */
    uc_lc_failsafe_t          initial_failsafe;

    /* ElectricalConnection characteristic (Scenario 4, §2.6.4) */
    uc_lc_ec_characteristic_t initial_ec_characteristic;

    /* Per-direction state change callback (invoked WITHOUT the UC mutex) */
    uc_lc_cs_state_change_fn  on_state_change;
} uc_lc_cs_dir_config_t;

/* ── Generic lifecycle ───────────────────────────────────────────────────────
 * These are the real implementation. Application code uses the thin inline
 * wrappers in uc_lpc_cs.h / uc_lpp_cs.h / uc_lpc_lpp_cs.h instead. */

/* Create a CS with one or both directions enabled. lpc_cfg / lpp_cfg NULL =
 * that direction disabled (at least one must be non-NULL). lpc_out / lpp_out
 * receive that direction's context handle (may be NULL if unwanted). In
 * combined mode the two directions' initial_failsafe.duration_s and
 * duration_changeable MUST match ([LPC-022]) or SPINE_ERR_INVALID is returned. */
spine_err_t uc_lc_cs_create(const uc_lc_cs_entity_config_t* entity_cfg,
                             const uc_lc_cs_dir_config_t*   lpc_cfg,
                             const uc_lc_cs_dir_config_t*   lpp_cfg,
                             uc_lc_cs_t**                   lpc_out,
                             uc_lc_cs_t**                   lpp_out);

/* Destroy the CS owning `cs`. Pass any one of its direction handles; the
 * shared entity is freed once. */
void        uc_lc_cs_destroy(uc_lc_cs_t* cs);

/* ── Generic operational API ─────────────────────────────────────────────────
 * `cs` is a direction context handle; the direction is implicit in it.
 * Application code uses the per-use-case inline wrappers instead. */

void          uc_lc_cs_get_limit(const uc_lc_cs_t* cs, uc_lc_limit_t* out);
spine_err_t   uc_lc_cs_set_limit(uc_lc_cs_t* cs, const uc_lc_limit_t* in);
lc_cs_state_t uc_lc_cs_get_state(const uc_lc_cs_t* cs);
void          uc_lc_cs_get_failsafe(const uc_lc_cs_t* cs, uc_lc_failsafe_t* out);
spine_err_t   uc_lc_cs_set_failsafe(uc_lc_cs_t* cs, const uc_lc_failsafe_t* in);
spine_err_t   uc_lc_cs_set_available(uc_lc_cs_t* cs, bool available);
void          uc_lc_cs_get_ec_characteristic(const uc_lc_cs_t* cs,
                                              uc_lc_ec_characteristic_t* out);
spine_err_t   uc_lc_cs_set_ec_characteristic(uc_lc_cs_t* cs,
                                              const uc_lc_ec_characteristic_t* in);

/* Last EnergyGuard heartbeat received by this CS entity. Returns true if at
 * least one heartbeat has EVER been received. *received_at (if non-NULL) gets
 * the local reception time of the most recent one — kept even after a
 * timeout. *active (if non-NULL) is false once the 120s watchdog fired
 * without a refresh (link currently stale). Heartbeat state is entity-shared
 * across LPC/LPP. */
bool          uc_lc_cs_get_eg_heartbeat(const uc_lc_cs_t* cs,
                                        time_t* received_at, bool* active);

#ifdef __cplusplus
}
#endif

#endif /* UC_LC_CS_H_ */
