// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lpp_cs.h — LPP-only (Limitation of Power Production) CS use case.
 *
 * Thin inline wrappers over the generic CS API (uc_lc_cs.h). For a device
 * that also limits consumption, use uc_lpc_lpp_cs.h instead.
 */
#ifndef UC_LPP_CS_H_
#define UC_LPP_CS_H_

#include "uc_lc_cs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Handle for the produce-direction context. */
typedef uc_lc_cs_t uc_lpp_cs_t;

/* ── LPP-only lifecycle ──────────────────────────────────────────────────────
 * Hidden when uc_lpc_lpp_cs.h is in use: a combined LPC+LPP device must use
 * the combined create, never the single-direction one. */
#ifndef UC_LPC_LPP_CS_H_

typedef struct {
    uc_lc_cs_entity_config_t  entity;
    uc_lc_cs_dir_config_t     lpp;
} uc_lpp_cs_config_t;

static inline spine_err_t uc_lpp_cs_create(uc_lpp_cs_t** out,
                                            const uc_lpp_cs_config_t* cfg) {
    return uc_lc_cs_create(&cfg->entity, NULL, &cfg->lpp, NULL, out);
}

static inline void uc_lpp_cs_destroy(uc_lpp_cs_t* cs) {
    uc_lc_cs_destroy(cs);
}

#endif /* UC_LPC_LPP_CS_H_ */

/* ── Operational API (always available; thin wrappers) ───────────────────── */

static inline void uc_lpp_cs_get_limit(const uc_lpp_cs_t* cs,
                                         uc_lc_limit_t* out) {
    uc_lc_cs_get_limit(cs, out);
}
static inline spine_err_t uc_lpp_cs_set_limit(uc_lpp_cs_t* cs,
                                               const uc_lc_limit_t* in) {
    return uc_lc_cs_set_limit(cs, in);
}
static inline lc_cs_state_t uc_lpp_cs_get_state(const uc_lpp_cs_t* cs) {
    return uc_lc_cs_get_state(cs);
}
static inline void uc_lpp_cs_get_failsafe(const uc_lpp_cs_t* cs,
                                           uc_lc_failsafe_t* out) {
    uc_lc_cs_get_failsafe(cs, out);
}
static inline spine_err_t uc_lpp_cs_set_failsafe(uc_lpp_cs_t* cs,
                                                  const uc_lc_failsafe_t* in) {
    return uc_lc_cs_set_failsafe(cs, in);
}
static inline spine_err_t uc_lpp_cs_set_available(uc_lpp_cs_t* cs,
                                                   bool available) {
    return uc_lc_cs_set_available(cs, available);
}
static inline void uc_lpp_cs_get_ec_characteristic(
        const uc_lpp_cs_t* cs, uc_lc_ec_characteristic_t* out) {
    uc_lc_cs_get_ec_characteristic(cs, out);
}
static inline spine_err_t uc_lpp_cs_set_ec_characteristic(
        uc_lpp_cs_t* cs, const uc_lc_ec_characteristic_t* in) {
    return uc_lc_cs_set_ec_characteristic(cs, in);
}
static inline bool uc_lpp_cs_get_eg_heartbeat(const uc_lpp_cs_t* cs,
                                               time_t* received_at,
                                               bool* active) {
    return uc_lc_cs_get_eg_heartbeat(cs, received_at, active);
}

#ifdef __cplusplus
}
#endif

#endif /* UC_LPP_CS_H_ */
