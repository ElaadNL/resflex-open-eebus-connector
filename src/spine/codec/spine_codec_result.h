// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_result.h
 *
 * Encode / decode for SPINE resultData function.
 * Resource Spec v1.3.0, §3.11, Table 18 & 19.
 *
 * resultData is used as the payload for messages with cmdClassifier="result".
 * It carries errorNumber (0 = OK, >0 = error) and an optional description.
 */

#ifndef SPINE_CODEC_RESULT_H_
#define SPINE_CODEC_RESULT_H_

#include <stdint.h>
#include "spine_codec_common.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Data structure ───────────────────────────────────────────────────────── */

typedef struct {
    uint32_t error_number;
    char     description[SPINE_DESCRIPTION_MAX];
    bool     description_present;
} spine_result_data_t;

/* ── Codec ────────────────────────────────────────────────────────────────── */

/**
 * @brief Decode resultData from a cmd payload array.
 *
 * @param payload  cJSON array (datagram.payload.cmd[])
 * @param out      Output struct.
 * @return SPINE_OK or SPINE_ERR_*.
 */
spine_err_t spine_result_data_decode(const cJSON* fn_data,
                                     spine_result_data_t* out);

/**
 * @brief Encode resultData to a cmd payload array.
 *
 * @param in  Input struct.
 * @return New cJSON array (caller must cJSON_Delete), or NULL on OOM.
 */
cJSON* spine_result_data_encode(const spine_result_data_t* in);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_RESULT_H_ */
