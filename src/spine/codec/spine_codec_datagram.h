// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * spine_codec_datagram.h
 *
 * SPINE datagram cJSON encode/decode.
 * Consistent with all other codec function styles:
 *   decode: (const cJSON*, spine_msg_t*) -> spine_err_t
 *   encode: (const spine_msg_t*)         -> cJSON*
 *
 * Wire format (Protocol Spec §5.2 + §5.3, SHIP §11.4 xs:sequence → Array):
 *   {"datagram":[
 *     {"header":[{"specificationVersion":"1.3.0"},
 *                {"addressSource":[{"device":"..."},{"entity":[0]},{"feature":0}]},
 *                {"addressDestination":[...]},
 *                {"msgCounter":5},{"cmdClassifier":"read"}]},
 *     {"payload":[{"cmd":[
 *       [{"<FUNCTION>":[xs_seq_array]}]
 *     ]}]}
 *   ]}
 *
 * Ownership rules:
 *   decode - fn_data is an internal pointer into the passed-in root tree, not owned;
 *            the caller (spine_device_dispatch) can simply cJSON_Delete(root) after
 *            the handler returns, without separately freeing fn_data.
 *   encode - Returns a newly created cJSON tree (caller must cJSON_Delete).
 */

#ifndef SPINE_CODEC_DATAGRAM_H_
#define SPINE_CODEC_DATAGRAM_H_

#include "spine_codec_common.h"
#include "spine_codec_msg.h"
#include "cjson/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a parsed cJSON datagram tree into spine_msg_t.
 *
 * fn_data is a pointer to an internal node of the root tree (not copied);
 * fn_data is valid as long as root is alive.
 *
 * For full READ: fn_name has a value, fn_data is an empty object {} (§5.3.4.4).
 *
 * @param root  The complete datagram tree obtained by the caller via cJSON_Parse
 * @param out   Output spine_msg_t
 * @return SPINE_OK or SPINE_ERR_PARSE
 */
spine_err_t spine_datagram_decode(const cJSON* root, spine_msg_t* out);

/**
 * @brief Encode spine_msg_t into a cJSON datagram tree.
 *
 * payload.fn_data == NULL -> generates {fn_name: {}} (full READ).
 * fn_data content is cJSON_Duplicate'd; caller may free the original fn_data
 * at any time after encode.
 *
 * @param msg  The message to encode
 * @return Newly created cJSON tree (caller must cJSON_Delete), NULL on failure
 */
cJSON* spine_datagram_encode(const spine_msg_t* msg);

#ifdef __cplusplus
}
#endif

#endif /* SPINE_CODEC_DATAGRAM_H_ */
