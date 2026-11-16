// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/**
 * @file  ship_model.c
 * @brief SHIP 1.0.1 message serialisation and deserialisation using cJSON.
 *
 * Wire format: EEBus_SHIP_TS_Specification_v1.0.1 §11.4 +
 * EEBus_SHIP_TS_TransferProtocol.xsd.
 *
 * All SHIP complex types use xs:sequence, which per §11.4.5 Table 6 maps to
 * a JSON array where each child element becomes a {"name": value} object:
 *
 *   {"connectionHello": [{"phase":"pending"}, {"waiting":60000}]}
 *   {"messageProtocolHandshake": [{"handshakeType":"announceMax"},
 *                                  {"version":[{"major":1},{"minor":0}]},
 *                                  {"formats":[{"format":["JSON-UTF8"]}]}]}
 *
 * The root object is sent directly - no outer array wrapper:
 *   {"connectionHello": [...]}    correct
 *   [{"connectionHello": [...]}]  wrong
 *
 * Empty xs:sequence types (dnsSd_mDns, accessMethodsRequest) serialise to []
 * per §11.4.6 rule 4.
 */

#include "ship_model.h"
#include "../../common/eebus_memory.h"

#include <cjson/cJSON.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* String constants matching the SHIP JSON field names */

static const char* kKeyConnectionHello              = "connectionHello";
static const char* kKeyMsgProtocolHandshake         = "messageProtocolHandshake";
static const char* kKeyMsgProtocolHandshakeError    = "messageProtocolHandshakeError";
static const char* kKeyConnectionPinState           = "connectionPinState";
static const char* kKeyConnectionPinInput           = "connectionPinInput";
static const char* kKeyConnectionPinError           = "connectionPinError";
static const char* kKeyAccessMethodsRequest         = "accessMethodsRequest";
static const char* kKeyAccessMethods                = "accessMethods";
static const char* kKeyConnectionClose              = "connectionClose";
static const char* kKeyData                         = "data";

/* Helpers */

static char* cjson_strdup(const cJSON* item) {
  if (!cJSON_IsString(item) || !item->valuestring) return NULL;
  return eebus_strdup(item->valuestring);
}

/*
 * Look up a named field inside an xs:sequence JSON array.
 *
 * xs:sequence serialises to a JSON array of single-key objects:
 *   [{"phase":"pending"}, {"waiting":60000}]
 * Iterates the array and returns the value for the first element whose key
 * matches key, or NULL if not found.
 */
static cJSON* seq_get(const cJSON* arr, const char* key) {
  if (!cJSON_IsArray(arr)) return NULL;
  cJSON* item;
  cJSON_ArrayForEach(item, arr) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(item, key);
    if (v) return v;
  }
  return NULL;
}

/* Append {"key": string_value} to a sequence array. */
static void seq_add_str(cJSON* arr, const char* key, const char* val) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, key, val);
  cJSON_AddItemToArray(arr, o);
}

/* Append {"key": number_value} to a sequence array. */
static void seq_add_num(cJSON* arr, const char* key, double val) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, key, val);
  cJSON_AddItemToArray(arr, o);
}

/* Append {"key": bool_value} to a sequence array. */
static void seq_add_bool(cJSON* arr, const char* key, int val) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, key, val);
  cJSON_AddItemToArray(arr, o);
}

/* Append {"key": child} to a sequence array (child is adopted). */
static void seq_add_item(cJSON* arr, const char* key, cJSON* child) {
  cJSON* o = cJSON_CreateObject();
  cJSON_AddItemToObject(o, key, child);
  cJSON_AddItemToArray(arr, o);
}

/* Hello serialisation */

static const char* hello_phase_str(ship_hello_phase_t p) {
  switch (p) {
    case SHIP_HELLO_PHASE_PENDING: return "pending";
    case SHIP_HELLO_PHASE_READY:   return "ready";
    case SHIP_HELLO_PHASE_ABORTED: return "aborted";
    default:                       return "pending";
  }
}

static ship_hello_phase_t hello_phase_from_str(const char* s) {
  if (!s)                        return SHIP_HELLO_PHASE_PENDING;
  if (strcmp(s, "ready")   == 0) return SHIP_HELLO_PHASE_READY;
  if (strcmp(s, "aborted") == 0) return SHIP_HELLO_PHASE_ABORTED;
  return SHIP_HELLO_PHASE_PENDING;
}

