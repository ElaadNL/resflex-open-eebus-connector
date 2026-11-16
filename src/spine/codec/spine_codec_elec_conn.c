// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_elec_conn.c — ElectricalConnection codec (Array format)
 */

#include "spine_codec_elec_conn.h"
#include "spine_codec_load_ctrl.h"
#include <string.h>
#include <stdio.h>

static bool seq_add_str(cJSON* a, const char* k, const char* v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddStringToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
}
static bool seq_add_num(cJSON* a, const char* k, double v) {
    cJSON* o = cJSON_CreateObject(); if (!o) return false;
    cJSON_AddNumberToObject(o, k, v); cJSON_AddItemToArray(a, o); return true;
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

/* ── ElectricalConnectionParameterDescriptionListData ────────────────────── */

cJSON* ec_param_desc_list_encode(const ec_param_desc_list_t* in) {
    if (!in) return NULL;
    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const ec_param_desc_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "electricalConnectionId",
                         (double)d->electrical_connection_id)) goto oom;
        if (!seq_add_num(item_seq, "parameterId",
                         (double)d->parameter_id)) goto oom;
        if (d->measurement_id_present)
            if (!seq_add_num(item_seq, "measurementId",
                             (double)d->measurement_id)) goto oom;
        if (d->voltage_type_present)
            if (!seq_add_str(item_seq, "voltageType", d->voltage_type)) goto oom;
        if (d->ac_measured_phases_present)
            if (!seq_add_str(item_seq, "acMeasuredPhases",
                             d->ac_measured_phases)) goto oom;
        if (d->ac_measured_in_ref_to_present)
            if (!seq_add_str(item_seq, "acMeasuredInReferenceTo",
                             d->ac_measured_in_ref_to)) goto oom;
        if (d->ac_measurement_type_present)
            if (!seq_add_str(item_seq, "acMeasurementType",
                             d->ac_measurement_type)) goto oom;
        if (d->ac_measurement_variant_present)
            if (!seq_add_str(item_seq, "acMeasurementVariant",
                             d->ac_measurement_variant)) goto oom;
        if (d->ac_measured_harmonic_present)
            if (!seq_add_num(item_seq, "acMeasuredHarmonic",
                             (double)d->ac_measured_harmonic)) goto oom;
        if (d->scope_type_present)
            if (!seq_add_str(item_seq, "scopeType", d->scope_type)) goto oom;
        if (d->label_present)
            if (!seq_add_str(item_seq, "label", d->label)) goto oom;
        if (d->description_present)
            if (!seq_add_str(item_seq, "description", d->description)) goto oom;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "electricalConnectionParameterDescriptionData",
                     list_arr)) { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data,
        "electricalConnectionParameterDescriptionListData", fn_seq);
    return fn_data;
}

spine_err_t ec_param_desc_list_decode(const cJSON* fn_data,
                                      ec_param_desc_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data,
        "electricalConnectionParameterDescriptionListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq,
        "electricalConnectionParameterDescriptionData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_EC_PARAM_DESC_MAX) break;
        ec_param_desc_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* v;
#define GET_NUM(jk, df) \
    v = seq_get(item_seq, jk); if (cJSON_IsNumber(v)) d->df = (uint32_t)v->valuedouble;
#define GET_STR(jk, df, pf) \
    v = seq_get(item_seq, jk); \
    if (cJSON_IsString(v) && v->valuestring) { \
        snprintf(d->df, sizeof(d->df), "%s", v->valuestring); d->pf = true; }

        GET_NUM("electricalConnectionId", electrical_connection_id);
        GET_NUM("parameterId",            parameter_id);
        v = seq_get(item_seq, "measurementId");
        if (cJSON_IsNumber(v)) {
            d->measurement_id = (uint32_t)v->valuedouble;
            d->measurement_id_present = true;
        }
        GET_STR("voltageType",             voltage_type,             voltage_type_present);
        GET_STR("acMeasuredPhases",        ac_measured_phases,        ac_measured_phases_present);
        GET_STR("acMeasuredInReferenceTo", ac_measured_in_ref_to,     ac_measured_in_ref_to_present);
        GET_STR("acMeasurementType",       ac_measurement_type,       ac_measurement_type_present);
        GET_STR("acMeasurementVariant",    ac_measurement_variant,    ac_measurement_variant_present);
        v = seq_get(item_seq, "acMeasuredHarmonic");
        if (cJSON_IsNumber(v)) {
            d->ac_measured_harmonic = (uint8_t)v->valuedouble;
            d->ac_measured_harmonic_present = true;
        }
        GET_STR("scopeType", scope_type, scope_type_present);
        GET_STR("label", label, label_present);
        GET_STR("description", description, description_present);
#undef GET_NUM
#undef GET_STR
        out->count++;
    }
    return SPINE_OK;
}

