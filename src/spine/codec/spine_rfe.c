// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_rfe.c
 *
 * Generic RFE engine for xxxListData functions.
 *
 * JSON structure handled:
 *   {"loadControlLimitListData": [         ← fn_data (Object)
 *     {"loadControlLimitData": [           ← fn_seq[0] (Object wrapping items)
 *       [{"limitId":1}, {"value":[...]}],  ← item 1 (Array)
 *       [{"limitId":2}, {"value":[...]}]   ← item 2 (Array)
 *     ]}
 *   ]}
 *
 * cJSON tree layout:
 *   fn_data (cJSON_Object)
 *     └─ child: "xxxListData" (cJSON_Array = fn_seq)
 *          └─ elem[0]: (cJSON_Object)
 *               └─ child: "xxxData" (cJSON_Array = item list)
 *                    ├─ [0]: item1 (cJSON_Array)
 *                    └─ [1]: item2 (cJSON_Array)
 */

#include "spine_rfe.h"
#include <stdio.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════════
 * Touched-marker (private)
 *
 * During an apply pass, each item that is the target of a partial merge,
 * selector match, delete-element, or full-replace is tagged with a synthetic
 * field {"_rfeTouched": true} appended to its xs:sequence array. The public
 * wrappers either strip these markers and discard them (spine_rfe_apply) or
 * extract the ids first and then strip (spine_rfe_apply_with_touched).
 *
 * The marker NEVER reaches outside this translation unit.
 * ══════════════════════════════════════════════════════════════════════════ */

#define RFE_TOUCHED_KEY "_rfeTouched"

/* ══════════════════════════════════════════════════════════════════════════
 * xs:sequence field access helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/* Find the value node for a key in an xs:sequence array.
 * Each element is {"key": value}; returns the cJSON value node or NULL. */