/*
 * ConnectionHelloType xs:sequence -> JSON array:
 *   [{"phase":"pending"}, {"waiting":60000}, {"prolongationRequest":true}]
 */
static cJSON* hello_to_cjson(const ship_hello_t* h) {
  cJSON* arr = cJSON_CreateArray();
  seq_add_str(arr, "phase", hello_phase_str(h->phase));
  if (h->waiting_present)      seq_add_num(arr, "waiting", (double)h->waiting);
  if (h->prolongation_request) seq_add_bool(arr, "prolongationRequest", cJSON_True);
  return arr;
}

static int hello_from_cjson(const cJSON* arr, ship_hello_t* h) {
  memset(h, 0, sizeof(*h));
  cJSON* phase = seq_get(arr, "phase");
  if (!cJSON_IsString(phase)) return -1;
  h->phase = hello_phase_from_str(phase->valuestring);

  cJSON* waiting = seq_get(arr, "waiting");
  if (cJSON_IsNumber(waiting)) {
    h->waiting_present = true;
    h->waiting         = (uint32_t)waiting->valuedouble;
  }
  cJSON* prolreq = seq_get(arr, "prolongationRequest");
  if (cJSON_IsBool(prolreq)) h->prolongation_request = cJSON_IsTrue(prolreq);
  return 0;
}

/* Protocol handshake serialisation */

static const char* handshake_type_str(ship_handshake_type_t t) {
  return (t == SHIP_HANDSHAKE_TYPE_SELECT) ? "select" : "announceMax";
}

static ship_handshake_type_t handshake_type_from_str(const char* s) {
  if (s && strcmp(s, "select") == 0) return SHIP_HANDSHAKE_TYPE_SELECT;
  return SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX;
}

static const char* format_str(ship_format_t f) {
  return (f == SHIP_FORMAT_JSON_UTF16) ? "JSON-UTF16" : "JSON-UTF8";
}

static ship_format_t format_from_str(const char* s) {
  if (s && strcmp(s, "JSON-UTF16") == 0) return SHIP_FORMAT_JSON_UTF16;
  return SHIP_FORMAT_JSON_UTF8;
}

/*
 * MessageProtocolHandshakeType xs:sequence -> JSON array:
 *   [
 *     {"handshakeType": "announceMax"},
 *     {"version": [{"major":1}, {"minor":0}]},
 *     {"formats": [{"format": ["JSON-UTF8"]}]}
 *   ]
 *
 * version: inner xs:sequence -> [{"major":1},{"minor":0}]
 * formats: MessageProtocolFormatsType xs:sequence with maxOccurs=unbounded
 *          "format" child -> [{"format":["JSON-UTF8"]}]
 */
static cJSON* handshake_to_cjson(const ship_protocol_handshake_t* h) {
  cJSON* arr = cJSON_CreateArray();

  seq_add_str(arr, "handshakeType", handshake_type_str(h->handshake_type));

  /* version xs:sequence -> [{"major":N}, {"minor":N}] */
  cJSON* ver = cJSON_CreateArray();
  seq_add_num(ver, "major", h->version.major);
  seq_add_num(ver, "minor", h->version.minor);
  seq_add_item(arr, "version", ver);

  /* formats xs:sequence -> [{"format":[str, ...]}] */
  cJSON* format_vals = cJSON_CreateArray();
  for (uint8_t i = 0; i < h->formats_count; i++) {
    cJSON_AddItemToArray(format_vals, cJSON_CreateString(format_str(h->formats[i])));
  }
  cJSON* fmts = cJSON_CreateArray();
  seq_add_item(fmts, "format", format_vals);
  seq_add_item(arr, "formats", fmts);

  return arr;
}

static int handshake_from_cjson(const cJSON* arr, ship_protocol_handshake_t* h) {
  memset(h, 0, sizeof(*h));

  cJSON* ht = seq_get(arr, "handshakeType");
  if (!cJSON_IsString(ht)) return -1;
  h->handshake_type = handshake_type_from_str(ht->valuestring);

  cJSON* ver = seq_get(arr, "version");
  if (cJSON_IsArray(ver)) {
    cJSON* maj = seq_get(ver, "major");
    cJSON* min = seq_get(ver, "minor");
    if (cJSON_IsNumber(maj)) h->version.major = (uint8_t)maj->valuedouble;
    if (cJSON_IsNumber(min)) h->version.minor = (uint8_t)min->valuedouble;
  }

  cJSON* fmts = seq_get(arr, "formats");
  if (cJSON_IsArray(fmts)) {
    cJSON* format_arr = seq_get(fmts, "format");
    if (cJSON_IsArray(format_arr)) {
      cJSON* item;
      cJSON_ArrayForEach(item, format_arr) {
        if (h->formats_count < SHIP_HANDSHAKE_MAX_FORMATS && cJSON_IsString(item)) {
          h->formats[h->formats_count++] = format_from_str(item->valuestring);
        }
      }
    }
  }
  return 0;
}