/* ── ElectricalConnectionPermittedValueSetListData ───────────────────────── */
/*
 * XSD: permittedValueSet is ScaledNumberSetType (xs:sequence):
 *   [{"value": [scaled_num_seq]}, ...  {"range": [min_seq, max_seq]}, ...]
 * value is maxOccurs=unbounded -> Array of scaled_num_seq
 * range is maxOccurs=unbounded -> Array of range_seq
 */

cJSON* ec_permitted_set_list_encode(const ec_permitted_set_list_t* in) {
    if (!in) return NULL;
    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const ec_permitted_set_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "electricalConnectionId",
                         (double)d->electrical_connection_id)) goto oom2;
        if (!seq_add_num(item_seq, "parameterId",
                         (double)d->parameter_id)) goto oom2;

        if (d->value_count > 0 || d->range_count > 0) {
            /* permittedValueSet xs:sequence → Array */
            cJSON* pvs_seq = cJSON_CreateArray();
            if (!pvs_seq) goto oom2;

            /* value items (maxOccurs=unbounded → outer array) */
            if (d->value_count > 0) {
                cJSON* val_list = cJSON_CreateArray();
                if (!val_list) { cJSON_Delete(pvs_seq); goto oom2; }
                for (uint8_t j = 0; j < d->value_count; j++) {
                    cJSON* sn = spine_scaled_number_encode(&d->values[j]);
                    if (!sn) { cJSON_Delete(val_list); cJSON_Delete(pvs_seq); goto oom2; }
                    cJSON_AddItemToArray(val_list, sn);
                }
                if (!seq_add_arr(pvs_seq, "value", val_list))
                    { cJSON_Delete(pvs_seq); goto oom2; }
            }

            /* range items (maxOccurs=unbounded → outer array) */
            if (d->range_count > 0) {
                cJSON* rng_list = cJSON_CreateArray();
                if (!rng_list) { cJSON_Delete(pvs_seq); goto oom2; }
                for (uint8_t j = 0; j < d->range_count; j++) {
                    /* ScaledNumberRangeType xs:sequence → Array */
                    cJSON* rng_seq = cJSON_CreateArray();
                    if (!rng_seq) { cJSON_Delete(rng_list); cJSON_Delete(pvs_seq); goto oom2; }
                    if (d->ranges[j].min_present) {
                        cJSON* mn = spine_scaled_number_encode(&d->ranges[j].min);
                        if (!mn || !seq_add_arr(rng_seq, "min", mn))
                            { cJSON_Delete(rng_seq); cJSON_Delete(rng_list); cJSON_Delete(pvs_seq); goto oom2; }
                    }
                    if (d->ranges[j].max_present) {
                        cJSON* mx = spine_scaled_number_encode(&d->ranges[j].max);
                        if (!mx || !seq_add_arr(rng_seq, "max", mx))
                            { cJSON_Delete(rng_seq); cJSON_Delete(rng_list); cJSON_Delete(pvs_seq); goto oom2; }
                    }
                    cJSON_AddItemToArray(rng_list, rng_seq);
                }
                if (!seq_add_arr(pvs_seq, "range", rng_list))
                    { cJSON_Delete(pvs_seq); goto oom2; }
            }

            if (!seq_add_arr(item_seq, "permittedValueSet", pvs_seq)) goto oom2;
        }

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom2:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "electricalConnectionPermittedValueSetData",
                     list_arr)) { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data,
        "electricalConnectionPermittedValueSetListData", fn_seq);
    return fn_data;
}

/* ── ElectricalConnectionCharacteristicListData ──────────────────────────── */

