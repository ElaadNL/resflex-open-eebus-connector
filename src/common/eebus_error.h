// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_error.h
 * @brief EEBus SDK unified error codes.
 *
 * Every public API function that can fail returns an eebus_err_t.
 * Callers MUST check return values; ignoring them may result in undefined
 * behaviour at protocol level.
 *
 * Convention:
 *   - EEBUS_OK  (0) always means success.
 *   - Negative values are error conditions.
 */

#ifndef EEBUS_INCLUDE_EEBUS_ERROR_H_
#define EEBUS_INCLUDE_EEBUS_ERROR_H_

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return type used by all fallible SDK functions. */
typedef int eebus_err_t;

/* ── Generic ─────────────────────────────────────────────────────────────── */
/** Success. */
#define EEBUS_OK             (0)
/** Unspecified internal error. */
#define EEBUS_ERR_INTERNAL   (-1)
/** One or more arguments are invalid (NULL pointer, out-of-range value, …). */
#define EEBUS_ERR_INVALID    (-2)
/** Memory allocation failed. */
#define EEBUS_ERR_NO_MEMORY  (-3)
/** The requested operation is not permitted in the current state. */
#define EEBUS_ERR_STATE      (-4)
/** A timeout expired before the operation completed. */
#define EEBUS_ERR_TIMEOUT    (-5)
/** The resource or item was not found. */
#define EEBUS_ERR_NOT_FOUND  (-6)
/** The operation is already in progress or the resource already exists. */
#define EEBUS_ERR_EXISTS     (-7)
/** Input/output error (file, socket, …). */
#define EEBUS_ERR_IO         (-8)
/** Buffer too small to hold the result. */
#define EEBUS_ERR_OVERFLOW   (-9)
/** Feature or path not supported on this platform / configuration. */
#define EEBUS_ERR_UNSUPPORTED (-10)

/* ── TLS / Certificate ───────────────────────────────────────────────────── */
/** TLS handshake failed. */
#define EEBUS_ERR_TLS        (-20)
/** Certificate validation failed. */
#define EEBUS_ERR_CERT       (-21)

/* ── SHIP ────────────────────────────────────────────────────────────────── */
/** SHIP handshake protocol error. */
#define EEBUS_ERR_SHIP_HANDSHAKE  (-30)
/** Remote node rejected the connection. */
#define EEBUS_ERR_SHIP_REJECTED   (-31)
/** SHIP protocol version mismatch. */
#define EEBUS_ERR_SHIP_VERSION    (-32)
/** Transport connection was dropped before the handshake completed. */
#define EEBUS_ERR_DISCONNECTED    (-33)

/* ── SPINE ───────────────────────────────────────────────────────────────── */
/** Received datagram failed validation. */
#define EEBUS_ERR_SPINE_INVALID_DATAGRAM (-40)
/** Write request was denied by the remote feature. */
#define EEBUS_ERR_SPINE_WRITE_DENIED     (-41)
/** Subscription or binding limit reached. */
#define EEBUS_ERR_SPINE_LIMIT            (-42)

/* ── Use Case ────────────────────────────────────────────────────────────── */
/** Use case is not yet in a state where this operation is valid. */
#define EEBUS_ERR_UC_NOT_READY  (-50)
/** Failsafe power limit was applied due to heartbeat timeout. */
#define EEBUS_ERR_UC_FAILSAFE   (-51)

/**
 * @brief Convert an error code to a human-readable string.
 *
 * The returned string is statically allocated and must not be freed.
 *
 * @param[in] err  Error code (any EEBUS_ERR_* or EEBUS_OK).
 * @return         Null-terminated description string.
 */
const char* eebus_err_str(eebus_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_INCLUDE_EEBUS_ERROR_H_ */
