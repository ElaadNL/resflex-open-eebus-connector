// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  eebus_error.c
 * @brief EEBus error-code to human-readable string conversion.
 */

#include "eebus_error.h"

const char* eebus_err_str(eebus_err_t err) {
  switch (err) {
    case EEBUS_OK:                          return "OK";
    case EEBUS_ERR_INTERNAL:               return "Internal error";
    case EEBUS_ERR_INVALID:                return "Invalid argument";
    case EEBUS_ERR_NO_MEMORY:              return "Out of memory";
    case EEBUS_ERR_STATE:                  return "Invalid state";
    case EEBUS_ERR_TIMEOUT:                return "Timeout";
    case EEBUS_ERR_NOT_FOUND:              return "Not found";
    case EEBUS_ERR_EXISTS:                 return "Already exists";
    case EEBUS_ERR_IO:                     return "I/O error";
    case EEBUS_ERR_OVERFLOW:               return "Buffer overflow";
    case EEBUS_ERR_UNSUPPORTED:            return "Not supported";
    case EEBUS_ERR_TLS:                    return "TLS error";
    case EEBUS_ERR_CERT:                   return "Certificate error";
    case EEBUS_ERR_SHIP_HANDSHAKE:         return "SHIP handshake error";
    case EEBUS_ERR_SHIP_REJECTED:          return "SHIP connection rejected";
    case EEBUS_ERR_SHIP_VERSION:           return "SHIP version mismatch";
    case EEBUS_ERR_DISCONNECTED:           return "Transport disconnected before handshake";
    case EEBUS_ERR_SPINE_INVALID_DATAGRAM: return "SPINE invalid datagram";
    case EEBUS_ERR_SPINE_WRITE_DENIED:     return "SPINE write denied";
    case EEBUS_ERR_SPINE_LIMIT:            return "SPINE subscription/binding limit";
    case EEBUS_ERR_UC_NOT_READY:           return "Use case not ready";
    case EEBUS_ERR_UC_FAILSAFE:            return "Use case failsafe activated";
    default:                               return "Unknown error";
  }
}
