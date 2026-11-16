// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lpc_eg.h — LPC EG role (consume direction only).
 *
 * Only include ONE of:
 *   - uc_lpc_eg.h      (LPC only)
 *   - uc_lpp_eg.h      (LPP only)
 *   - uc_lpc_lpp_eg.h  (LPC + LPP combined)
 */

#ifndef UC_LPC_EG_H_
#define UC_LPC_EG_H_

#include "uc_lc_eg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle types and functions: standalone mode only */
#ifndef UC_LPC_LPP_EG_H_
typedef uc_lc_eg_t        uc_lpc_eg_t;
typedef uc_lc_eg_config_t uc_lpc_eg_config_t;

static inline spine_err_t uc_lpc_eg_create(uc_lpc_eg_t** out,
                                            const uc_lpc_eg_config_t* cfg) {
    return uc_lc_eg_create(out, cfg, true, false);
}

static inline void uc_lpc_eg_destroy(uc_lpc_eg_t* eg) {
    uc_lc_eg_destroy(eg);
}
#endif /* UC_LPC_LPP_EG_H_ */

/* Get/write wrappers: always available */

static inline void uc_lpc_eg_get_limit(const uc_lc_eg_t* eg,
                                        const char* session_id,
                                        uc_lc_limit_t* out) {
    uc_lc_eg_get_limit(eg, session_id, LC_DIRECTION_CONSUME, out);
}

static inline void uc_lpc_eg_get_failsafe(const uc_lc_eg_t* eg,
                                            const char* session_id,
                                            uc_lc_failsafe_t* out) {
    uc_lc_eg_get_failsafe(eg, session_id, LC_DIRECTION_CONSUME, out);
}

static inline void uc_lpc_eg_get_ec_characteristic(const uc_lc_eg_t* eg,
                                                     const char* session_id,
                                                     uc_lc_ec_characteristic_t* out) {
    uc_lc_eg_get_ec_characteristic(eg, session_id, LC_DIRECTION_CONSUME, out);
}

static inline spine_err_t uc_lpc_eg_write_limit(uc_lc_eg_t* eg,
                                                  const char* session_id,
                                                  const uc_lc_limit_t* limit) {
    return uc_lc_eg_write_limit(eg, session_id, LC_DIRECTION_CONSUME, limit);
}

static inline spine_err_t uc_lpc_eg_write_failsafe_power(uc_lc_eg_t* eg,
                                                            const char* session_id,
                                                            double power_limit_w) {
    return uc_lc_eg_write_failsafe_power(eg, session_id,
                                          LC_DIRECTION_CONSUME, power_limit_w);
}

static inline spine_err_t uc_lpc_eg_write_failsafe_duration(uc_lc_eg_t* eg,
                                                              const char* session_id,
                                                              uint32_t duration_s) {
    return uc_lc_eg_write_failsafe_duration(eg, session_id,
                                              LC_DIRECTION_CONSUME, duration_s);
}

static inline spine_err_t uc_lpc_eg_set_available(uc_lc_eg_t* eg, bool available) {
    return uc_lc_eg_set_available(eg, LC_DIRECTION_CONSUME, available);
}

#ifdef __cplusplus
}
#endif

#endif /* UC_LPC_EG_H_ */
