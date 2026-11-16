// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_rfe.h
 *
 * Generic Restricted Function Exchange (RFE) engine.
 * Protocol Spec v1.3.0 §5.3.4, SHIP §11.4.
 *
 * Operates on cJSON trees following the xs:sequence → JSON Array encoding.
 *
 * SCOPE: This engine handles xxxListData functions whose JSON structure is:
 *
 *   {"loadControlLimitListData": [
 *     {"loadControlLimitData": [[item1], [item2], ...]}
 *   ]}
 *
 * All standard list-based SPINE functions follow this pattern.
 * The only per-function difference is the id_field name
 * ("limitId", "measurementId", "keyId", ...).
 *
 * NON-LIST functions (e.g. smartEnergyManagementPsData) have a different
 * structure and are NOT supported by this engine.  They require per-type
 * handling.
 */

#ifndef SPINE_RFE_H_
#define SPINE_RFE_H_

#include "spine_codec_msg.h"
#include "cjson/cJSON.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touched-id output container for spine_rfe_apply_with_touched().
 *
 * Populated with the identifiers of items modified by the call:
 *   - partial merge by id_field        → matching ids
 *   - partial merge by selector        → ids of items the selector matched
 *   - partial merge without identifier → all existing ids
 *   - delete-element                   → ids of items whose fields were removed
 *   - full replace (no filters)        → all ids in incoming_fn
 *
 * Entirely deleted items (delete without elements) are NOT reported because
 * they no longer appear in the output and cannot be identified.
 *
 * `overflow` is set if the touched count exceeds SPINE_RFE_TOUCHED_MAX; in
 * that case the first MAX ids are kept and the caller should treat the
 * result as untrustworthy (typically reject the operation).
 */
#define SPINE_RFE_TOUCHED_MAX 64

typedef struct {
    int64_t ids[SPINE_RFE_TOUCHED_MAX];
    uint8_t count;
    bool    overflow;
} spine_rfe_touched_t;

/**
 * @brief Apply any combination of RFE operations to existing list data.
 *
 * Decision table:
 *   partial   delete    behaviour
 *   ───────   ──────    ──────────────────────────────────────────
 *   absent    absent    Full replace: return copy of incoming_fn
 *   present   absent    Partial merge (implicit ID or explicit selector)
 *   absent    present   Delete (items / fields by selector / elements)
 *   present   present   Delete first, then partial merge (§5.3.4.2)
 *
 * @param existing_fn  Current data, e.g. {"loadControlLimitListData":[...]}
 * @param incoming_fn  Incoming data (same structure). May be NULL for
 *                     delete-only operations.
 * @param id_field     Primary identifier field name for implicit selection.
 * @param partial      Partial filter (NULL or present==false to skip).
 * @param delete_      Delete  filter (NULL or present==false to skip).
 * @return New cJSON with the effective result.  Caller must cJSON_Delete().
 *         Returns NULL on error.
 */
cJSON* spine_rfe_apply(const cJSON*              existing_fn,
                        const cJSON*              incoming_fn,
                        const char*               id_field,
                        const spine_rfe_filter_t* partial,
                        const spine_rfe_filter_t* delete_);

/**
 * @brief Same as spine_rfe_apply(), plus report which item ids were touched.
 *
 * Identical semantics for the returned cJSON. Additionally, `touched_out`
 * (if non-NULL) is filled with the identifiers of items modified by this
 * call (see spine_rfe_touched_t for the precise rules).
 *
 * The returned effective_fn is clean (no internal markers); touched_out is
 * the authoritative source of touched-id information.
 */
cJSON* spine_rfe_apply_with_touched(const cJSON*              existing_fn,
                                     const cJSON*              incoming_fn,
                                     const char*               id_field,
                                     const spine_rfe_filter_t* partial,
                                     const spine_rfe_filter_t* delete_,
                                     spine_rfe_touched_t*      touched_out);

/**
 * @brief Filter list data for a partial read reply.
 *
 * @param full_fn    Complete function data.
 * @param id_field   Primary identifier field name (always kept in output).
 * @param selector   Row filter (NULL = keep all items).
 * @param elements   Column filter (NULL = keep all fields).
 * @return Filtered cJSON.  Caller must cJSON_Delete().
 */
cJSON* spine_rfe_filter_read(const cJSON* full_fn,
                               const char*  id_field,
                               const cJSON* selector,
                               const cJSON* elements);

/* ── Delete filter builder ────────────────────────────────────────────────── */

/** Typed value for selector fields. */
typedef enum {
    SPINE_RFE_VAL_INT,
    SPINE_RFE_VAL_INT64,
    SPINE_RFE_VAL_DOUBLE,
    SPINE_RFE_VAL_STR,
    SPINE_RFE_VAL_BOOL,
} spine_rfe_val_type_t;

typedef struct {
    spine_rfe_val_type_t type;
    union {
        int32_t     i;
        int64_t     i64;
        double      d;
        const char* s;
        bool        b;
    };
} spine_rfe_val_t;

#define SPINE_RFE_INT(v)    ((spine_rfe_val_t){.type = SPINE_RFE_VAL_INT,    .i   = (v)})
#define SPINE_RFE_INT64(v)  ((spine_rfe_val_t){.type = SPINE_RFE_VAL_INT64,  .i64 = (v)})
#define SPINE_RFE_DOUBLE(v) ((spine_rfe_val_t){.type = SPINE_RFE_VAL_DOUBLE, .d   = (v)})
#define SPINE_RFE_STR(v)    ((spine_rfe_val_t){.type = SPINE_RFE_VAL_STR,    .s   = (v)})
#define SPINE_RFE_BOOL(v)   ((spine_rfe_val_t){.type = SPINE_RFE_VAL_BOOL,   .b   = (v)})

/**
 * Build RFE filter (usable for both delete and partial).
 * Caller assigns result to msg.payload.delete_ or msg.payload.partial.
 *
 * Type names auto-derived from fn_name:
 *   Selectors: fn_name + "Selectors"
 *   Elements:  fn_name with "ListData" replaced by "DataElements"
 *
 * @param fn_name      e.g. "loadControlLimitListData"
 * @param sel_keys     Selector field names. NULL if sel_count == 0.
 * @param sel_vals     Selector values (same count). NULL if sel_count == 0.
 * @param sel_count    0 = no selector (match all items).
 * @param elem_paths   NULL-terminated dot-separated paths to delete.
 *                     NULL = delete entire matching item(s).
 *                     e.g. {"timePeriod.endTime", "value", NULL}
 */
void spine_rfe_build_filter(spine_rfe_filter_t* out,
                             const char* fn_name,
                             const char* const* sel_keys,
                             const spine_rfe_val_t* sel_vals,
                             uint8_t sel_count,
                             const char* const* elem_paths);

/** Free selector/elements allocated by spine_rfe_build_filter. */
void spine_rfe_free_filter(spine_rfe_filter_t* f);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_RFE_H_ */