cJSON* ec_characteristic_list_encode(const ec_characteristic_list_t* in) {
    if (!in) return NULL;
    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const ec_characteristic_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "electricalConnectionId",
                         (double)d->electrical_connection_id)) goto oom3;
        if (!seq_add_num(item_seq, "parameterId",
                         (double)d->parameter_id)) goto oom3;
        if (!seq_add_num(item_seq, "characteristicId",
                         (double)d->characteristic_id)) goto oom3;
        if (d->characteristic_context_present)
            if (!seq_add_str(item_seq, "characteristicContext",
                             d->characteristic_context)) goto oom3;
        if (d->characteristic_type_present)
            if (!seq_add_str(item_seq, "characteristicType",
                             d->characteristic_type)) goto oom3;
        if (d->value_present) {
            cJSON* val = spine_scaled_number_encode(&d->value);
            if (!val || !seq_add_arr(item_seq, "value", val)) goto oom3;
        }
        if (d->unit_present)
            if (!seq_add_str(item_seq, "unit", d->unit)) goto oom3;

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom3:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "electricalConnectionCharacteristicData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data,
        "electricalConnectionCharacteristicListData", fn_seq);
    return fn_data;
}

spine_err_t ec_characteristic_list_decode(const cJSON* fn_data,
                                           ec_characteristic_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data,
        "electricalConnectionCharacteristicListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq,
        "electricalConnectionCharacteristicData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_EC_CHARACTERISTIC_MAX) break;
        ec_characteristic_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* v;
        v = seq_get(item_seq, "electricalConnectionId");
        if (cJSON_IsNumber(v)) d->electrical_connection_id = (uint32_t)v->valuedouble;
        v = seq_get(item_seq, "parameterId");
        if (cJSON_IsNumber(v)) d->parameter_id = (uint32_t)v->valuedouble;
        v = seq_get(item_seq, "characteristicId");
        if (cJSON_IsNumber(v)) d->characteristic_id = (uint32_t)v->valuedouble;

#define GET_STR_C(jk, df, pf) \
    v = seq_get(item_seq, jk); \
    if (cJSON_IsString(v) && v->valuestring) { \
        snprintf(d->df, sizeof(d->df), "%s", v->valuestring); d->pf = true; }

        GET_STR_C("characteristicContext", characteristic_context,
                   characteristic_context_present);
        GET_STR_C("characteristicType", characteristic_type,
                   characteristic_type_present);
        GET_STR_C("unit", unit, unit_present);
#undef GET_STR_C

        v = seq_get(item_seq, "value");
        if (v) {
            spine_scaled_number_decode(v, &d->value);
            d->value_present = true;
        }
        out->count++;
    }
    return SPINE_OK;
}

spine_err_t ec_permitted_set_list_decode(const cJSON* fn_data,
                                         ec_permitted_set_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(fn_data,
        "electricalConnectionPermittedValueSetListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq,
        "electricalConnectionPermittedValueSetData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_EC_PERMITTED_SET_MAX) break;
        ec_permitted_set_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* ecid = seq_get(item_seq, "electricalConnectionId");
        if (cJSON_IsNumber(ecid))
            d->electrical_connection_id = (uint32_t)ecid->valuedouble;
        const cJSON* pid = seq_get(item_seq, "parameterId");
        if (cJSON_IsNumber(pid)) d->parameter_id = (uint32_t)pid->valuedouble;

        const cJSON* pvs = seq_get(item_seq, "permittedValueSet");
        if (cJSON_IsArray(pvs)) {
            /* value list */
            const cJSON* val_list = seq_get(pvs, "value");
            if (cJSON_IsArray(val_list)) {
                const cJSON* sn;
                cJSON_ArrayForEach(sn, val_list) {
                    if (d->value_count >= SPINE_EC_PERMITTED_VALUE_MAX) break;
                    spine_scaled_number_decode(sn, &d->values[d->value_count++]);
                }
            }
            /* range list */
            const cJSON* rng_list = seq_get(pvs, "range");
            if (cJSON_IsArray(rng_list)) {
                const cJSON* rng_seq;
                cJSON_ArrayForEach(rng_seq, rng_list) {
                    if (d->range_count >= SPINE_EC_PERMITTED_RANGE_MAX) break;
                    ec_scaled_number_range_t* r = &d->ranges[d->range_count];
                    const cJSON* mn = seq_get(rng_seq, "min");
                    if (mn) {
                        spine_scaled_number_decode(mn, &r->min);
                        r->min_present = true;
                    }
                    const cJSON* mx = seq_get(rng_seq, "max");
                    if (mx) {
                        spine_scaled_number_decode(mx, &r->max);
                        r->max_present = true;
                    }
                    d->range_count++;
                }
            }
        }
        out->count++;
    }
    return SPINE_OK;
}

/* ── electricalConnectionDescriptionListData ─────────────────────────────── */

