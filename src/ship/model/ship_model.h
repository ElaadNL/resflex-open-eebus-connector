// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_model.h
 * @brief SHIP 1.0.1 wire-format data structures.
 *
 * Every structure in this file corresponds directly to a JSON object defined
 * in the SHIP specification (EEBus_SHIP_TS_Specification_v1.0.1, chapter 13).
 *
 * Naming convention
 * -----------------
 * JSON field names are preserved as C member names (snake_case).
 * Optional fields are represented as pointers; NULL means "absent".
 *
 * Memory ownership
 * ----------------
 * String members (char*) that are heap-allocated must be freed by the caller.
 * Use ship_msg_free() to release a parsed ship_msg_t and all its children.
 */

#ifndef EEBUS_SRC_SHIP_MODEL_SHIP_MODEL_H_
#define EEBUS_SRC_SHIP_MODEL_SHIP_MODEL_H_

#include <stdbool.h>
#include <stddef.h>   /* size_t */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol constants (SHIP 1.0.1 §13.4) ──────────────────────────────── */

/** Wire protocol identifier placed in the SHIP header. SHIP §13.4.5.1 */
#define SHIP_PROTOCOL_ID "ee1.0"

/** Maximum SHIP protocol major version this SDK supports. */
#define SHIP_MAX_MAJOR_VERSION (1U)

/** Maximum SHIP protocol minor version this SDK supports. */
#define SHIP_MAX_MINOR_VERSION (0U)

/** Length of a SKI string (40 hex characters, no terminator counted). */
#define SHIP_SKI_LEN (40U)

/* ── Message type byte (first byte of every SHIP frame) ─────────────────── */
/* SHIP §13.3.2, Table 13 */

typedef uint8_t ship_msg_type_t;

#define SHIP_MSG_TYPE_INIT    ((ship_msg_type_t)0x00) /**< CMI init message   */
#define SHIP_MSG_TYPE_CONTROL ((ship_msg_type_t)0x01) /**< Control messages   */
#define SHIP_MSG_TYPE_DATA    ((ship_msg_type_t)0x02) /**< SPINE payload      */
#define SHIP_MSG_TYPE_END     ((ship_msg_type_t)0x03) /**< Connection close   */

/* ── Hello phase (SHIP §13.4.4.1) ───────────────────────────────────────── */

/** Values for connectionHello.phase */
typedef enum {
  SHIP_HELLO_PHASE_PENDING = 0, /**< "pending" – still deciding           */
  SHIP_HELLO_PHASE_READY   = 1, /**< "ready"   – ready to proceed         */
  SHIP_HELLO_PHASE_ABORTED = 2, /**< "aborted" – rejecting the connection */
} ship_hello_phase_t;

/**
 * @brief connectionHello message body. SHIP §13.4.4.1
 *
 * JSON key: "connectionHello"
 */
typedef struct {
  ship_hello_phase_t phase;              /**< "phase" – mandatory              */
  bool               waiting_present;    /**< true when "waiting" is included  */
  uint32_t           waiting;            /**< "waiting" – ms remaining on timer*/
  bool               prolongation_request; /**< "prolongationRequest" – opt.   */
} ship_hello_t;

/* ── Protocol handshake (SHIP §13.4.4.2) ────────────────────────────────── */

/** Values for messageProtocolHandshake.handshakeType */
typedef enum {
  SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX = 0, /**< "announceMax" */
  SHIP_HANDSHAKE_TYPE_SELECT       = 1, /**< "select"      */
} ship_handshake_type_t;

/** Wire format for a protocol version pair. */
typedef struct {
  uint8_t major; /**< Major version number */
  uint8_t minor; /**< Minor version number */
} ship_version_t;

/** Supported message encoding formats. */
typedef enum {
  SHIP_FORMAT_JSON_UTF8  = 0, /**< "JSON-UTF8"  – mandatory minimum */
  SHIP_FORMAT_JSON_UTF16 = 1, /**< "JSON-UTF16" – optional          */
} ship_format_t;

/** Maximum number of format entries in a handshake message. */
#define SHIP_HANDSHAKE_MAX_FORMATS (2U)

