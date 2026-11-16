// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_address.h
 *
 * SPINE address type and encode/decode/compare helpers.
 * Protocol Spec v1.3.0, §3.2 and §5.2.2.
 */

#ifndef SPINE_CODEC_ADDRESS_H_
#define SPINE_CODEC_ADDRESS_H_

#include <stdbool.h>
#include <stdint.h>
#include "spine_codec_common.h"
#include "spine_codec_node_mgmt.h"  /* nm_addr_t */
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

#define SPINE_ENTITY_DEPTH_MAX   8    /* Protocol Spec §3.2: max 15, we use 8 */
/* SPINE_DEVICE_ID_MAX is defined in spine_codec_common.h (256 bytes per spec §7.1.1.2) */

/* ── spine_addr_t ─────────────────────────────────────────────────────────── */
/*
 * Full SPINE feature address: device / entity[] / feature
 * Protocol Spec §3.2: address is a hierarchical path.
 *
 * device:       "" means "this device" (simple mode omission)
 * entity_depth: number of valid entries in entity[]
 * feature:      feature address number
 */
typedef struct {
    char    device[SPINE_DEVICE_ID_MAX];
    uint8_t entity[SPINE_ENTITY_DEPTH_MAX];
    uint8_t entity_depth;
    uint16_t feature;
} spine_addr_t;

/* ── Constructors ─────────────────────────────────────────────────────────── */

/** Initialize an address to all-zero / empty. */
static inline void spine_addr_clear(spine_addr_t* a) {
    if (a) {
        a->device[0]   = '\0';
        a->entity_depth = 0;
        a->feature      = 0;
    }
}

/**
 * @brief Build a simple one-level entity address.
 * @example spine_addr_make("SKI:abc", 1, 0) → device/entity[1]/feature[0]
 */
void spine_addr_make(spine_addr_t* out,
                     const char*   device_id,
                     uint8_t       entity,
                     uint16_t      feature);

/**
 * @brief Build a two-level nested entity address.
 */
void spine_addr_make2(spine_addr_t* out,
                      const char*   device_id,
                      uint8_t       entity1,
                      uint8_t       entity2,
                      uint16_t      feature);

/* ── Compare ──────────────────────────────────────────────────────────────── */

/** Full equality (device + entity path + feature). */
bool spine_addr_equal(const spine_addr_t* a, const spine_addr_t* b);

/** Entity-only equality (ignores feature field). */
bool spine_addr_entity_equal(const spine_addr_t* a, const spine_addr_t* b);

/** Device-only equality. */
bool spine_addr_device_equal(const spine_addr_t* a, const spine_addr_t* b);

/**
 * @brief Entity prefix match: true if a's entity path is a prefix of b's.
 * e.g. a=[1], b=[1,4] → true; a=[1], b=[1] → true; a=[1,4], b=[1] → false.
 * Also requires device match. Ignores feature field.
 */
bool spine_addr_entity_is_prefix(const spine_addr_t* a, const spine_addr_t* b);

/* ── JSON encode / decode ─────────────────────────────────────────────────── */

/**
 * @brief Encode spine_addr_t to a JSON object {"device":"...","entity":[..],"feature":N}.
 * @param include_device  If false, omit "device" field (simple mode).
 * @return New cJSON object (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* spine_addr_encode(const spine_addr_t* addr, bool include_device);

/**
 * @brief Decode a JSON address object into spine_addr_t.
 * @param obj  JSON object with "device"(opt), "entity"(list), "feature".
 * @param out  Output address.
 * @return SPINE_OK or SPINE_ERR_*.
 */
spine_err_t spine_addr_decode(const cJSON* obj, spine_addr_t* out);

/* ── Conversion from nm_addr_t ────────────────────────────────────────────── */

/**
 * @brief Convert nm_addr_t (from codec) to spine_addr_t.
 * Used after parsing a DetailedDiscovery reply.
 */
void spine_addr_from_nm(spine_addr_t* out, const nm_addr_t* nm,
                        const char* fallback_device);

/* ── String representation (for logging) ─────────────────────────────────── */

/* Maximum size of spine_addr_to_str() output buffer.
 * = SPINE_DEVICE_ID_MAX + "/e[" + entity digits + "]/f" + feature digits + NUL */
#define SPINE_ADDR_STR_MAX  (SPINE_DEVICE_ID_MAX + 48)

/**
 * @brief Write address as "device/e[1,2]/f3" into buf.
 */
void spine_addr_to_str(const spine_addr_t* addr, char* buf, size_t buf_sz);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_ADDRESS_H_ */