spine_err_t ec_description_list_decode(const cJSON* fn_data,
                                        ec_description_list_t* out) {
    if (!fn_data || !out) return SPINE_ERR_INVALID;
    memset(out, 0, sizeof(*out));

    const cJSON* fn_seq = cJSON_GetObjectItemCaseSensitive(
        fn_data, "electricalConnectionDescriptionListData");
    if (!cJSON_IsArray(fn_seq)) return SPINE_ERR_PARSE;

    const cJSON* ldata = seq_get(fn_seq, "electricalConnectionDescriptionData");
    if (!cJSON_IsArray(ldata)) return SPINE_OK;

    const cJSON* item_seq;
    cJSON_ArrayForEach(item_seq, ldata) {
        if (out->count >= SPINE_EC_DESC_MAX) break;
        ec_description_data_t* d = &out->items[out->count];
        memset(d, 0, sizeof(*d));

        const cJSON* ecid = seq_get(item_seq, "electricalConnectionId");
        if (cJSON_IsNumber(ecid))
            d->electrical_connection_id = (uint32_t)ecid->valuedouble;

#define DECODE_STR_EC(jk, fld, pf) do { \
    const cJSON* _v = seq_get(item_seq, jk); \
    if (cJSON_IsString(_v) && _v->valuestring) { \
        snprintf(d->fld, sizeof(d->fld), "%s", _v->valuestring); d->pf = true; } \
} while(0)
        DECODE_STR_EC("powerSupplyType",          power_supply_type,          power_supply_type_present);
        DECODE_STR_EC("positiveEnergyDirection",  positive_energy_direction,  positive_energy_direction_present);
        DECODE_STR_EC("acRmsPeriodDuration",      ac_rms_period_duration,     ac_rms_period_duration_present);
        DECODE_STR_EC("scopeType",                scope_type,                 scope_type_present);
        DECODE_STR_EC("label",                    label,                      label_present);
        DECODE_STR_EC("description",              description,                description_present);
#undef DECODE_STR_EC

        const cJSON* acp = seq_get(item_seq, "acConnectedPhases");
        if (cJSON_IsNumber(acp)) {
            d->ac_connected_phases = (uint32_t)acp->valuedouble;
            d->ac_connected_phases_present = true;
        }
        out->count++;
    }
    return SPINE_OK;
}

cJSON* ec_description_list_encode(const ec_description_list_t* in) {
    if (!in) return NULL;

    cJSON* list_arr = cJSON_CreateArray();
    if (!list_arr) return NULL;

    for (uint8_t i = 0; i < in->count; i++) {
        const ec_description_data_t* d = &in->items[i];
        cJSON* item_seq = cJSON_CreateArray();
        if (!item_seq) { cJSON_Delete(list_arr); return NULL; }

        if (!seq_add_num(item_seq, "electricalConnectionId",
                         (double)d->electrical_connection_id)) goto oom_ec;

#define ENCODE_STR_EC(pf, jk, fld) \
    if (d->pf) \
        if (!seq_add_str(item_seq, jk, d->fld)) goto oom_ec;

        ENCODE_STR_EC(power_supply_type_present,          "powerSupplyType",          power_supply_type);
        if (d->ac_connected_phases_present)
            if (!seq_add_num(item_seq, "acConnectedPhases",
                             (double)d->ac_connected_phases)) goto oom_ec;
        ENCODE_STR_EC(ac_rms_period_duration_present,     "acRmsPeriodDuration",      ac_rms_period_duration);
        ENCODE_STR_EC(positive_energy_direction_present,  "positiveEnergyDirection",  positive_energy_direction);
        ENCODE_STR_EC(scope_type_present,                 "scopeType",                scope_type);
        ENCODE_STR_EC(label_present,                      "label",                    label);
        ENCODE_STR_EC(description_present,                "description",              description);
#undef ENCODE_STR_EC

        cJSON_AddItemToArray(list_arr, item_seq);
        continue;
oom_ec:
        cJSON_Delete(item_seq); cJSON_Delete(list_arr); return NULL;
    }

    cJSON* fn_seq = cJSON_CreateArray();
    if (!fn_seq) { cJSON_Delete(list_arr); return NULL; }
    if (!seq_add_arr(fn_seq, "electricalConnectionDescriptionData", list_arr))
        { cJSON_Delete(fn_seq); return NULL; }

    cJSON* fn_data = cJSON_CreateObject();
    if (!fn_data) { cJSON_Delete(fn_seq); return NULL; }
    cJSON_AddItemToObject(fn_data,
        "electricalConnectionDescriptionListData", fn_seq);
    return fn_data;
}
