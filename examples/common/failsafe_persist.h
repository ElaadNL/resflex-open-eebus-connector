// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/failsafe_persist.h
 *
 * Persist a CS's failsafe values (LPC and/or LPP) to disk so the values
 * survive a restart, satisfying the LPC/LPP UC §2.6.2 "SHOULD store
 * changed values persistently" recommendation. File lives next to the
 * demo's TLS material under ~/.eebus/<name>/failsafe.json.
 *
 * On restart, demos call _load() and use the result (when present) to
 * override their hardcoded initial_failsafe; if the file is missing or
 * corrupt the demo falls back to its hardcoded defaults — this matches
 * [LPC-903] / [LPP-903] "data was lost → use pre-configured value".
 *
 * On each accepted EG failsafe write, demos call _update_lpc/lpp() to
 * record the new value atomically (temp file + rename).
 */
#ifndef FAILSAFE_PERSIST_H_
#define FAILSAFE_PERSIST_H_

#include "uc_lc.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool             lpc_present;
    uc_lc_failsafe_t lpc;
    bool             lpp_present;
    uc_lc_failsafe_t lpp;
} failsafe_persist_t;

/* Load ~/.eebus/<name>/failsafe.json. Returns true if the file exists
 * and at least one direction was parsed; out->{lpc,lpp}_present
 * indicate which. Returns false on missing / corrupt file. */
bool failsafe_persist_load(const char* name, failsafe_persist_t* out);

/* Atomic write (temp file + rename) of the full persisted state. */
bool failsafe_persist_save(const char* name, const failsafe_persist_t* in);

/* Read-modify-write: replace one direction, keep the other; if the file
 * doesn't exist, creates one with only that direction. */
bool failsafe_persist_update_lpc(const char* name,
                                  const uc_lc_failsafe_t* lpc);
bool failsafe_persist_update_lpp(const char* name,
                                  const uc_lc_failsafe_t* lpp);

/* Remove ~/.eebus/<name>/failsafe.json. Returns true if the file is
 * gone afterwards (including the "didn't exist" case). */
bool failsafe_persist_clear(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* FAILSAFE_PERSIST_H_ */
