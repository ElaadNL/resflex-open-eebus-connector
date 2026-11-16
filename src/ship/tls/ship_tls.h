// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_tls.h
 * @brief SHIP TLS certificate management.
 *
 * SHIP uses mutual TLS (mTLS) with self-signed certificates.  The Subject Key
 * Identifier (SKI) of each certificate acts as the node's permanent identity.
 *
 * SHIP §7, §8, §12
 *
 * ## Certificate requirements (SHIP §8.3)
 * - Key type : ECDSA with P-256 curve (secp256r1)
 * - Validity : at least 1 year (this SDK generates 10-year certs)
 * - SKI      : SHA-1 of the subjectPublicKey BIT STRING value
 *
 * ## SKI format
 * The SKI is represented as a 40-character lowercase hex string,
 * matching the mDNS TXT record "ski=" field. SHIP §7.3.2
 */

#ifndef EEBUS_SRC_SHIP_TLS_SHIP_TLS_H_
#define EEBUS_SRC_SHIP_TLS_SHIP_TLS_H_

#include <stddef.h>
#include <stdint.h>

#include "../../common/eebus_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Length of a SKI hex string (40 chars + NUL = 41 bytes). */
#define SHIP_SKI_STR_LEN (41U)

/** Maximum file path length stored inside ship_tls_cred_t. */
#define SHIP_TLS_PATH_MAX (512U)

/**
 * @brief Loaded certificate and private key for use with libwebsockets.
 *
 * PEM strings (cert_pem / key_pem) are heap-allocated; call
 * ship_tls_cred_free() to release them.
 *
 * cert_path / key_path are populated by ship_tls_load() and ship_tls_save().
 * They must be non-empty before passing the credential to ship_ws_*() when
 * libwebsockets is configured to use the file-path TLS API.
 *
 * Typical flow for a generated certificate:
 *   ship_tls_generate(&cred, "MyDevice");
 *   ship_tls_save(&cred, "/etc/eebus/cert.pem", "/etc/eebus/key.pem");
 *   // cred.cert_path and cred.key_path are now set
 *   ship_ws_server_create(&ws, &ws_cfg);
 */
typedef struct {
  char* cert_pem; /**< PEM-encoded certificate (heap, NUL-terminated)  */
  char* key_pem;  /**< PEM-encoded private key  (heap, NUL-terminated) */
  char  ski[SHIP_SKI_STR_LEN]; /**< SKI hex string (40 chars + NUL)    */

  /** Filesystem paths used by the libwebsockets file-path TLS API.
   *  Set automatically by ship_tls_load() and ship_tls_save().
   *  Empty string ("") means the path has not been set yet. */
  char  cert_path[SHIP_TLS_PATH_MAX]; /**< Path to PEM certificate file */
  char  key_path[SHIP_TLS_PATH_MAX];  /**< Path to PEM private key file */
} ship_tls_cred_t;

/**
 * @brief Generate a new ECDSA P-256 self-signed certificate and private key.
 *
 * The certificate is suitable for SHIP mTLS as specified in SHIP §8.3.
 * Both PEM strings are heap-allocated inside @p cred.
 *
 * @param[out] cred         Receives the generated certificate and key.
 * @param[in]  common_name  CN field of the certificate (e.g. device model name).
 *                          If NULL, "EEBus Device" is used.
 * @return EEBUS_OK on success, EEBUS_ERR_TLS on generation failure.
 */
eebus_err_t ship_tls_generate(ship_tls_cred_t* cred, const char* common_name);

/**
 * @brief Load a certificate and private key from PEM files on disk.
 *
 * On success, copies @p cert_path and @p key_path into cred->cert_path and
 * cred->key_path so the credential can be used directly with the
 * libwebsockets file-path TLS API.
 *
 * @param[out] cred      Receives the loaded credentials.
 * @param[in]  cert_path Path to the PEM certificate file.
 * @param[in]  key_path  Path to the PEM private key file.
 * @return EEBUS_OK, EEBUS_ERR_IO if files cannot be read,
 *         or EEBUS_ERR_CERT if the certificate is malformed.
 */
eebus_err_t ship_tls_load(ship_tls_cred_t* cred, const char* cert_path, const char* key_path);

/**
 * @brief Save a generated certificate and key to PEM files.
 *
 * On success, copies @p cert_path and @p key_path into cred->cert_path and
 * cred->key_path.  After calling this function the credential is ready for
 * use with the libwebsockets file-path TLS API.
 *
 * @param[in,out] cred      Credentials to save (paths are updated on success).
 * @param[in]     cert_path Destination path for the certificate.
 * @param[in]     key_path  Destination path for the private key.
 * @return EEBUS_OK or EEBUS_ERR_IO.
 */
eebus_err_t ship_tls_save(ship_tls_cred_t* cred, const char* cert_path, const char* key_path);

/**
 * @brief Compute the SKI of a PEM certificate.
 *
 * The SKI is the lowercase hex encoding of SHA-1(subjectPublicKey BIT STRING).
 *
 * @param[in]  cert_pem  NUL-terminated PEM certificate string.
 * @param[out] ski_out   Buffer of at least SHIP_SKI_STR_LEN bytes.
 * @return EEBUS_OK or EEBUS_ERR_CERT.
 */
eebus_err_t ship_tls_compute_ski(const char* cert_pem, char ski_out[SHIP_SKI_STR_LEN]);

/**
 * @brief Compute the SKI from a DER-encoded certificate.
 *
 * Intended for use with libwebsockets' LWS_TLS_CERT_INFO_DER_RAW, which
 * delivers the peer certificate as raw DER bytes after the TLS handshake.
 *
 * @param[in]  der      DER-encoded certificate bytes.
 * @param[in]  der_len  Length of @p der in bytes.
 * @param[out] ski_out  Buffer of at least SHIP_SKI_STR_LEN bytes.
 * @return EEBUS_OK or EEBUS_ERR_CERT.
 */
eebus_err_t ship_tls_compute_ski_from_der(const uint8_t* der, size_t der_len,
                                          char ski_out[SHIP_SKI_STR_LEN]);

/**
 * @brief Verify that the SKI of a peer certificate matches an expected value.
 *
 * Used during the TLS handshake to authenticate the peer's identity.
 *
 * @param[in] cert_pem      PEM certificate received from the peer.
 * @param[in] expected_ski  Expected 40-char hex SKI string.
 * @return EEBUS_OK if they match, EEBUS_ERR_CERT otherwise.
 */
eebus_err_t ship_tls_verify_ski(const char* cert_pem, const char* expected_ski);

/**
 * @brief Free heap memory owned by @p cred.
 *
 * Does not free @p cred itself (it may be stack-allocated).
 *
 * @param[in] cred  Credentials to release. NULL is a no-op.
 */
void ship_tls_cred_free(ship_tls_cred_t* cred);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_TLS_SHIP_TLS_H_ */
