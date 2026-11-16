// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lpc_lpp_eg.h — Combined LPC + LPP EG role.
 *
 * Only include ONE of:
 *   - uc_lpc_eg.h      (LPC only)
 *   - uc_lpp_eg.h      (LPP only)
 *   - uc_lpc_lpp_eg.h  (LPC + LPP combined)
 */

#ifndef UC_LPC_LPP_EG_H_
#define UC_LPC_LPP_EG_H_   /* also suppresses sub-header lifecycle definitions */

#include "uc_lpc_eg.h"     /* get/write wrappers for consume */
#include "uc_lpp_eg.h"     /* get/write wrappers for produce */

#ifdef __cplusplus
extern "C" {
#endif

typedef uc_lc_eg_t        uc_lpc_lpp_eg_t;
typedef uc_lc_eg_config_t uc_lpc_lpp_eg_config_t;

static inline spine_err_t uc_lpc_lpp_eg_create(uc_lpc_lpp_eg_t** out,
                                                const uc_lpc_lpp_eg_config_t* cfg) {
    return uc_lc_eg_create(out, cfg, true, true);
}

static inline void uc_lpc_lpp_eg_destroy(uc_lpc_lpp_eg_t* eg) {
    uc_lc_eg_destroy(eg);
}

#ifdef __cplusplus
}
#endif

#endif /* UC_LPC_LPP_EG_H_ */