/* Close serialisation */

static const char* close_phase_str(ship_close_phase_t p) {
  return (p == SHIP_CLOSE_PHASE_CONFIRM) ? "confirm" : "announce";
}

static ship_close_phase_t close_phase_from_str(const char* s) {
  if (s && strcmp(s, "confirm") == 0) return SHIP_CLOSE_PHASE_CONFIRM;
  return SHIP_CLOSE_PHASE_ANNOUNCE;
}

/* Wire-format mapping: only the two spec-defined strings are emitted.
 * Internal sentinel NONE is intentionally folded into "unspecific" here
 * as a safety net — it should never actually be serialised. */
static const char* close_reason_wire_str(ship_close_reason_t r) {
  switch (r) {
    case SHIP_CLOSE_REASON_REMOVED_CONNECTION: return "removedConnection";
    case SHIP_CLOSE_REASON_UNSPECIFIC:         /* fall-through */
    default:                                   return "unspecific";
  }
}

/* Public log/display mapping — distinguishes all enum values. */
const char* ship_close_reason_str(ship_close_reason_t r) {
  switch (r) {
    case SHIP_CLOSE_REASON_NONE:               return "none";
    case SHIP_CLOSE_REASON_UNSPECIFIC:         return "unspecific";
    case SHIP_CLOSE_REASON_REMOVED_CONNECTION: return "removedConnection";
    default:                                   return "unknown";
  }
}

static ship_close_reason_t close_reason_from_str(const char* s) {
  if (s && strcmp(s, "removedConnection") == 0)
    return SHIP_CLOSE_REASON_REMOVED_CONNECTION;
  return SHIP_CLOSE_REASON_UNSPECIFIC;
}

/*
 * ConnectionCloseType xs:sequence -> JSON array:
 *   [{"phase":"announce"}, {"maxTime":10000}, {"reason":"unspecific"}]
 */
static cJSON* close_to_cjson(const ship_close_t* c) {
  cJSON* arr = cJSON_CreateArray();
  seq_add_str(arr, "phase", close_phase_str(c->phase));
  if (c->max_time_present) seq_add_num(arr, "maxTime", (double)c->max_time);
  /* SHIP §13.4.7 Table 25: "reason" is Optional.
   * §13.4.7.1.2 DEV-B rule 1: confirm MUST have no other sub-elements set. */
  if (c->reason_present) seq_add_str(arr, "reason", close_reason_wire_str(c->reason));
  return arr;
}

static int close_from_cjson(const cJSON* arr, ship_close_t* c) {
  memset(c, 0, sizeof(*c));
  cJSON* ph = seq_get(arr, "phase");
  if (!cJSON_IsString(ph)) return -1;
  c->phase = close_phase_from_str(ph->valuestring);

  cJSON* mt = seq_get(arr, "maxTime");
  if (cJSON_IsNumber(mt)) {
    c->max_time_present = true;
    c->max_time         = (uint32_t)mt->valuedouble;
  }
  cJSON* reason = seq_get(arr, "reason");
  if (cJSON_IsString(reason)) {
    c->reason_present = true;
    c->reason         = close_reason_from_str(reason->valuestring);
  }
  /* If reason absent, reason_present stays false; handle_close() defaults to
   * UNSPECIFIC which means reconnection IS expected (§13.4.7.1.1). */
  return 0;
}

/* Access methods serialisation */

/*
 * AccessMethodsType xs:sequence -> JSON array:
 *   [{"id":"my-device"}, {"dnsSd_mDns":[]}, {"dns":[{"uri":"https://..."}]}]
 *
 * dnsSd_mDns is an empty xs:sequence -> [] per §11.4.6 rule 4.
 * dns contains xs:sequence with "uri" -> [{"uri":"..."}].
 */