/**
 * @brief messageProtocolHandshake message body. SHIP §13.4.4.2
 *
 * JSON key: "messageProtocolHandshake"
 */
typedef struct {
  ship_handshake_type_t handshake_type;                      /**< "handshakeType" */
  ship_version_t        version;                             /**< "version"       */
  ship_format_t         formats[SHIP_HANDSHAKE_MAX_FORMATS]; /**< "formats.format"*/
  uint8_t               formats_count;                       /**< actual count    */
} ship_protocol_handshake_t;

/** Values for messageProtocolHandshakeError.error */
typedef enum {
  SHIP_HANDSHAKE_ERR_RFU                = 0,
  SHIP_HANDSHAKE_ERR_TIMEOUT            = 1,
  SHIP_HANDSHAKE_ERR_UNEXPECTED_MESSAGE = 2,
  SHIP_HANDSHAKE_ERR_SELECTION_MISMATCH = 3,
} ship_handshake_error_code_t;

/**
 * @brief messageProtocolHandshakeError body. SHIP §13.4.4.2
 *
 * JSON key: "messageProtocolHandshakeError"
 */
typedef struct {
  ship_handshake_error_code_t error; /**< "error" */
} ship_protocol_handshake_error_t;

/* ── PIN phase (SHIP §13.4.5) ────────────────────────────────────────────── */

/** Values for connectionPinState.pinState */
typedef enum {
  SHIP_PIN_STATE_REQUIRED = 0, /**< "required" – PIN must be entered   */
  SHIP_PIN_STATE_OPTIONAL = 1, /**< "optional" – PIN entry is optional */
  SHIP_PIN_STATE_PIN_OK   = 2, /**< "pinOk"    – PIN accepted          */
  SHIP_PIN_STATE_NONE     = 3, /**< "none"     – no PIN used           */
} ship_pin_state_t;

/** Values for connectionPinState.inputPermission */
typedef enum {
  SHIP_PIN_INPUT_PERMISSION_BUSY = 0, /**< "busy" */
  SHIP_PIN_INPUT_PERMISSION_OK   = 1, /**< "ok"   */
} ship_pin_input_permission_t;

/**
 * @brief connectionPinState message body. SHIP §13.4.5
 *
 * JSON key: "connectionPinState"
 */
typedef struct {
  ship_pin_state_t           pin_state;                 /**< "pinState"        */
  bool                       input_permission_present;  /**< optional field    */
  ship_pin_input_permission_t input_permission;         /**< "inputPermission" */
} ship_pin_state_msg_t;

/**
 * @brief connectionPinInput message body. SHIP §13.4.5
 *
 * JSON key: "connectionPinInput"
 *
 * §13.4.4.3.5: the "pin" field on the wire is a string of 8-16 contiguous
 * hexadecimal characters (case-insensitive).  pin_str holds the canonical
 * wire value; pin holds the equivalent uint64 for convenience.
 */
typedef struct {
  char     pin_str[17]; /**< "pin" – hex string, e.g. "12ABC34D" (NUL-terminated) */
  uint64_t pin;         /**< numeric equivalent of pin_str (for internal use)      */
} ship_pin_input_t;

/** Values for connectionPinError.error */
typedef enum {
  SHIP_PIN_ERROR_RFU_0     = 0,
  SHIP_PIN_ERROR_WRONG_PIN = 1,
  SHIP_PIN_ERROR_RFU_4     = 4,
} ship_pin_error_code_t;

/**
 * @brief connectionPinError message body. SHIP §13.4.5
 *
 * JSON key: "connectionPinError"
 */
typedef struct {
  ship_pin_error_code_t error; /**< "error" */
} ship_pin_error_t;

/* ── Access Methods phase (SHIP §13.4.6) ────────────────────────────────── */

/**
 * @brief accessMethodsRequest message body. SHIP §13.4.6
 *
 * JSON key: "accessMethodsRequest"
 * (Empty object – presence alone is the signal.)
 */
typedef struct {
  uint8_t _reserved; /**< Placeholder to avoid zero-size struct in C */
} ship_access_methods_request_t;

/**
 * @brief DNS sub-element within accessMethods.
 */
typedef struct {
  char* uri; /**< "uri" – heap-allocated, caller must free */
} ship_dns_t;

