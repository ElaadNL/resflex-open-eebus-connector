// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/tls_persist.h
 *
 * Certificate persistence for EEBus demo programs.
 *
 * On first run, generates an ECDSA P-256 self-signed certificate and
 * stores it under ~/.eebus/<name>/.  On subsequent runs, loads the
 * existing certificate from disk.
 *
 * Directory layout:
 *   ~/.eebus/<name>/
 *     cert.pem   — self-signed certificate
 *     key.pem    — private key
 *     ski.txt    — 40-char hex SKI (for quick reference)
 */

#ifndef EEBUS_DEMO_TLS_PERSIST_H_
#define EEBUS_DEMO_TLS_PERSIST_H_

#include "eebus_error.h"
#include "ship_tls.h"   /* ship_tls_cred_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load or generate TLS credentials for a named demo program.
 *
 * If ~/.eebus/<name>/cert.pem and key.pem already exist, loads them.
 * Otherwise generates a new ECDSA P-256 certificate, saves it, and
 * writes the SKI to ~/.eebus/<name>/ski.txt.
 *
 * On success, cred->cert_path and cred->key_path are set to the
 * absolute paths so libwebsockets can read them directly.
 *
 * @param[in]  name  Program name used as the directory name (e.g. "heatpump").
 * @param[out] cred  Receives the loaded/generated credentials.
 * @return EEBUS_OK, EEBUS_ERR_IO, or EEBUS_ERR_TLS.
 */
eebus_err_t tls_persist_load(const char* name, ship_tls_cred_t* cred);

/**
 * @brief Return the base directory path for a named demo program.
 *
 * Returns something like "/home/user/.eebus/heatpump".
 * The buffer must be at least PATH_MAX bytes.
 *
 * @param[in]  name   Program name.
 * @param[out] buf    Output buffer.
 * @param[in]  bufsz  Size of buf.
 * @return EEBUS_OK or EEBUS_ERR_IO.
 */
eebus_err_t tls_persist_dir(const char* name, char* buf, size_t bufsz);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_DEMO_TLS_PERSIST_H_ */
