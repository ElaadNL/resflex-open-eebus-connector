// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lpp_eg.h — LPP EG role (produce direction only).
 *
 * Only include ONE of:
 *   - uc_lpc_eg.h      (LPC only)
 *   - uc_lpp_eg.h      (LPP only)
 *   - uc_lpc_lpp_eg.h  (LPC + LPP combined)
 */

#ifndef UC_LPP_EG_H_
#define UC_LPP_EG_H_

#include "uc_lc_eg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle types and functions: standalone mode only */
#ifndef UC_LPC_LPP_EG_H_
typedef uc_lc_eg_t        uc_lpp_eg_t;
typedef uc_lc_eg_config_t uc_lpp_eg_config_t;

static inline spine_err_t uc_lpp_eg_create(uc_lpp_eg_t** out,
                                            const uc_lpp_eg_config_t* cfg) {
    return uc_lc_eg_create(out, cfg, false, true);
}

static inline void uc_lpp_eg_destroy(uc_lpp_eg_t* eg) {
    uc_lc_eg_destroy(eg);
}
#endif /* UC_LPC_LPP_EG_H_ */

/* Get/write wrappers: always available */

static inline void uc_lpp_eg_get_limit(const uc_lc_eg_t* eg,
                                        const char* session_id,
                                        uc_lc_limit_t* out) {
    uc_lc_eg_get_limit(eg, session_id, LC_DIRECTION_PRODUCE, out);
}

static inline void uc_lpp_eg_get_failsafe(const uc_lc_eg_t* eg,
                                            const char* session_id,
                                            uc_lc_failsafe_t* out) {
    uc_lc_eg_get_failsafe(eg, session_id, LC_DIRECTION_PRODUCE, out);
}

static inline void uc_lpp_eg_get_ec_characteristic(const uc_lc_eg_t* eg,
                                                     const char* session_id,
                                                     uc_lc_ec_characteristic_t* out) {
    uc_lc_eg_get_ec_characteristic(eg, session_id, LC_DIRECTION_PRODUCE, out);
}

static inline spine_err_t uc_lpp_eg_write_limit(uc_lc_eg_t* eg,
                                                  const char* session_id,
                                                  const uc_lc_limit_t* limit) {
    return uc_lc_eg_write_limit(eg, session_id, LC_DIRECTION_PRODUCE, limit);
}

static inline spine_err_t uc_lpp_eg_write_failsafe_power(uc_lc_eg_t* eg,
                                                            const char* session_id,
                                                            double power_limit_w) {
    return uc_lc_eg_write_failsafe_power(eg, session_id,
                                          LC_DIRECTION_PRODUCE, power_limit_w);
}

static inline spine_err_t uc_lpp_eg_write_failsafe_duration(uc_lc_eg_t* eg,
                                                              const char* session_id,
                                                              uint32_t duration_s) {
    return uc_lc_eg_write_failsafe_duration(eg, session_id,
                                              LC_DIRECTION_PRODUCE, duration_s);
}

static inline spine_err_t uc_lpp_eg_set_available(uc_lc_eg_t* eg, bool available) {
    return uc_lc_eg_set_available(eg, LC_DIRECTION_PRODUCE, available);
}

#ifdef __cplusplus
}
#endif

#endif /* UC_LPP_EG_H_ */