/**
 * @brief accessMethods message body. SHIP §13.4.6
 *
 * JSON key: "accessMethods"
 */
typedef struct {
  const char* id;          /**< "id"        – SKI/unique-ID string (const, not heap-owned) */
  bool        dns_sd_mdns; /**< "dnsSd_mDns"                            */
  bool        dns_present; /**< true when "dns" sub-object is included  */
  ship_dns_t  dns;         /**< "dns"       – only valid if dns_present  */
} ship_access_methods_t;

/* ── Connection close (SHIP §13.4.7) ────────────────────────────────────── */

/** Values for connectionClose.phase */
typedef enum {
  SHIP_CLOSE_PHASE_ANNOUNCE = 0, /**< "announce" – initiating close  */
  SHIP_CLOSE_PHASE_CONFIRM  = 1, /**< "confirm"  – acknowledging     */
} ship_close_phase_t;

/**
 * @brief Reason codes for connectionClose.reason (SHIP §13.4.7.1.1).
 *
 * Only two values are defined by the specification:
 *  - UNSPECIFIC:         Temporary disconnection; reconnection expected.
 *  - REMOVED_CONNECTION: Sender has removed the peer from its trusted list.
 *                        If the peer reconnects it will be treated as unknown.
 *
 * Unknown reason strings received from the peer are mapped to UNSPECIFIC
 * (the safer default: assume reconnection is possible).
 */
typedef enum {
  /**
   * Internal sentinel: no close has been initiated yet.
   * Used to distinguish an unexpected transport drop (value stays NONE) from
   * a SHIP-layer graceful close (value set to UNSPECIFIC/REMOVED_CONNECTION).
   * NOT a wire value — never serialised.
   */
  SHIP_CLOSE_REASON_NONE               = 0,
  SHIP_CLOSE_REASON_UNSPECIFIC         = 1, /**< "unspecific"        */
  SHIP_CLOSE_REASON_REMOVED_CONNECTION = 2, /**< "removedConnection" */
} ship_close_reason_t;

/**
 * @brief Human-readable string for a ship_close_reason_t.
 *
 * For logs and application-level display.  Distinguishes all enum values
 * including the internal sentinel SHIP_CLOSE_REASON_NONE.  NOT for wire
 * serialisation: the SHIP encoder uses a separate internal mapping that
 * only emits the spec-defined "unspecific" / "removedConnection" strings.
 */
const char* ship_close_reason_str(ship_close_reason_t r);

/**
 * @brief connectionClose message body. SHIP §13.4.7
 *
 * JSON key: "connectionClose"
 */
typedef struct {
  ship_close_phase_t  phase;            /**< "phase"                         */
  bool                max_time_present; /**< optional                        */
  uint32_t            max_time;         /**< "maxTime" – ms                  */
  /**
   * Whether to include "reason" in the serialised message.
   *
   * SHIP §13.4.7 Table 25: "reason" is Optional.
   * SHIP §13.4.7.1.2 DEV-B rule 1: the confirm message SHALL have
   * "no other sub-element set" – i.e. only "phase":"confirm" is sent.
   * Set reason_present = true only for "announce" messages.
   */
  bool                reason_present;   /**< when true, "reason" is included */
  ship_close_reason_t reason;           /**< "reason" – see ship_close_reason_t */
  /* doReconnect is NOT part of the SHIP v1.0.1 specification (Table 25 only
   * defines phase, maxTime, reason).  Reconnection intent is conveyed by
   * "reason": unspecific → reconnect expected; removedConnection → do not.
   * The field has been removed to avoid sending non-standard wire data. */
} ship_close_t;

/* ── Data message (SHIP §13.4.5.2) ────────────────────────────────────────  */

/**
 * @brief SHIP data frame header.
 *
 * JSON key: "header" inside "data"
 */
typedef struct {
  char protocol_id[8]; /**< "protocolId" – e.g. "ee1.0" */
} ship_data_header_t;

/**
 * @brief SHIP data frame (carries SPINE payload). SHIP §13.4.5.2
 *
 * The payload is kept as a raw JSON string to decouple SHIP from SPINE parsing.
 * JSON key: "data"
 */