static const cJSON* seq_find(const cJSON* arr, const char* key) {
    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (cJSON_IsObject(elem) && elem->child
            && elem->child->string
            && strcmp(elem->child->string, key) == 0) {
            return elem->child;
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Value comparison
 * ══════════════════════════════════════════════════════════════════════════ */

static bool val_equal(const cJSON* a, const cJSON* b) {
    if (!a || !b || a->type != b->type) return false;
    if (cJSON_IsNumber(a)) return a->valuedouble == b->valuedouble;
    if (cJSON_IsString(a))
        return a->valuestring && b->valuestring
            && strcmp(a->valuestring, b->valuestring) == 0;
    if (cJSON_IsBool(a)) return cJSON_IsTrue(a) == cJSON_IsTrue(b);
    return false;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Selector matching (§5.3.4.7)
 *
 * selector wrapper: {"xxxListDataSelectors": [[sel1], [sel2]]}
 * sel_instance:     [{"limitId": 1}, {"limitDirection": "consume"}]
 *
 * Within one instance: AND (all fields must match).
 * Across instances:   OR  (any match suffices).
 * ══════════════════════════════════════════════════════════════════════════ */

/* Does the item match one selector instance? (AND) */
static bool match_one(const cJSON* item, const cJSON* sel_instance) {
    const cJSON* sel_elem;
    cJSON_ArrayForEach(sel_elem, sel_instance) {
        if (!cJSON_IsObject(sel_elem) || !sel_elem->child) continue;
        const char* name = sel_elem->child->string;
        if (!name) continue;

        const cJSON* item_val = seq_find(item, name);
        if (!item_val || !val_equal(item_val, sel_elem->child))
            return false;
    }
    return true;
}

/* selector = {"xxxSelectors": [[sel1], [sel2]]}
 * selector->child = cJSON member node (type=Array, key="xxxSelectors")
 * Iterate its children as individual selector instances. */
static bool match_any(const cJSON* item, const cJSON* selector) {
    if (!selector || !cJSON_IsObject(selector) || !selector->child)
        return false;
    const cJSON* sel_arr = selector->child;
    const cJSON* inst;
    cJSON_ArrayForEach(inst, sel_arr) {
        if (match_one(item, inst))
            return true;
    }
    return false;
}

/* ══════════════════════════════════════════════════════════════════════════
 * ID extraction (implicit selection §5.3.4.6)
 * ══════════════════════════════════════════════════════════════════════════ */

static int64_t extract_id(const cJSON* item, const char* id_field) {
    const cJSON* v = seq_find(item, id_field);
    return (v && cJSON_IsNumber(v)) ? (int64_t)v->valuedouble : -1;
}

static cJSON* find_by_id(cJSON* list, const char* id_field, int64_t id) {
    cJSON* item;
    cJSON_ArrayForEach(item, list) {
        if (extract_id(item, id_field) == id)
            return item;
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Touched marker: insert / detect / strip on a single item
 * ══════════════════════════════════════════════════════════════════════════ */

/* Idempotent: append {"_rfeTouched": true} unless already present. */
static void mark_item_touched(cJSON* item) {
    if (!item || !cJSON_IsArray(item)) return;
    const cJSON* elem;
    cJSON_ArrayForEach(elem, item) {
        if (cJSON_IsObject(elem) && elem->child
            && elem->child->string
            && strcmp(elem->child->string, RFE_TOUCHED_KEY) == 0) {
            return;
        }
    }
    cJSON* obj = cJSON_CreateObject();
    if (!obj) return;
    cJSON_AddBoolToObject(obj, RFE_TOUCHED_KEY, true);
    cJSON_AddItemToArray(item, obj);
}

/* True iff item carries the marker. */
static bool item_is_touched(const cJSON* item) {
    if (!item || !cJSON_IsArray(item)) return false;
    const cJSON* elem;
    cJSON_ArrayForEach(elem, item) {
        if (cJSON_IsObject(elem) && elem->child
            && elem->child->string
            && strcmp(elem->child->string, RFE_TOUCHED_KEY) == 0
            && cJSON_IsTrue(elem->child)) {
            return true;
        }
    }
    return false;
}

/* Remove the marker from one item (if present). */
static void strip_item_marker(cJSON* item) {
    if (!item || !cJSON_IsArray(item)) return;
    int idx = 0;
    cJSON* elem;
    cJSON_ArrayForEach(elem, item) {
        if (cJSON_IsObject(elem) && elem->child
            && elem->child->string
            && strcmp(elem->child->string, RFE_TOUCHED_KEY) == 0) {
            cJSON_DeleteItemFromArray(item, idx);
            return;
        }
        idx++;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Field-level merge: overwrite existing / append new fields
 * ══════════════════════════════════════════════════════════════════════════ */

static void merge_fields(cJSON* dst, const cJSON* src) {
    mark_item_touched(dst);
    const cJSON* src_elem;
    cJSON_ArrayForEach(src_elem, src) {
        if (!cJSON_IsObject(src_elem) || !src_elem->child) continue;
        const char* name = src_elem->child->string;
        if (!name) continue;
        /* Defensive: never propagate the marker via merge. */
        if (strcmp(name, RFE_TOUCHED_KEY) == 0) continue;

        int idx = 0;
        bool replaced = false;
        cJSON* dst_elem;
        cJSON_ArrayForEach(dst_elem, dst) {
            if (cJSON_IsObject(dst_elem) && dst_elem->child
                && dst_elem->child->string
                && strcmp(dst_elem->child->string, name) == 0) {
                cJSON* dup = cJSON_Duplicate(src_elem, 1);
                if (dup) cJSON_ReplaceItemInArray(dst, idx, dup);
                replaced = true;
                break;
            }
            idx++;
        }
        if (!replaced) {
            cJSON* dup = cJSON_Duplicate(src_elem, 1);
            if (dup) cJSON_AddItemToArray(dst, dup);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Field-level removal (delete with ELEMENTS)
 *
 * elements = {"xxxDataElements": [{"isLimitActive":[]}, {"value":[]}]}
 * elements->child = cJSON member node (type=Array) of field markers.
 * ══════════════════════════════════════════════════════════════════════════ */

static void remove_fields(cJSON* item, const cJSON* elements) {
    if (!elements || !cJSON_IsObject(elements) || !elements->child)
        return;
    mark_item_touched(item);
    const cJSON* marker;
    cJSON_ArrayForEach(marker, elements->child) {
        if (!cJSON_IsObject(marker) || !marker->child) continue;
        const char* name = marker->child->string;
        if (!name) continue;

        /* Check if marker has sub-fields (non-empty array = recurse) */
        bool has_children = cJSON_IsArray(marker->child)
                            && cJSON_GetArraySize(marker->child) > 0;

        int idx = 0;
        cJSON* field;
        cJSON_ArrayForEach(field, item) {
            if (!cJSON_IsObject(field) || !field->child
                || !field->child->string
                || strcmp(field->child->string, name) != 0) {
                idx++;
                continue;
            }
            if (has_children) {
                /* Recurse: delete sub-fields within this field's value */
                remove_fields(field->child, marker);
            } else {
                /* Leaf: delete entire field */
                cJSON_DeleteItemFromArray(item, idx);
            }
            break;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Navigate fn_data → item list
 *
 * fn_data:
 *   {"loadControlLimitListData": [       ← Object (fn_data)
 *     {"loadControlLimitData": [         ← fn_seq[0], Object wrapper
 *       [item1], [item2]                 ← item list (Array of Arrays)
 *     ]}
 *   ]}
 *
 * fn_data->child = "xxxListData" member (cJSON_Array = fn_seq)
 * fn_seq elem[0] = Object whose child = "xxxData" (cJSON_Array = item list)
 *
 * Returns the inner item list Array node, or NULL.
 * ══════════════════════════════════════════════════════════════════════════ */

static cJSON* get_item_list(cJSON* fn) {
    if (!fn || !cJSON_IsObject(fn) || !fn->child) return NULL;
    /* fn->child = fn_seq (cJSON_Array, key="xxxListData") */
    cJSON* fn_seq = fn->child;
    /* Search fn_seq for the first Object member whose child is an Array.
     * All xxxListData xs:sequences have exactly one unbounded child element
     * (maxOccurs="unbounded", type=xs:sequence) which produces the item list. */
    cJSON* elem;
    cJSON_ArrayForEach(elem, fn_seq) {
        if (cJSON_IsObject(elem) && elem->child
            && cJSON_IsArray(elem->child)) {
            return elem->child;
        }
    }
    return NULL;
}

static const cJSON* get_item_list_const(const cJSON* fn) {
    return get_item_list((cJSON*)fn);
}

/*
 * Create an empty fn_data shell matching the two-level structure of src.
 * Produces: {"loadControlLimitListData": [{"loadControlLimitData": []}]}
 */
static cJSON* clone_empty_shell(const cJSON* src) {
    if (!src || !cJSON_IsObject(src) || !src->child) return NULL;

    const char* outer_key = src->child->string;
    if (!outer_key) return NULL;

    /* Find the inner key name ("xxxData") */
    const char* inner_key = NULL;
    const cJSON* elem;
    cJSON_ArrayForEach(elem, src->child) {
        if (cJSON_IsObject(elem) && elem->child
            && elem->child->string
            && cJSON_IsArray(elem->child)) {
            inner_key = elem->child->string;
            break;
        }
    }
    if (!inner_key) return NULL;

    /* Build: {"outerKey": [{"innerKey": []}]} */
    cJSON* inner_arr = cJSON_CreateArray();
    if (!inner_arr) return NULL;

    cJSON* inner_obj = cJSON_CreateObject();
    if (!inner_obj) { cJSON_Delete(inner_arr); return NULL; }
    cJSON_AddItemToObject(inner_obj, inner_key, inner_arr);

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(inner_obj); return NULL; }
    cJSON_AddItemToArray(fn_seq, inner_obj);

    cJSON* result = cJSON_CreateObject();
    if (!result) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(result, outer_key, fn_seq);

    return result;
}

/* ══════════════════════════════════════════════════════════════════════════
 * 5 core operations
 * ══════════════════════════════════════════════════════════════════════════ */

/* Partial merge by id_field (implicit selection, §5.3.4.6).
 *
 * - Incoming item WITH identifier: merge into matching existing item,
 *   or append as new item if no match.
 * - Incoming item WITHOUT identifier: apply its fields to ALL existing
 *   items (§5.3.4.2 *1). */
static void merge_by_id(cJSON* list, const cJSON* in_list,
                          const char* id_field) {
    const cJSON* in_item;
    cJSON_ArrayForEach(in_item, in_list) {
        int64_t id = extract_id(in_item, id_field);

        if (id < 0) {
            /* No identifier: apply to ALL existing items (§5.3.4.2 *1) */
            cJSON* item;
            cJSON_ArrayForEach(item, list) {
                merge_fields(item, in_item);
            }
        } else {
            /* Has identifier: merge into matching item or add new */
            cJSON* found = find_by_id(list, id_field, id);
            if (found) {
                merge_fields(found, in_item);
            } else {
                cJSON* dup = cJSON_Duplicate(in_item, 1);
                if (dup) {
                    cJSON_AddItemToArray(list, dup);
                    mark_item_touched(dup);
                }
            }
        }
    }
}

/* Partial merge by explicit selector (§5.3.4.7):
 * FUNCTION SHALL NOT use identifiers; typically contains one item whose
 * fields are applied to ALL items matching the selector. */
static void merge_by_selector(cJSON* list, const cJSON* in_list,
                                const cJSON* selector) {
    const cJSON* in_item = cJSON_GetArrayItem(in_list, 0);
    if (!in_item) return;
    cJSON* item;
    cJSON_ArrayForEach(item, list) {
        if (match_any(item, selector))
            merge_fields(item, in_item);
    }
}

/* Delete entire items matching selector (§5.3.4.2, Table 6 row 3) */
static void delete_items(cJSON* list, const cJSON* selector) {
    for (int i = cJSON_GetArraySize(list) - 1; i >= 0; i--) {
        cJSON* item = cJSON_GetArrayItem(list, i);
        if (match_any(item, selector))
            cJSON_DeleteItemFromArray(list, i);
    }
}

/* Delete fields from items (§5.3.4.2, Table 6 rows 4/5).
 * selector==NULL → apply to all items; otherwise only matching. */
static void delete_matched_fields(cJSON* list, const cJSON* selector,
                                    const cJSON* elements) {
    cJSON* item;
    cJSON_ArrayForEach(item, list) {
        if (!selector || match_any(item, selector))
            remove_fields(item, elements);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Internal engine — produces an effective_fn with _rfeTouched markers
 * on each item that was modified by this call.
 * ══════════════════════════════════════════════════════════════════════════ */

static cJSON* rfe_apply_internal(const cJSON*              existing_fn,
                                  const cJSON*              incoming_fn,
                                  const char*               id_field,
                                  const spine_rfe_filter_t* partial,
                                  const spine_rfe_filter_t* delete_) {
    if (!existing_fn || !id_field) return NULL;

    bool has_partial = partial && partial->present;
    bool has_delete  = delete_ && delete_->present;

    /* Full replace */
    if (!has_partial && !has_delete) {
        if (!incoming_fn) return NULL;
        cJSON* dup = cJSON_Duplicate(incoming_fn, 1);
        if (!dup) return NULL;
        /* Full replace touches every item by definition. */
        cJSON* list = get_item_list(dup);
        if (list) {
            cJSON* it;
            cJSON_ArrayForEach(it, list) {
                if (cJSON_IsArray(it)) mark_item_touched(it);
            }
        }
        return dup;
    }

    /* Work on a copy of existing */
    cJSON* result = cJSON_Duplicate(existing_fn, 1);
    if (!result) return NULL;

    cJSON* list = get_item_list(result);
    if (!list) { cJSON_Delete(result); return NULL; }

    /* Step 1: Delete (must precede partial per §5.3.4.2 L1384) */
    if (has_delete) {
        if (delete_->selector && delete_->elements)
            delete_matched_fields(list, delete_->selector, delete_->elements);
        else if (delete_->selector)
            delete_items(list, delete_->selector);
        else if (delete_->elements)
            delete_matched_fields(list, NULL, delete_->elements);
    }

    /* Step 2: Partial merge */
    if (has_partial && incoming_fn) {
        const cJSON* in_list = get_item_list_const(incoming_fn);
        if (in_list) {
            if (partial->selector)
                merge_by_selector(list, in_list, partial->selector);
            else
                merge_by_id(list, in_list, id_field);
        }
    }

    return result;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Marker post-processing
 * ══════════════════════════════════════════════════════════════════════════ */

/* Walk effective_fn and strip every marker from every item. */
static void strip_touched_markers(cJSON* fn) {
    cJSON* list = get_item_list(fn);
    if (!list) return;
    cJSON* item;
    cJSON_ArrayForEach(item, list) {
        strip_item_marker(item);
    }
}

/* Walk effective_fn and collect ids of marked items into `out`. */
static void extract_touched_ids(const cJSON* fn,
                                 const char* id_field,
                                 spine_rfe_touched_t* out) {
    memset(out, 0, sizeof(*out));
    const cJSON* list = get_item_list_const(fn);
    if (!list) return;
    const cJSON* item;
    cJSON_ArrayForEach(item, list) {
        if (!item_is_touched(item)) continue;
        int64_t id = extract_id(item, id_field);
        if (id < 0) continue;  /* item without id_field: cannot report */
        if (out->count >= SPINE_RFE_TOUCHED_MAX) {
            out->overflow = true;
            break;
        }
        out->ids[out->count++] = id;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Public: spine_rfe_apply (backward-compatible)
 * ══════════════════════════════════════════════════════════════════════════ */

cJSON* spine_rfe_apply(const cJSON*              existing_fn,
                        const cJSON*              incoming_fn,
                        const char*               id_field,
                        const spine_rfe_filter_t* partial,
                        const spine_rfe_filter_t* delete_) {
    cJSON* eff = rfe_apply_internal(existing_fn, incoming_fn,
                                     id_field, partial, delete_);
    if (eff) strip_touched_markers(eff);
    return eff;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Public: spine_rfe_apply_with_touched
 * ══════════════════════════════════════════════════════════════════════════ */

cJSON* spine_rfe_apply_with_touched(const cJSON*              existing_fn,
                                     const cJSON*              incoming_fn,
                                     const char*               id_field,
                                     const spine_rfe_filter_t* partial,
                                     const spine_rfe_filter_t* delete_,
                                     spine_rfe_touched_t*      touched_out) {
    if (touched_out) memset(touched_out, 0, sizeof(*touched_out));

    cJSON* eff = rfe_apply_internal(existing_fn, incoming_fn,
                                     id_field, partial, delete_);
    if (!eff) return NULL;

    if (touched_out) extract_touched_ids(eff, id_field, touched_out);
    strip_touched_markers(eff);
    return eff;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Public: spine_rfe_filter_read
 * ══════════════════════════════════════════════════════════════════════════ */

/* Keep only fields listed in elements + the id_field (always retained). */
static cJSON* filter_item_fields(const cJSON* item,
                                   const cJSON* elements,
                                   const char* id_field) {
    if (!elements || !cJSON_IsObject(elements) || !elements->child)
        return cJSON_Duplicate(item, 1);

    cJSON* out = cJSON_CreateArray();
    if (!out) return NULL;

    const cJSON* elem_list = elements->child;
    const cJSON* field;
    cJSON_ArrayForEach(field, item) {
        if (!cJSON_IsObject(field) || !field->child) continue;
        const char* name = field->child->string;
        if (!name) continue;

        /* Always keep the identifier */
        if (strcmp(name, id_field) == 0) {
            cJSON* dup = cJSON_Duplicate(field, 1);
            if (dup) cJSON_AddItemToArray(out, dup);
            continue;
        }

        /* Keep if listed in elements */
        const cJSON* marker;
        cJSON_ArrayForEach(marker, elem_list) {
            if (cJSON_IsObject(marker) && marker->child
                && marker->child->string
                && strcmp(marker->child->string, name) == 0) {
                cJSON* dup = cJSON_Duplicate(field, 1);
                if (dup) cJSON_AddItemToArray(out, dup);
                break;
            }
        }
    }
    return out;
}

cJSON* spine_rfe_filter_read(const cJSON* full_fn,
                               const char*  id_field,
                               const cJSON* selector,
                               const cJSON* elements) {
    if (!full_fn || !id_field) return NULL;
    if (!selector && !elements)
        return cJSON_Duplicate(full_fn, 1);

    cJSON* result = clone_empty_shell(full_fn);
    if (!result) return NULL;

    cJSON* result_list = get_item_list(result);
    if (!result_list) { cJSON_Delete(result); return NULL; }

    const cJSON* src_list = get_item_list_const(full_fn);
    if (!src_list) { cJSON_Delete(result); return NULL; }

    const cJSON* item;
    cJSON_ArrayForEach(item, src_list) {
        if (selector && !match_any(item, selector))
            continue;

        if (elements) {
            cJSON* filtered = filter_item_fields(item, elements, id_field);
            if (filtered) cJSON_AddItemToArray(result_list, filtered);
        } else {
            cJSON* dup = cJSON_Duplicate(item, 1);
            if (dup) cJSON_AddItemToArray(result_list, dup);
        }
    }
    return result;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Delete filter builder
 * ══════════════════════════════════════════════════════════════════════════ */

/* Build nested xs:sequence for a dot-separated path.
 * "timePeriod.endTime" → {"timePeriod": [{"endTime": []}]} */
static cJSON* build_element_path(const char* path) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);

    const char* parts[8];
    uint8_t depth = 0;
    char* saveptr = NULL;
    char* tok = strtok_r(buf, ".", &saveptr);
    while (tok && depth < 8) { parts[depth++] = tok; tok = strtok_r(NULL, ".", &saveptr); }
    if (depth == 0) return NULL;

    /* Build inside-out: innermost = [] (ElementTagType) */
    cJSON* inner = cJSON_CreateArray();
    for (int d = (int)depth - 1; d >= 0; d--) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddItemToObject(obj, parts[d], inner);
        if (d > 0) {
            inner = cJSON_CreateArray();
            cJSON_AddItemToArray(inner, obj);
        } else {
            inner = obj;
        }
    }
    return inner;
}

/* Derive elements type name: replace "ListData" with "DataElements".
 * "loadControlLimitListData" → "loadControlLimitDataElements" */
static void derive_elements_type(const char* fn_name, char* out, size_t sz) {
    snprintf(out, sz, "%s", fn_name);
    char* p = strstr(out, "ListData");
    if (p)
        snprintf(p, sz - (size_t)(p - out), "DataElements");
}

void spine_rfe_build_filter(spine_rfe_filter_t* out,
                             const char* fn_name,
                             const char* const* sel_keys,
                             const spine_rfe_val_t* sel_vals,
                             uint8_t sel_count,
                             const char* const* elem_paths) {
    memset(out, 0, sizeof(*out));
    out->present = true;

    /* Selector: {fn_name + "Selectors": [[{"k1":v1}, {"k2":v2}]]} */
    if (sel_count > 0 && sel_keys && sel_vals) {
        cJSON* inst = cJSON_CreateArray();
        for (uint8_t i = 0; i < sel_count; i++) {
            cJSON* obj = cJSON_CreateObject();
            switch (sel_vals[i].type) {
            case SPINE_RFE_VAL_INT:
                cJSON_AddNumberToObject(obj, sel_keys[i], sel_vals[i].i);
                break;
            case SPINE_RFE_VAL_INT64:
                cJSON_AddNumberToObject(obj, sel_keys[i], (double)sel_vals[i].i64);
                break;
            case SPINE_RFE_VAL_DOUBLE:
                cJSON_AddNumberToObject(obj, sel_keys[i], sel_vals[i].d);
                break;
            case SPINE_RFE_VAL_STR:
                cJSON_AddStringToObject(obj, sel_keys[i], sel_vals[i].s);
                break;
            case SPINE_RFE_VAL_BOOL:
                cJSON_AddBoolToObject(obj, sel_keys[i], sel_vals[i].b);
                break;
            }
            cJSON_AddItemToArray(inst, obj);
        }
        cJSON* arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, inst);

        char sel_type[128];
        snprintf(sel_type, sizeof(sel_type), "%sSelectors", fn_name);
        cJSON* wrap = cJSON_CreateObject();
        cJSON_AddItemToObject(wrap, sel_type, arr);
        out->selector = wrap;
    }

    /* Elements: {elem_type: [path1_obj, path2_obj, ...]} */
    if (elem_paths) {
        cJSON* seq = cJSON_CreateArray();
        for (int i = 0; elem_paths[i]; i++) {
            cJSON* entry = build_element_path(elem_paths[i]);
            if (entry) cJSON_AddItemToArray(seq, entry);
        }

        char elem_type[128];
        derive_elements_type(fn_name, elem_type, sizeof(elem_type));
        cJSON* wrap = cJSON_CreateObject();
        cJSON_AddItemToObject(wrap, elem_type, seq);
        out->elements = wrap;
    }
}

void spine_rfe_free_filter(spine_rfe_filter_t* f) {
    if (!f) return;
    if (f->selector) { cJSON_Delete(f->selector); f->selector = NULL; }
    if (f->elements) { cJSON_Delete(f->elements); f->elements = NULL; }
    f->present = false;
}