static cJSON* access_methods_to_cjson(const ship_access_methods_t* a) {
  cJSON* arr = cJSON_CreateArray();
  if (a->id) seq_add_str(arr, "id", a->id);
  if (a->dns_sd_mdns) {
    /* empty xs:sequence element -> [] */
    seq_add_item(arr, "dnsSd_mDns", cJSON_CreateArray());
  }
  if (a->dns_present && a->dns.uri) {
    cJSON* dns_arr = cJSON_CreateArray();
    seq_add_str(dns_arr, "uri", a->dns.uri);
    seq_add_item(arr, "dns", dns_arr);
  }
  return arr;
}

static int access_methods_from_cjson(const cJSON* arr, ship_access_methods_t* a) {
  memset(a, 0, sizeof(*a));
  a->id = cjson_strdup(seq_get(arr, "id"));

  /* dnsSd_mDns presence (value is []) means mDNS is supported */
  if (seq_get(arr, "dnsSd_mDns")) a->dns_sd_mdns = true;

  cJSON* dns_arr = seq_get(arr, "dns");
  if (cJSON_IsArray(dns_arr)) {
    a->dns_present = true;
    a->dns.uri     = cjson_strdup(seq_get(dns_arr, "uri"));
  }
  return 0;
}

/* Public API */

char* ship_msg_to_json(const ship_msg_t* msg) {
  if (!msg) return NULL;

  cJSON* root  = cJSON_CreateObject();
  cJSON* inner = NULL;

  switch (msg->kind) {
    case SHIP_MSG_KIND_HELLO:
      inner = hello_to_cjson(&msg->hello);
      cJSON_AddItemToObject(root, kKeyConnectionHello, inner);
      break;

    case SHIP_MSG_KIND_PROTOCOL_HANDSHAKE:
      inner = handshake_to_cjson(&msg->handshake);
      cJSON_AddItemToObject(root, kKeyMsgProtocolHandshake, inner);
      break;

    case SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR:
      /* MessageProtocolHandshakeErrorType xs:sequence -> [{"error":N}] */
      inner = cJSON_CreateArray();
      seq_add_num(inner, "error", (double)msg->handshake_err.error);
      cJSON_AddItemToObject(root, kKeyMsgProtocolHandshakeError, inner);
      break;

    case SHIP_MSG_KIND_PIN_STATE: {
      /* ConnectionPinStateType xs:sequence -> [{"pinState":"..."}, ...] */
      inner = cJSON_CreateArray();
      const ship_pin_state_msg_t* ps = &msg->pin_state;
      const char* ps_str = "none";
      switch (ps->pin_state) {
        case SHIP_PIN_STATE_REQUIRED: ps_str = "required"; break;
        case SHIP_PIN_STATE_OPTIONAL: ps_str = "optional"; break;
        case SHIP_PIN_STATE_PIN_OK:   ps_str = "pinOk";    break;
        default:                      ps_str = "none";     break;
      }
      seq_add_str(inner, "pinState", ps_str);
      if (ps->input_permission_present) {
        seq_add_str(inner, "inputPermission",
            ps->input_permission == SHIP_PIN_INPUT_PERMISSION_OK ? "ok" : "busy");
      }
      cJSON_AddItemToObject(root, kKeyConnectionPinState, inner);
      break;
    }

    case SHIP_MSG_KIND_PIN_INPUT: {
      /* ConnectionPinInputType xs:sequence -> [{"pin":"AABBCCDD"}] */
      inner = cJSON_CreateArray();
      /* §13.4.4.3.5: "pin" is a string of 8-16 contiguous hex chars */
      if (msg->pin_input.pin_str[0] != '\0') {
        seq_add_str(inner, "pin", msg->pin_input.pin_str);
      } else {
        char hex[17];
        snprintf(hex, sizeof(hex), "%llX",
                 (unsigned long long)msg->pin_input.pin);
        seq_add_str(inner, "pin", hex);
      }
      cJSON_AddItemToObject(root, kKeyConnectionPinInput, inner);
      break;
    }

    case SHIP_MSG_KIND_PIN_ERROR:
      /* ConnectionPinErrorType xs:sequence -> [{"error":N}] */
      inner = cJSON_CreateArray();
      seq_add_num(inner, "error", (double)msg->pin_error.error);
      cJSON_AddItemToObject(root, kKeyConnectionPinError, inner);
      break;

    case SHIP_MSG_KIND_ACCESS_METHODS_REQUEST:
      /* AccessMethodsRequestType: empty xs:complexType -> [] */
      cJSON_AddItemToObject(root, kKeyAccessMethodsRequest, cJSON_CreateArray());
      break;

    case SHIP_MSG_KIND_ACCESS_METHODS:
      inner = access_methods_to_cjson(&msg->access_methods);
      cJSON_AddItemToObject(root, kKeyAccessMethods, inner);
      break;

    case SHIP_MSG_KIND_CLOSE:
      inner = close_to_cjson(&msg->close);
      cJSON_AddItemToObject(root, kKeyConnectionClose, inner);
      break;

    case SHIP_MSG_KIND_DATA: {
      /*
       * DataType xs:sequence -> [{"header":[{"protocolId":"..."}]},{"payload":{}}]
       * HeaderType xs:sequence -> [{"protocolId":"..."}]
       */
      inner = cJSON_CreateArray();

      cJSON* hdr_arr = cJSON_CreateArray();
      seq_add_str(hdr_arr, "protocolId", msg->data.header.protocol_id);
      seq_add_item(inner, "header", hdr_arr);

      if (msg->data.payload_json) {
        cJSON* payload = cJSON_Parse(msg->data.payload_json);
        if (payload) seq_add_item(inner, "payload", payload);
      }
      cJSON_AddItemToObject(root, kKeyData, inner);
      break;
    }

    default:
      cJSON_Delete(root);
      return NULL;
  }

  /* Root is a plain JSON object - no outer array wrapper.
   * Wire format: {"connectionHello":[...]} */
  char* json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json_str;
}

