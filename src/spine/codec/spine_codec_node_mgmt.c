// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_node_mgmt.c — NodeManagement codec (Array format)
 */

#include "spine_codec_node_mgmt.h"
#include "spine_codec_address.h"
#include <string.h>
#include <stdio.h>

/* ── seq helpers ─────────────────────────────────────────────────────────── */

static bool seq_add_str(cJSON* a, const char* k, const char* v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddStringToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_arr(cJSON* a, const char* k, cJSON* inner) {
    cJSON* o = cJSON_CreateObject(); if (!o) { cJSON_Delete(inner); return false; }
    cJSON_AddItemToObject(o, k, inner); cJSON_AddItemToArray(a, o); return true;
}
static const cJSON* seq_get(const cJSON* a, const char* k) {
    if (!cJSON_IsArray(a)) return NULL;
    const cJSON* e;
    cJSON_ArrayForEach(e, a) {
        if (!cJSON_IsObject(e) || !e->child) continue;
        if (strcmp(e->child->string, k) == 0) return e->child;
    }
    return NULL;
}

/* ── nm_addr_t encode/decode (local helpers) ──────────────────────────────── */

static cJSON* nm_addr_encode_local(const nm_addr_t* a, bool include_device,
                                   bool include_feature) {
    cJSON* arr = cJSON_CreateArray();
    if (!arr) return NULL;

    if (include_device && a->device[0]) {
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(arr); return NULL; }
        cJSON_AddStringToObject(o, "device", a->device);
        cJSON_AddItemToArray(arr, o);
    }
    {
        cJSON* ent_arr = cJSON_CreateArray();
        if (!ent_arr) { cJSON_Delete(arr); return NULL; }
        for (uint8_t i = 0; i < a->entity_depth; i++)
            cJSON_AddItemToArray(ent_arr, cJSON_CreateNumber(a->entity[i]));
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(ent_arr); cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToObject(o, "entity", ent_arr);
        cJSON_AddItemToArray(arr, o);
    }
    if (include_feature && a->feature_present) {
        cJSON* o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(arr); return NULL; }
        cJSON_AddNumberToObject(o, "feature", (double)a->feature);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static void decode_nm_addr(const cJSON* arr, nm_addr_t* out) {
    memset(out, 0, sizeof(*out));
    if (!cJSON_IsArray(arr)) return;
    const cJSON* elem;
    cJSON_ArrayForEach(elem, arr) {
        if (!cJSON_IsObject(elem) || !elem->child) continue;
        const char* k = elem->child->string;
        const cJSON* v = elem->child;
        if (strcmp(k, "device") == 0 && cJSON_IsString(v) && v->valuestring)
            snprintf(out->device, sizeof(out->device), "%s", v->valuestring);
        else if (strcmp(k, "entity") == 0 && cJSON_IsArray(v)) {
            const cJSON* e;
            cJSON_ArrayForEach(e, v) {
                if (out->entity_depth >= NM_ENTITY_MAX_DEPTH) break;
                if (cJSON_IsNumber(e))
                    out->entity[out->entity_depth++] = (uint8_t)e->valuedouble;
            }
        } else if (strcmp(k, "feature") == 0 && cJSON_IsNumber(v)) {
            out->feature         = (uint16_t)v->valuedouble;
            out->feature_present = true;
        }
    }
}

bool nm_addr_equal(const nm_addr_t* a, const nm_addr_t* b) {
    if (!a || !b) return false;
    if (strcmp(a->device, b->device) != 0) return false;
    if (a->entity_depth != b->entity_depth) return false;
    for (uint8_t i = 0; i < a->entity_depth; i++)
        if (a->entity[i] != b->entity[i]) return false;
    if (a->feature_present != b->feature_present) return false;
    if (a->feature_present && a->feature != b->feature) return false;
    return true;
}

/* nm_parse_state_change / nm_state_change_str: declared in .h as non-static */
nm_state_change_t nm_parse_state_change(const char* s) {
    if (!s) return NM_STATE_CHANGE_NONE;
    if (!strcmp(s, "added"))    return NM_STATE_CHANGE_ADDED;
    if (!strcmp(s, "removed"))  return NM_STATE_CHANGE_REMOVED;
    if (!strcmp(s, "modified")) return NM_STATE_CHANGE_MODIFIED;
    return NM_STATE_CHANGE_NONE;
}

const char* nm_state_change_str(nm_state_change_t sc) {
    switch (sc) {
        case NM_STATE_CHANGE_ADDED:    return "added";
        case NM_STATE_CHANGE_REMOVED:  return "removed";
        case NM_STATE_CHANGE_MODIFIED: return "modified";
        default: return NULL;
    }
}

/* ── Strict validator for nodeManagementDetailedDiscoveryData ──────────────
 * Verifies the raw cJSON tree against the SPINE NodeManagement XSD before
 * decoding. Every xs:sequence element must use a key listed in this layer's
 * whitelist (no unknown fields allowed — conformance requirement), plus a
 * few required-field/semantic checks. Any violation returns SPINE_ERR_PARSE
 * and the caller (nm_discovery_data_decode) propagates that as a decode
 * failure, which the NM handler already treats as "stop further interaction"
 * (no state advance, no subscribe, no binding, no peer_ready). */

static const char* const TOP_KEYS[] = {
    "specificationVersionList", "deviceInformation",
    "entityInformation", "featureInformation",
};
static const char* const SPEC_LIST_KEYS[] = { "specificationVersion" };
static const char* const DEV_INFO_KEYS[]  = { "description" };
static const char* const DEV_DESC_KEYS[]  = {
    "deviceAddress", "deviceType", "networkManagementResponsibleAddress",
    "nativeSetup", "technologyAddress", "communicationsTechnologyInformation",
    "networkFeatureSet", "lastStateChange", "minimumTrustLevel",
    "label", "description",
};
static const char* const DEV_ADDR_KEYS[]  = { "device" };
static const char* const ENT_INFO_KEYS[]  = { "description" };
static const char* const ENT_DESC_KEYS[]  = {
    "entityAddress", "entityType", "lastStateChange",
    "minimumTrustLevel", "label", "description",
};
static const char* const ENT_ADDR_KEYS[]  = { "entity" };
static const char* const FEAT_INFO_KEYS[] = { "description" };
static const char* const FEAT_DESC_KEYS[] = {
    "featureAddress", "featureType", "specificUsage", "featureGroup",
    "role", "supportedFunction", "lastStateChange", "minimumTrustLevel",
    "label", "description", "maxResponseDelay",
};
static const char* const FEAT_ADDR_KEYS[] = { "entity", "feature" };
static const char* const SUPP_FN_KEYS[]   = { "function", "possibleOperations" };
static const char* const POSS_OPS_KEYS[]  = { "read", "write" };
static const char* const RD_WR_KEYS[]     = { "partial" };

#define WL_N(a) (sizeof(a)/sizeof((a)[0]))

/* True iff every element in `seq` is a single-key object whose key is one of
 * `allowed[0..n-1]`. A non-array `seq` is treated as failure. */
static bool seq_keys_in_whitelist(const cJSON* seq,
                                   const char* const* allowed, size_t n) {
    if (!cJSON_IsArray(seq)) return false;
    const cJSON* el;
    cJSON_ArrayForEach(el, seq) {
        if (!cJSON_IsObject(el) || !el->child || !el->child->string)
            return false;
        const char* k = el->child->string;
        bool ok = false;
        for (size_t i = 0; i < n; i++)
            if (strcmp(allowed[i], k) == 0) { ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

static spine_err_t validate_entity_block(const cJSON* ent_node) {
    if (!cJSON_IsArray(ent_node) ||
        !seq_keys_in_whitelist(ent_node, ENT_INFO_KEYS, WL_N(ENT_INFO_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* edesc = seq_get(ent_node, "description");
    if (!edesc) return SPINE_ERR_PARSE;
    if (!seq_keys_in_whitelist(edesc, ENT_DESC_KEYS, WL_N(ENT_DESC_KEYS)))
        return SPINE_ERR_PARSE;
    /* entityAddress required, must carry at least one entity element */
    const cJSON* ea = seq_get(edesc, "entityAddress");
    if (!ea || !seq_keys_in_whitelist(ea, ENT_ADDR_KEYS, WL_N(ENT_ADDR_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* ent_arr = seq_get(ea, "entity");
    if (!cJSON_IsArray(ent_arr) || cJSON_GetArraySize(ent_arr) < 1)
        return SPINE_ERR_PARSE;
    /* entityType required */
    const cJSON* et = seq_get(edesc, "entityType");
    if (!cJSON_IsString(et) || !et->valuestring || !et->valuestring[0])
        return SPINE_ERR_PARSE;
    return SPINE_OK;
}

static spine_err_t validate_feature_block(const cJSON* feat_node) {
    if (!cJSON_IsArray(feat_node) ||
        !seq_keys_in_whitelist(feat_node, FEAT_INFO_KEYS, WL_N(FEAT_INFO_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* fdesc = seq_get(feat_node, "description");
    if (!fdesc) return SPINE_ERR_PARSE;
    if (!seq_keys_in_whitelist(fdesc, FEAT_DESC_KEYS, WL_N(FEAT_DESC_KEYS)))
        return SPINE_ERR_PARSE;
    /* featureAddress required: must carry entity AND feature */
    const cJSON* fa = seq_get(fdesc, "featureAddress");
    if (!fa || !seq_keys_in_whitelist(fa, FEAT_ADDR_KEYS, WL_N(FEAT_ADDR_KEYS)))
        return SPINE_ERR_PARSE;
    if (!seq_get(fa, "entity") || !seq_get(fa, "feature"))
        return SPINE_ERR_PARSE;
    /* featureType + role required */
    const cJSON* ft = seq_get(fdesc, "featureType");
    if (!cJSON_IsString(ft) || !ft->valuestring || !ft->valuestring[0])
        return SPINE_ERR_PARSE;
    const cJSON* rl = seq_get(fdesc, "role");
    if (!cJSON_IsString(rl) || !rl->valuestring || !rl->valuestring[0])
        return SPINE_ERR_PARSE;
    /* supportedFunction (optional) — sub-tree must be well-formed if present */
    const cJSON* sf_arr = seq_get(fdesc, "supportedFunction");
    if (sf_arr) {
        if (!cJSON_IsArray(sf_arr)) return SPINE_ERR_PARSE;
        uint8_t sf_count = 0;
        const cJSON* sf;
        cJSON_ArrayForEach(sf, sf_arr) {
            if (++sf_count > NM_SUPPORTED_FN_MAX) return SPINE_ERR_PARSE;
            if (!seq_keys_in_whitelist(sf, SUPP_FN_KEYS, WL_N(SUPP_FN_KEYS)))
                return SPINE_ERR_PARSE;
            const cJSON* po = seq_get(sf, "possibleOperations");
            if (po) {
                if (!seq_keys_in_whitelist(po, POSS_OPS_KEYS,
                                            WL_N(POSS_OPS_KEYS)))
                    return SPINE_ERR_PARSE;
                const cJSON* rd = seq_get(po, "read");
                if (rd && cJSON_IsArray(rd) &&
                    !seq_keys_in_whitelist(rd, RD_WR_KEYS, WL_N(RD_WR_KEYS)))
                    return SPINE_ERR_PARSE;
                const cJSON* wr = seq_get(po, "write");
                if (wr && cJSON_IsArray(wr) &&
                    !seq_keys_in_whitelist(wr, RD_WR_KEYS, WL_N(RD_WR_KEYS)))
                    return SPINE_ERR_PARSE;
            }
        }
    }
    return SPINE_OK;
}

static spine_err_t validate_detailed_discovery_strict(const cJSON* fn_data) {
    if (!fn_data || !cJSON_IsObject(fn_data)) return SPINE_ERR_PARSE;
    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "nodeManagementDetailedDiscoveryData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    /* Top-level keys must all be in the whitelist */
    if (!seq_keys_in_whitelist(fn_seq, TOP_KEYS, WL_N(TOP_KEYS)))
        return SPINE_ERR_PARSE;

    /* specificationVersionList: must exist with >=1 valid X.Y.Z, major == 1.
     * specificationVersion is unbounded -> its value is a string array. */
    const cJSON* svl = seq_get(fn_seq, "specificationVersionList");
    if (!svl ||
        !seq_keys_in_whitelist(svl, SPEC_LIST_KEYS, WL_N(SPEC_LIST_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* sv_arr = seq_get(svl, "specificationVersion");
    if (!cJSON_IsArray(sv_arr)) return SPINE_ERR_PARSE;
    bool spec_ok = false;
    const cJSON* sv;
    cJSON_ArrayForEach(sv, sv_arr) {
        if (!cJSON_IsString(sv) || !sv->valuestring) return SPINE_ERR_PARSE;
        unsigned mj = 0;
        if (sscanf(sv->valuestring, "%u.", &mj) < 1) return SPINE_ERR_PARSE;
        if (mj != 1) return SPINE_ERR_PARSE;
        spec_ok = true;
    }
    if (!spec_ok) return SPINE_ERR_PARSE;

    /* deviceInformation: maxOccurs=1; deviceAddress.device required */
    const cJSON* di = seq_get(fn_seq, "deviceInformation");
    if (!di ||
        !seq_keys_in_whitelist(di, DEV_INFO_KEYS, WL_N(DEV_INFO_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* ddesc = seq_get(di, "description");
    if (!ddesc ||
        !seq_keys_in_whitelist(ddesc, DEV_DESC_KEYS, WL_N(DEV_DESC_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* da = seq_get(ddesc, "deviceAddress");
    if (!da ||
        !seq_keys_in_whitelist(da, DEV_ADDR_KEYS, WL_N(DEV_ADDR_KEYS)))
        return SPINE_ERR_PARSE;
    const cJSON* dv = seq_get(da, "device");
    if (!cJSON_IsString(dv) || !dv->valuestring || !dv->valuestring[0])
        return SPINE_ERR_PARSE;

    /* entityInformation: unbounded -> outer array of instances; >=1, each
     * well-formed, within SDK capacity. */
    const cJSON* ei_arr = seq_get(fn_seq, "entityInformation");
    if (!ei_arr || !cJSON_IsArray(ei_arr)) return SPINE_ERR_PARSE;
    uint8_t ent_n = (uint8_t)cJSON_GetArraySize(ei_arr);
    if (ent_n < 1 || ent_n > NM_ENTITY_INFO_MAX) return SPINE_ERR_PARSE;
    const cJSON* ei;
    cJSON_ArrayForEach(ei, ei_arr) {
        if (validate_entity_block(ei) != SPINE_OK) return SPINE_ERR_PARSE;
    }

    /* featureInformation: same unbounded pattern; optional per schema. */
    const cJSON* fi_arr = seq_get(fn_seq, "featureInformation");
    if (fi_arr) {
        if (!cJSON_IsArray(fi_arr)) return SPINE_ERR_PARSE;
        uint8_t feat_n = (uint8_t)cJSON_GetArraySize(fi_arr);
        if (feat_n > NM_FEATURE_INFO_MAX) return SPINE_ERR_PARSE;
        const cJSON* fi;
        cJSON_ArrayForEach(fi, fi_arr) {
            if (validate_feature_block(fi) != SPINE_OK) return SPINE_ERR_PARSE;
        }
    }

    return SPINE_OK;
}

/* ── nm_discovery_data decode ─────────────────────────────────────────────── */

spine_err_t nm_discovery_data_decode(const cJSON* fn_data,
                                      nm_discovery_data_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    /* Strict XSD-conformance check before any decoding (conformance:
     * any unknown field aborts further NM interaction). */
    if (validate_detailed_discovery_strict(fn_data) != SPINE_OK)
        return SPINE_ERR_PARSE;

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "nodeManagementDetailedDiscoveryData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    /* specificationVersionList */
    const cJSON* svl_seq = seq_get(fn_seq, "specificationVersionList");
    if (svl_seq && cJSON_IsArray(svl_seq)) {
        const cJSON* sv_arr = seq_get(svl_seq, "specificationVersion");
        if (cJSON_IsArray(sv_arr)) {
            const cJSON* sv;
            cJSON_ArrayForEach(sv, sv_arr) {
                if (out->spec_version_count >= 4) break;
                if (cJSON_IsString(sv) && sv->valuestring)
                    snprintf(out->spec_versions[out->spec_version_count++],
                             SPINE_UC_VERSION_MAX, "%s", sv->valuestring);
            }
        }
    }

    /* deviceInformation */
    const cJSON* di_seq = seq_get(fn_seq, "deviceInformation");
    if (di_seq && cJSON_IsArray(di_seq)) {
        const cJSON* desc_seq = seq_get(di_seq, "description");
        if (desc_seq && cJSON_IsArray(desc_seq)) {
            const cJSON* da_arr = seq_get(desc_seq, "deviceAddress");
            if (da_arr && cJSON_IsArray(da_arr)) {
                const cJSON* dev = seq_get(da_arr, "device");
                if (cJSON_IsString(dev) && dev->valuestring)
                    snprintf(out->device_address, sizeof(out->device_address),
                             "%s", dev->valuestring);
            }
            const cJSON* dt = seq_get(desc_seq, "deviceType");
            if (cJSON_IsString(dt) && dt->valuestring)
                snprintf(out->device_type, sizeof(out->device_type),
                         "%s", dt->valuestring);
            const cJSON* nfs = seq_get(desc_seq, "networkFeatureSet");
            if (cJSON_IsString(nfs) && nfs->valuestring)
                snprintf(out->network_feature_set,
                         sizeof(out->network_feature_set),
                         "%s", nfs->valuestring);
            const cJSON* dlbl = seq_get(desc_seq, "label");
            if (cJSON_IsString(dlbl) && dlbl->valuestring) {
                snprintf(out->device_label, sizeof(out->device_label),
                         "%s", dlbl->valuestring);
                out->device_label_present = true;
            }
            const cJSON* ddsc = seq_get(desc_seq, "description");
            if (cJSON_IsString(ddsc) && ddsc->valuestring) {
                snprintf(out->device_description, sizeof(out->device_description),
                         "%s", ddsc->valuestring);
                out->device_description_present = true;
            }
            const cJSON* dlsc = seq_get(desc_seq, "lastStateChange");
            if (cJSON_IsString(dlsc))
                out->device_last_state_change = nm_parse_state_change(dlsc->valuestring);
        }
    }

    /* entityInformation */
    const cJSON* ei_arr = seq_get(fn_seq, "entityInformation");
    if (cJSON_IsArray(ei_arr)) {
        const cJSON* ei_item;
        cJSON_ArrayForEach(ei_item, ei_arr) {
            if (out->entity_count >= NM_ENTITY_INFO_MAX) break;
            if (!cJSON_IsArray(ei_item)) continue;
            const cJSON* desc_seq = seq_get(ei_item, "description");
            if (!cJSON_IsArray(desc_seq)) continue;
            nm_entity_info_t* e = &out->entities[out->entity_count];
            memset(e, 0, sizeof(*e));
            const cJSON* ea = seq_get(desc_seq, "entityAddress");
            if (ea) decode_nm_addr(ea, &e->entity_addr);
            const cJSON* et = seq_get(desc_seq, "entityType");
            if (cJSON_IsString(et) && et->valuestring)
                snprintf(e->entity_type, sizeof(e->entity_type),
                         "%s", et->valuestring);
            const cJSON* lsc = seq_get(desc_seq, "lastStateChange");
            if (cJSON_IsString(lsc))
                e->last_state_change = nm_parse_state_change(lsc->valuestring);
            const cJSON* lbl = seq_get(desc_seq, "label");
            if (cJSON_IsString(lbl) && lbl->valuestring) {
                snprintf(e->label, sizeof(e->label), "%s", lbl->valuestring);
                e->label_present = true;
            }
            const cJSON* edsc = seq_get(desc_seq, "description");
            if (cJSON_IsString(edsc) && edsc->valuestring) {
                snprintf(e->description, sizeof(e->description), "%s", edsc->valuestring);
                e->description_present = true;
            }
            out->entity_count++;
        }
    }

    /* featureInformation */
    const cJSON* fi_arr = seq_get(fn_seq, "featureInformation");
    if (cJSON_IsArray(fi_arr)) {
        const cJSON* fi_item;
        cJSON_ArrayForEach(fi_item, fi_arr) {
            if (out->feature_count >= NM_FEATURE_INFO_MAX) break;
            if (!cJSON_IsArray(fi_item)) continue;
            const cJSON* desc_seq = seq_get(fi_item, "description");
            if (!cJSON_IsArray(desc_seq)) continue;
            nm_feature_info_t* f = &out->features[out->feature_count];
            memset(f, 0, sizeof(*f));
            const cJSON* fa = seq_get(desc_seq, "featureAddress");
            if (fa) decode_nm_addr(fa, &f->feature_addr);
            const cJSON* ft = seq_get(desc_seq, "featureType");
            if (cJSON_IsString(ft) && ft->valuestring)
                snprintf(f->feature_type, sizeof(f->feature_type),
                         "%s", ft->valuestring);
            const cJSON* role = seq_get(desc_seq, "role");
            if (cJSON_IsString(role) && role->valuestring)
                snprintf(f->role, sizeof(f->role), "%s", role->valuestring);
            const cJSON* lsc = seq_get(desc_seq, "lastStateChange");
            if (cJSON_IsString(lsc))
                f->last_state_change = nm_parse_state_change(lsc->valuestring);
            const cJSON* flbl = seq_get(desc_seq, "label");
            if (cJSON_IsString(flbl) && flbl->valuestring) {
                snprintf(f->label, sizeof(f->label), "%s", flbl->valuestring);
                f->label_present = true;
            }
            const cJSON* fdsc = seq_get(desc_seq, "description");
            if (cJSON_IsString(fdsc) && fdsc->valuestring) {
                snprintf(f->description, sizeof(f->description), "%s", fdsc->valuestring);
                f->description_present = true;
            }

            const cJSON* sf_arr = seq_get(desc_seq, "supportedFunction");
            if (cJSON_IsArray(sf_arr)) {
                const cJSON* sf_item;
                cJSON_ArrayForEach(sf_item, sf_arr) {
                    if (f->supported_fn_count >= NM_SUPPORTED_FN_MAX) break;
                    if (!cJSON_IsArray(sf_item)) continue;
                    nm_supported_fn_t* fn_e = &f->supported_fns[f->supported_fn_count];
                    memset(fn_e, 0, sizeof(*fn_e));
                    const cJSON* fn_n = seq_get(sf_item, "function");
                    if (cJSON_IsString(fn_n) && fn_n->valuestring)
                        snprintf(fn_e->function_name, sizeof(fn_e->function_name),
                                 "%s", fn_n->valuestring);
                    const cJSON* po = seq_get(sf_item, "possibleOperations");
                    if (po && cJSON_IsArray(po)) {
                        const cJSON* rd = seq_get(po, "read");
                        if (rd) {
                            fn_e->ops.read_present = true;
                            if (cJSON_IsArray(rd) && seq_get(rd, "partial"))
                                fn_e->ops.read_partial = true;
                        }
                        const cJSON* wr = seq_get(po, "write");
                        if (wr) {
                            fn_e->ops.write_present = true;
                            if (cJSON_IsArray(wr) && seq_get(wr, "partial"))
                                fn_e->ops.write_partial = true;
                        }
                    }
                    f->supported_fn_count++;
                }
            }
            out->feature_count++;
        }
    }
    return SPINE_OK;
}

/* ── nm_discovery_data encode ─────────────────────────────────────────────── */

cJSON* nm_discovery_data_encode(const nm_discovery_data_t* in) {
    if (!in) return NULL;

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) return NULL;

    /* specificationVersionList */
    {
        cJSON* sv_arr = cJSON_CreateArray();
        if (!sv_arr) { cJSON_Delete(fn_seq); return NULL; }
        uint8_t cnt = in->spec_version_count ? in->spec_version_count : 1;
        for (uint8_t i = 0; i < cnt; i++) {
            const char* ver = (in->spec_version_count > 0)
                              ? in->spec_versions[i] : SPINE_SPEC_VERSION_STR;
            cJSON_AddItemToArray(sv_arr, cJSON_CreateString(ver));
        }
        cJSON* svl_seq = cJSON_CreateArray();
        if (!svl_seq) { cJSON_Delete(sv_arr); cJSON_Delete(fn_seq); return NULL; }
        if (!seq_add_arr(svl_seq, "specificationVersion", sv_arr))
            { cJSON_Delete(svl_seq); cJSON_Delete(fn_seq); return NULL; }
        if (!seq_add_arr(fn_seq, "specificationVersionList", svl_seq))
            { cJSON_Delete(fn_seq); return NULL; }
    }

    /* deviceInformation */
    {
        cJSON* desc_seq = cJSON_CreateArray();
        if (!desc_seq) { cJSON_Delete(fn_seq); return NULL; }

        cJSON* da_arr = cJSON_CreateArray();
        if (!da_arr) { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        const char* dev_str = in->device_address[0]
                              ? in->device_address : "d:_i:0_unknown";
        cJSON* dev_obj = cJSON_CreateObject();
        if (!dev_obj) { cJSON_Delete(da_arr); cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        cJSON_AddStringToObject(dev_obj, "device", dev_str);
        cJSON_AddItemToArray(da_arr, dev_obj);
        if (!seq_add_arr(desc_seq, "deviceAddress", da_arr))
            { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }

        if (in->device_type[0])
            if (!seq_add_str(desc_seq, "deviceType", in->device_type))
                { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }

        /* networkFeatureSet (M per SPINE §7.1.1.5.3), default "simple" */
        {
            const char* nfs = in->network_feature_set[0]
                              ? in->network_feature_set : "simple";
            if (!seq_add_str(desc_seq, "networkFeatureSet", nfs))
                { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        }
        {
            const char* lsc = nm_state_change_str(in->device_last_state_change);
            if (lsc)
                if (!seq_add_str(desc_seq, "lastStateChange", lsc))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        }
        if (in->device_label_present)
            if (!seq_add_str(desc_seq, "label", in->device_label))
                { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        if (in->device_description_present)
            if (!seq_add_str(desc_seq, "description", in->device_description))
                { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }

        cJSON* di_seq = cJSON_CreateArray();
        if (!di_seq) { cJSON_Delete(desc_seq); cJSON_Delete(fn_seq); return NULL; }
        if (!seq_add_arr(di_seq, "description", desc_seq))
            { cJSON_Delete(di_seq); cJSON_Delete(fn_seq); return NULL; }

        if (!seq_add_arr(fn_seq, "deviceInformation", di_seq))
            { cJSON_Delete(fn_seq); return NULL; }
    }

    /* entityInformation */
    {
        cJSON* ei_list = cJSON_CreateArray();
        if (!ei_list) { cJSON_Delete(fn_seq); return NULL; }
        for (uint8_t i = 0; i < in->entity_count; i++) {
            const nm_entity_info_t* e = &in->entities[i];
            cJSON* desc_seq = cJSON_CreateArray();
            if (!desc_seq) { cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }

            cJSON* ea = nm_addr_encode_local(&e->entity_addr, false, false);
            if (!ea || !seq_add_arr(desc_seq, "entityAddress", ea))
                { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            if (e->entity_type[0])
                if (!seq_add_str(desc_seq, "entityType", e->entity_type))
                    { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            const char* lsc = nm_state_change_str(e->last_state_change);
            if (lsc)
                if (!seq_add_str(desc_seq, "lastStateChange", lsc))
                    { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            if (e->label_present)
                if (!seq_add_str(desc_seq, "label", e->label))
                    { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            if (e->description_present)
                if (!seq_add_str(desc_seq, "description", e->description))
                    { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }

            cJSON* ei_item = cJSON_CreateArray();
            if (!ei_item) { cJSON_Delete(desc_seq); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            if (!seq_add_arr(ei_item, "description", desc_seq))
                { cJSON_Delete(ei_item); cJSON_Delete(ei_list); cJSON_Delete(fn_seq); return NULL; }
            cJSON_AddItemToArray(ei_list, ei_item);
        }
        if (!seq_add_arr(fn_seq, "entityInformation", ei_list))
            { cJSON_Delete(fn_seq); return NULL; }
    }

    /* featureInformation */
    {
        cJSON* fi_list = cJSON_CreateArray();
        if (!fi_list) { cJSON_Delete(fn_seq); return NULL; }
        for (uint8_t i = 0; i < in->feature_count; i++) {
            const nm_feature_info_t* f = &in->features[i];
            cJSON* desc_seq = cJSON_CreateArray();
            if (!desc_seq) { cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }

            cJSON* fa = nm_addr_encode_local(&f->feature_addr, false, true);
            if (!fa || !seq_add_arr(desc_seq, "featureAddress", fa))
                { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            if (f->feature_type[0])
                if (!seq_add_str(desc_seq, "featureType", f->feature_type))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            if (f->role[0])
                if (!seq_add_str(desc_seq, "role", f->role))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }

            /* supportedFunction — must come after role, before lastStateChange per XSD */
            if (f->supported_fn_count > 0) {
                cJSON* sf_list = cJSON_CreateArray();
                if (!sf_list) { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                for (uint8_t j = 0; j < f->supported_fn_count; j++) {
                    const nm_supported_fn_t* fn_e = &f->supported_fns[j];
                    cJSON* sf_item = cJSON_CreateArray();
                    if (!sf_item) { cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    if (!seq_add_str(sf_item, "function", fn_e->function_name))
                        { cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    cJSON* po_seq = cJSON_CreateArray();
                    if (!po_seq) { cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    if (fn_e->ops.read_present) {
                        cJSON* rd = cJSON_CreateArray();
                        if (!rd) { cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                        if (fn_e->ops.read_partial) {
                            cJSON* p = cJSON_CreateObject();
                            if (!p) { cJSON_Delete(rd); cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                            cJSON_AddItemToObject(p, "partial", cJSON_CreateArray());
                            cJSON_AddItemToArray(rd, p);
                        }
                        if (!seq_add_arr(po_seq, "read", rd))
                            { cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    }
                    if (fn_e->ops.write_present) {
                        cJSON* wr = cJSON_CreateArray();
                        if (!wr) { cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                        if (fn_e->ops.write_partial) {
                            cJSON* p = cJSON_CreateObject();
                            if (!p) { cJSON_Delete(wr); cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                            cJSON_AddItemToObject(p, "partial", cJSON_CreateArray());
                            cJSON_AddItemToArray(wr, p);
                        }
                        if (!seq_add_arr(po_seq, "write", wr))
                            { cJSON_Delete(po_seq); cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    }
                    if (!seq_add_arr(sf_item, "possibleOperations", po_seq))
                        { cJSON_Delete(sf_item); cJSON_Delete(sf_list); cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
                    cJSON_AddItemToArray(sf_list, sf_item);
                }
                if (!seq_add_arr(desc_seq, "supportedFunction", sf_list))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            }
            const char* lsc = nm_state_change_str(f->last_state_change);
            if (lsc)
                if (!seq_add_str(desc_seq, "lastStateChange", lsc))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            if (f->label_present)
                if (!seq_add_str(desc_seq, "label", f->label))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            if (f->description_present)
                if (!seq_add_str(desc_seq, "description", f->description))
                    { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }

            cJSON* fi_item = cJSON_CreateArray();
            if (!fi_item) { cJSON_Delete(desc_seq); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            if (!seq_add_arr(fi_item, "description", desc_seq))
                { cJSON_Delete(fi_item); cJSON_Delete(fi_list); cJSON_Delete(fn_seq); return NULL; }
            cJSON_AddItemToArray(fi_list, fi_item);
        }
        if (!seq_add_arr(fn_seq, "featureInformation", fi_list))
            { cJSON_Delete(fn_seq); return NULL; }
    }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "nodeManagementDetailedDiscoveryData", fn_seq);
    return fn_data;
}

/* ── nm_uc_data decode ────────────────────────────────────────────────────── */
/*
 * nm_uc_data_t: items[NM_UC_INFO_MAX], count
 * nm_uc_info_t: address, actor, uc_supports[NM_UC_SUPPORT_MAX], uc_support_count
 * nm_uc_support_t: use_case_name, use_case_version, scenario_support[], scenario_count
 */

spine_err_t nm_uc_data_decode(const cJSON* fn_data, nm_uc_data_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "nodeManagementUseCaseData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ui_arr = seq_get(fn_seq, "useCaseInformation");
    if (!cJSON_IsArray(ui_arr)) return SPINE_OK;

    const cJSON* ui_item;
    cJSON_ArrayForEach(ui_item, ui_arr) {
        if (out->count >= NM_UC_INFO_MAX) break;
        if (!cJSON_IsArray(ui_item)) continue;
        nm_uc_info_t* info = &out->items[out->count];
        memset(info, 0, sizeof(*info));

        const cJSON* addr = seq_get(ui_item, "address");
        if (addr) decode_nm_addr(addr, &info->address);

        const cJSON* actor = seq_get(ui_item, "actor");
        if (cJSON_IsString(actor) && actor->valuestring)
            snprintf(info->actor, sizeof(info->actor), "%s", actor->valuestring);

        const cJSON* ucs_arr = seq_get(ui_item, "useCaseSupport");
        if (cJSON_IsArray(ucs_arr)) {
            const cJSON* ucs_item;
            cJSON_ArrayForEach(ucs_item, ucs_arr) {
                if (info->uc_support_count >= NM_UC_SUPPORT_MAX) break;
                if (!cJSON_IsArray(ucs_item)) continue;
                nm_uc_support_t* s = &info->uc_supports[info->uc_support_count];
                memset(s, 0, sizeof(*s));

                const cJSON* ucn = seq_get(ucs_item, "useCaseName");
                if (cJSON_IsString(ucn) && ucn->valuestring)
                    snprintf(s->use_case_name, sizeof(s->use_case_name),
                             "%s", ucn->valuestring);
                const cJSON* ucv = seq_get(ucs_item, "useCaseVersion");
                if (cJSON_IsString(ucv) && ucv->valuestring)
                    snprintf(s->use_case_version, sizeof(s->use_case_version),
                             "%s", ucv->valuestring);
                const cJSON* uca = seq_get(ucs_item, "useCaseAvailable");
                s->use_case_available = cJSON_IsBool(uca) ? cJSON_IsTrue(uca) : true;

                const cJSON* ucr = seq_get(ucs_item, "useCaseDocumentSubRevision");
                if (cJSON_IsString(ucr) && ucr->valuestring)
                    snprintf(s->use_case_doc_sub_revision,
                             sizeof(s->use_case_doc_sub_revision),
                             "%s", ucr->valuestring);

                const cJSON* sc_arr = seq_get(ucs_item, "scenarioSupport");
                if (cJSON_IsArray(sc_arr)) {
                    const cJSON* sc;
                    cJSON_ArrayForEach(sc, sc_arr) {
                        if (s->scenario_count >= NM_SCENARIO_SUPPORT_MAX) break;
                        if (cJSON_IsNumber(sc))
                            s->scenario_support[s->scenario_count++] =
                                (uint8_t)sc->valuedouble;
                    }
                }
                info->uc_support_count++;
            }
        }
        out->count++;
    }
    return SPINE_OK;
}

/* ── nm_uc_data encode ────────────────────────────────────────────────────── */

cJSON* nm_uc_data_encode(const nm_uc_data_t* in) {
    if (!in) return NULL;

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) return NULL;

    cJSON* ui_list = cJSON_CreateArray();
    if (!ui_list) { cJSON_Delete(fn_seq); return NULL; }

    for (uint8_t i = 0; i < in->count; i++) {
        const nm_uc_info_t* info = &in->items[i];
        cJSON* ui_item = cJSON_CreateArray();
        if (!ui_item) { cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }

        cJSON* addr_arr = nm_addr_encode_local(&info->address, true, false);
        if (!addr_arr || !seq_add_arr(ui_item, "address", addr_arr))
            { cJSON_Delete(ui_item); cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }

        if (info->actor[0])
            if (!seq_add_str(ui_item, "actor", info->actor))
                { cJSON_Delete(ui_item); cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }

        if (info->uc_support_count > 0) {
            cJSON* ucs_list = cJSON_CreateArray();
            if (!ucs_list) { cJSON_Delete(ui_item); cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }
            for (uint8_t j = 0; j < info->uc_support_count; j++) {
                const nm_uc_support_t* s = &info->uc_supports[j];
                cJSON* ucs_item = cJSON_CreateArray();
                if (!ucs_item) { cJSON_Delete(ucs_list); cJSON_Delete(ui_item); cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }

                if (!seq_add_str(ucs_item, "useCaseName", s->use_case_name))
                    goto fail_ucs;
                if (!seq_add_str(ucs_item, "useCaseVersion", s->use_case_version))
                    goto fail_ucs;
                {
                    cJSON* avail_obj = cJSON_CreateObject();
                    if (!avail_obj) goto fail_ucs;
                    cJSON_AddBoolToObject(avail_obj, "useCaseAvailable",
                                          s->use_case_available);
                    cJSON_AddItemToArray(ucs_item, avail_obj);
                }
                if (s->scenario_count > 0) {
                    cJSON* sc_arr = cJSON_CreateArray();
                    if (!sc_arr) goto fail_ucs;
                    for (uint8_t k = 0; k < s->scenario_count; k++)
                        cJSON_AddItemToArray(sc_arr,
                            cJSON_CreateNumber(s->scenario_support[k]));
                    if (!seq_add_arr(ucs_item, "scenarioSupport", sc_arr))
                        goto fail_ucs;
                }
                /* useCaseDocumentSubRevision (M for SPINE >= 1.3.0) */
                if (s->use_case_doc_sub_revision[0])
                    if (!seq_add_str(ucs_item, "useCaseDocumentSubRevision",
                                     s->use_case_doc_sub_revision))
                        goto fail_ucs;
                cJSON_AddItemToArray(ucs_list, ucs_item);
                continue;
fail_ucs:
                cJSON_Delete(ucs_item);
                cJSON_Delete(ucs_list);
                cJSON_Delete(ui_item);
                cJSON_Delete(ui_list);
                cJSON_Delete(fn_seq);
                return NULL;
            }
            if (!seq_add_arr(ui_item, "useCaseSupport", ucs_list))
                { cJSON_Delete(ui_item); cJSON_Delete(ui_list); cJSON_Delete(fn_seq); return NULL; }
        }

        cJSON_AddItemToArray(ui_list, ui_item);
    }

    if (!seq_add_arr(fn_seq, "useCaseInformation", ui_list))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, "nodeManagementUseCaseData", fn_seq);
    return fn_data;
}

/* ══════════════════════════════════════════════════════════════════════════
 * nodeManagementSubscriptionData / nodeManagementBindingData codec
 * ══════════════════════════════════════════════════════════════════════════ */

static bool sb_add_num(cJSON* a, const char* k, double v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddNumberToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}

/* Encode a single entry as an xs:sequence array.
 * Order per XSD: id → clientAddress → serverAddress */
static cJSON* encode_sb_entry(const nm_sb_entry_t* e, const char* id_key) {
    cJSON* entry_seq = cJSON_CreateArray();
    if (!entry_seq) return NULL;

    if (!sb_add_num(entry_seq, id_key, (double)e->id))
        { cJSON_Delete(entry_seq); return NULL; }

    cJSON* ca = nm_addr_encode_local(&e->client_addr, true, true);
    if (!ca || !seq_add_arr(entry_seq, "clientAddress", ca))
        { cJSON_Delete(entry_seq); return NULL; }

    cJSON* sa = nm_addr_encode_local(&e->server_addr, true, true);
    if (!sa || !seq_add_arr(entry_seq, "serverAddress", sa))
        { cJSON_Delete(entry_seq); return NULL; }

    return entry_seq;
}

/* Decode a single entry from xs:sequence array */
static void decode_sb_entry(const cJSON* entry_seq, const char* id_key,
                             nm_sb_entry_t* out) {
    memset(out, 0, sizeof(*out));
    const cJSON* id = seq_get(entry_seq, id_key);
    if (cJSON_IsNumber(id)) out->id = (uint32_t)id->valuedouble;
    const cJSON* ca = seq_get(entry_seq, "clientAddress");
    if (ca) decode_nm_addr(ca, &out->client_addr);
    const cJSON* sa = seq_get(entry_seq, "serverAddress");
    if (sa) decode_nm_addr(sa, &out->server_addr);
}

/* Generic list encode: wraps entries in {fn_name: [{entry_key: [entries]}]} */
static cJSON* encode_sb_list(const nm_sb_entry_t* items, uint16_t count,
                              const char* fn_name, const char* entry_key,
                              const char* id_key) {
    cJSON* entry_list = cJSON_CreateArray();
    if (!entry_list) return NULL;

    for (uint16_t i = 0; i < count; i++) {
        cJSON* entry = encode_sb_entry(&items[i], id_key);
        if (!entry) { cJSON_Delete(entry_list); return NULL; }
        cJSON_AddItemToArray(entry_list, entry);
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(entry_list); return NULL; }
    if (!seq_add_arr(fn_seq, entry_key, entry_list))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data, fn_name, fn_seq);
    return fn_data;
}

/* Generic list decode */
static spine_err_t decode_sb_list(const cJSON* fn_data,
                                    const char* fn_name, const char* entry_key,
                                    const char* id_key,
                                    nm_sb_entry_t* items, uint16_t max,
                                    uint16_t* count_out) {
    *count_out = 0;
    if (!fn_data) return SPINE_ERR_INVALID;
    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data, fn_name);
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;
    const cJSON* entry_list = seq_get(fn_seq, entry_key);
    if (!cJSON_IsArray(entry_list)) return SPINE_OK;

    const cJSON* entry;
    cJSON_ArrayForEach(entry, entry_list) {
        if (*count_out >= max) break;
        decode_sb_entry(entry, id_key, &items[*count_out]);
        (*count_out)++;
    }
    return SPINE_OK;
}

cJSON* nm_sub_data_encode(const nm_sub_data_t* in) {
    if (!in) return NULL;
    return encode_sb_list(in->items, in->count,
                           "nodeManagementSubscriptionData",
                           "subscriptionEntry", "subscriptionId");
}

spine_err_t nm_sub_data_decode(const cJSON* fn_data, nm_sub_data_t* out) {
    if (!out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    return decode_sb_list(fn_data,
                           "nodeManagementSubscriptionData",
                           "subscriptionEntry", "subscriptionId",
                           out->items, NM_SB_ENTRY_MAX, &out->count);
}

cJSON* nm_bind_data_encode(const nm_bind_data_t* in) {
    if (!in) return NULL;
    return encode_sb_list(in->items, in->count,
                           "nodeManagementBindingData",
                           "bindingEntry", "bindingId");
}

spine_err_t nm_bind_data_decode(const cJSON* fn_data, nm_bind_data_t* out) {
    if (!out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    return decode_sb_list(fn_data,
                           "nodeManagementBindingData",
                           "bindingEntry", "bindingId",
                           out->items, NM_SB_ENTRY_MAX, &out->count);
}
