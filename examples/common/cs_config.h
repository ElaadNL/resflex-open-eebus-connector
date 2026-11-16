// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/cs_config.h
 *
 * Per-direction CS configuration parameters persisted to
 * ~/.eebus/<name>/cs_config.json. Read/write only — the values are
 * stored independently of the running SDK state; they do not feed
 * into uc_*_cs_create() yet. Wire-up to runtime will come later.
 *
 * Field naming is direction-neutral:
 *   apl/fapl/pfapl  -> Active / Failsafe / Pre-configured Failsafe
 *                      Active Power Limit (consumption for LPC,
 *                      production for LPP)
 *   mfsdm/pfsdm     -> Max / Pre-configured Failsafe Duration Minimum
 *   nominal_max     -> Consumption Nominal Max (LPC) or
 *                      Production Nominal Max (LPP)
 *
 * Register the bundled CLI handler with:
 *   cli_register(cli, "cfg", cs_config_cli_handler, help, (void*)NAME)
 * where NAME is the demo's *_DEMO_NAME string.
 */
#ifndef CS_CONFIG_H_
#define CS_CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double   apl_max_w;     /* Active Power Limit max */
    double   apl_min_w;     /* Active Power Limit min */
    double   fapl_max_w;    /* Failsafe APL max */
    double   fapl_min_w;    /* Failsafe APL min */
    double   pfapl_w;       /* Pre-configured Failsafe APL */
    uint32_t mfsdm_s;       /* Max Failsafe Duration Minimum (s) */
    uint32_t pfsdm_s;       /* Pre-configured Failsafe Duration Minimum (s) */
    double   nominal_max_w; /* Nominal Max (direction-dependent meaning) */
} cs_config_dir_t;

typedef struct {
    bool             lpc_present;
    cs_config_dir_t  lpc;
    bool             lpp_present;
    cs_config_dir_t  lpp;
} cs_config_t;

bool cs_config_load(const char* name, cs_config_t* out);
bool cs_config_save(const char* name, const cs_config_t* in);
bool cs_config_update_lpc(const char* name, const cs_config_dir_t* lpc);
bool cs_config_update_lpp(const char* name, const cs_config_dir_t* lpp);

/* Create ~/.eebus/<name>/cs_config.json with `defaults` if the file
 * does not yet exist. No-op when the file is already present (does not
 * merge missing fields). Returns true if config is in place (created
 * or already present); false on I/O error. */
bool cs_config_init_if_absent(const char* name, const cs_config_t* defaults);

/* CLI handler — register with cli_register("cfg", ...). userdata must
 * be the demo name string (e.g. (void*)HP_DEMO_NAME). */
void cs_config_cli_handler(int argc, char** argv, void* ud);

#ifdef __cplusplus
}
#endif

#endif /* CS_CONFIG_H_ */