int ship_msg_from_json(const char* json, ship_msg_t* msg) {
  if (!json || !msg) return -1;
  memset(msg, 0, sizeof(*msg));

  cJSON* root = cJSON_Parse(json);
  if (!root) return -1;

  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return -1;
  }

  int rc = 0;

  cJSON* hello = cJSON_GetObjectItemCaseSensitive(root, kKeyConnectionHello);
  if (hello) {
    msg->kind = SHIP_MSG_KIND_HELLO;
    rc        = hello_from_cjson(hello, &msg->hello);
    goto done;
  }

  cJSON* hs = cJSON_GetObjectItemCaseSensitive(root, kKeyMsgProtocolHandshake);
  if (hs) {
    msg->kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE;
    rc        = handshake_from_cjson(hs, &msg->handshake);
    goto done;
  }

  cJSON* hse = cJSON_GetObjectItemCaseSensitive(root, kKeyMsgProtocolHandshakeError);
  if (hse) {
    msg->kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR;
    cJSON* err = seq_get(hse, "error");
    if (cJSON_IsNumber(err))
      msg->handshake_err.error = (ship_handshake_error_code_t)(int)err->valuedouble;
    goto done;
  }

  cJSON* ps = cJSON_GetObjectItemCaseSensitive(root, kKeyConnectionPinState);
  if (ps) {
    msg->kind = SHIP_MSG_KIND_PIN_STATE;
    cJSON* pst = seq_get(ps, "pinState");
    if (cJSON_IsString(pst)) {
      const char* s = pst->valuestring;
      if      (strcmp(s, "required") == 0) msg->pin_state.pin_state = SHIP_PIN_STATE_REQUIRED;
      else if (strcmp(s, "optional") == 0) msg->pin_state.pin_state = SHIP_PIN_STATE_OPTIONAL;
      else if (strcmp(s, "pinOk")    == 0) msg->pin_state.pin_state = SHIP_PIN_STATE_PIN_OK;
      else                                 msg->pin_state.pin_state = SHIP_PIN_STATE_NONE;
    }
    cJSON* ip = seq_get(ps, "inputPermission");
    if (cJSON_IsString(ip)) {
      msg->pin_state.input_permission_present = true;
      msg->pin_state.input_permission =
          (strcmp(ip->valuestring, "ok") == 0)
              ? SHIP_PIN_INPUT_PERMISSION_OK
              : SHIP_PIN_INPUT_PERMISSION_BUSY;
    }
    goto done;
  }

  cJSON* pi = cJSON_GetObjectItemCaseSensitive(root, kKeyConnectionPinInput);
  if (pi) {
    msg->kind = SHIP_MSG_KIND_PIN_INPUT;
    cJSON* pin_val = seq_get(pi, "pin");
    /* §13.4.4.3.5: "pin" is a string of 8-16 hex chars */
    if (cJSON_IsString(pin_val) && pin_val->valuestring) {
      snprintf(msg->pin_input.pin_str, sizeof(msg->pin_input.pin_str),
               "%s", pin_val->valuestring);
      msg->pin_input.pin =
          (uint64_t)strtoull(pin_val->valuestring, NULL, 16);
    } else if (cJSON_IsNumber(pin_val)) {
      msg->pin_input.pin = (uint64_t)pin_val->valuedouble;
      snprintf(msg->pin_input.pin_str, sizeof(msg->pin_input.pin_str),
               "%llX", (unsigned long long)msg->pin_input.pin);
    }
    goto done;
  }

  cJSON* pe = cJSON_GetObjectItemCaseSensitive(root, kKeyConnectionPinError);
  if (pe) {
    msg->kind = SHIP_MSG_KIND_PIN_ERROR;
    cJSON* err = seq_get(pe, "error");
    if (cJSON_IsNumber(err))
      msg->pin_error.error = (ship_pin_error_code_t)(int)err->valuedouble;
    goto done;
  }

  if (cJSON_GetObjectItemCaseSensitive(root, kKeyAccessMethodsRequest)) {
    msg->kind = SHIP_MSG_KIND_ACCESS_METHODS_REQUEST;
    goto done;
  }

  cJSON* am = cJSON_GetObjectItemCaseSensitive(root, kKeyAccessMethods);
  if (am) {
    msg->kind = SHIP_MSG_KIND_ACCESS_METHODS;
    rc        = access_methods_from_cjson(am, &msg->access_methods);
    goto done;
  }

  cJSON* cl = cJSON_GetObjectItemCaseSensitive(root, kKeyConnectionClose);
  if (cl) {
    msg->kind = SHIP_MSG_KIND_CLOSE;
    rc        = close_from_cjson(cl, &msg->close);
    goto done;
  }

  cJSON* data = cJSON_GetObjectItemCaseSensitive(root, kKeyData);
  if (data) {
    msg->kind = SHIP_MSG_KIND_DATA;
    /* DataType xs:sequence array: header and payload are array elements */
    cJSON* hdr_arr = seq_get(data, "header");
    if (cJSON_IsArray(hdr_arr)) {
      cJSON* pid = seq_get(hdr_arr, "protocolId");
      if (cJSON_IsString(pid)) {
        snprintf(msg->data.header.protocol_id,
                 sizeof(msg->data.header.protocol_id),
                 "%s", pid->valuestring);
      }
    }
    cJSON* payload = seq_get(data, "payload");
    if (payload) {
      msg->data.payload_json = cJSON_PrintUnformatted(payload);
    }
    goto done;
  }

  /* Unknown message type */
  rc = -1;

