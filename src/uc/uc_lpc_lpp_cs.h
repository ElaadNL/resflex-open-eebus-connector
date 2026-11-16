// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * uc_lpc_lpp_cs.h — combined LPC + LPP CS use case on one SPINE entity.
 *
 * For a device that limits both consumption and production (e.g. a V2G
 * charger). Both directions share one entity, one LoadControl feature, one
 * DeviceConfiguration feature and one heartbeat, per LPC §1.3 / LPP §1.3.
 */
#ifndef UC_LPC_LPP_CS_H_
#define UC_LPC_LPP_CS_H_

#include "uc_lpc_cs.h"
#include "uc_lpp_cs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uc_lc_cs_entity_config_t  entity;
    uc_lc_cs_dir_config_t     lpc;
    uc_lc_cs_dir_config_t     lpp;
} uc_lpc_lpp_cs_config_t;

/* Create a combined LPC+LPP CS. Both out handles point into one shared
 * entity. The lpc/lpp initial_failsafe.duration_s and duration_changeable
 * MUST match — otherwise create returns SPINE_ERR_INVALID ([LPC-022] shared
 * failsafe duration). Operational calls use uc_lpc_cs_* on lpc_out and
 * uc_lpp_cs_* on lpp_out. */
static inline spine_err_t uc_lpc_lpp_cs_create(
        uc_lpc_cs_t** lpc_out, uc_lpp_cs_t** lpp_out,
        const uc_lpc_lpp_cs_config_t* cfg) {
    return uc_lc_cs_create(&cfg->entity, &cfg->lpc, &cfg->lpp,
                           lpc_out, lpp_out);
}

/* Destroy a combined LPC+LPP CS. Both handles belong to the same entity;
 * the shared entity is freed once. */
static inline void uc_lpc_lpp_cs_destroy(uc_lpc_cs_t* lpc_cs,
                                          uc_lpp_cs_t* lpp_cs) {
    uc_lc_cs_destroy(lpc_cs ? lpc_cs : lpp_cs);
}

#ifdef __cplusplus
}
#endif

#endif /* UC_LPC_LPP_CS_H_ */