typedef struct {
  ship_data_header_t header;       /**< "header"  */
  char*              payload_json; /**< "payload" – heap-allocated JSON string */
} ship_data_t;

/* ── Unified message discriminated union ────────────────────────────────── */

/** Identifies which variant of ship_msg_t is active. */
typedef enum {
  SHIP_MSG_KIND_HELLO                  = 0,
  SHIP_MSG_KIND_PROTOCOL_HANDSHAKE     = 1,
  SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR = 2,
  SHIP_MSG_KIND_PIN_STATE              = 3,
  SHIP_MSG_KIND_PIN_INPUT              = 4,
  SHIP_MSG_KIND_PIN_ERROR              = 5,
  SHIP_MSG_KIND_ACCESS_METHODS_REQUEST = 6,
  SHIP_MSG_KIND_ACCESS_METHODS         = 7,
  SHIP_MSG_KIND_CLOSE                  = 8,
  SHIP_MSG_KIND_DATA                   = 9,
} ship_msg_kind_t;

/**
 * @brief Unified SHIP message container.
 *
 * After parsing, inspect `kind` to determine which union member is valid.
 */
typedef struct {
  ship_msg_kind_t kind;

  union {
    ship_hello_t                    hello;
    ship_protocol_handshake_t       handshake;
    ship_protocol_handshake_error_t handshake_err;
    ship_pin_state_msg_t            pin_state;
    ship_pin_input_t                pin_input;
    ship_pin_error_t                pin_error;
    ship_access_methods_request_t   access_req;
    ship_access_methods_t           access_methods;
    ship_close_t                    close;
    ship_data_t                     data;
  };
} ship_msg_t;

/* ── Serialisation / deserialisation ─────────────────────────────────────── */

/**
 * @brief Serialise @p msg to a JSON string.
 *
 * The returned string is heap-allocated; caller must free() it.
 *
 * @param[in] msg  Message to serialise.
 * @return Heap-allocated JSON string, or NULL on error.
 */
char* ship_msg_to_json(const ship_msg_t* msg);

/**
 * @brief Parse a SHIP control-layer JSON string into a ship_msg_t.
 *
 * @param[in]  json  NULL-terminated JSON string from the wire.
 * @param[out] msg   Populated on success. Call ship_msg_free() when done.
 * @return 0 on success, -1 on parse error.
 */
int ship_msg_from_json(const char* json, ship_msg_t* msg);

/**
 * @brief Release any heap memory owned by @p msg members.
 *
 * Does not free @p msg itself (it may be stack-allocated).
 *
 * @param[in] msg  Message to clean up. NULL is a no-op.
 */
void ship_msg_free(ship_msg_t* msg);

/* ── Frame encode / decode helpers ──────────────────────────────────────── */

/**
 * @brief Encode a SHIP frame ready to send over WebSocket.
 *
 * Prepends the 1-byte message-type header before the JSON payload.
 * The returned buffer is heap-allocated; caller must free() it.
 *
 * @param[in]  type          SHIP_MSG_TYPE_INIT / CONTROL / DATA / END.
 * @param[in]  json_payload  JSON body (may be NULL for INIT frames).
 * @param[out] out_len       Length of the returned buffer in bytes.
 * @return Heap-allocated frame buffer, or NULL on error.
 */
uint8_t* ship_frame_encode(ship_msg_type_t type, const char* json_payload, size_t* out_len);

/**
 * @brief Decode a raw WebSocket frame into its type and JSON payload.
 *
 * The payload is NOT heap-allocated; it points into @p frame.
 *
 * @param[in]  frame       Raw frame bytes.
 * @param[in]  frame_len   Length of @p frame.
 * @param[out] type        Receives the message type byte.
 * @param[out] json_out    Receives a pointer into @p frame at the JSON start.
 * @param[out] json_len    Length of the JSON portion.
 * @return 0 on success, -1 if frame is too short or malformed.
 */
int ship_frame_decode(
    const uint8_t*   frame,
    size_t           frame_len,
    ship_msg_type_t* type,
    const char**     json_out,
    size_t*          json_len
);

#ifdef __cplusplus
}
#endif

#endif /* EEBUS_SRC_SHIP_MODEL_SHIP_MODEL_H_ */