done:
  cJSON_Delete(root);
  return rc;
}

void ship_msg_free(ship_msg_t* msg) {
  if (!msg) return;
  switch (msg->kind) {
    case SHIP_MSG_KIND_ACCESS_METHODS:
      EEBUS_FREE((char*)msg->access_methods.id);
      EEBUS_FREE(msg->access_methods.dns.uri);
      break;
    case SHIP_MSG_KIND_DATA:
      EEBUS_FREE(msg->data.payload_json);
      break;
    default:
      break;
  }
  memset(msg, 0, sizeof(*msg));
}

uint8_t* ship_frame_encode(ship_msg_type_t type, const char* json_payload, size_t* out_len) {
  if (!out_len) return NULL;

  size_t json_len   = json_payload ? strlen(json_payload) : 0;
  size_t frame_size = 1 + json_len;

  uint8_t* frame = EEBUS_MALLOC(frame_size);
  if (!frame) return NULL;

  frame[0] = type;
  if (json_len > 0) memcpy(frame + 1, json_payload, json_len);

  *out_len = frame_size;
  return frame;
}

int ship_frame_decode(
    const uint8_t*   frame,
    size_t           frame_len,
    ship_msg_type_t* type,
    const char**     json_out,
    size_t*          json_len
) {
  if (!frame || frame_len < 1 || !type) return -1;

  *type = frame[0];

  if (json_out) *json_out = (frame_len > 1) ? (const char*)(frame + 1) : NULL;
  if (json_len) *json_len = (frame_len > 1) ? frame_len - 1 : 0;

  return 0;
}
