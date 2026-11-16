// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "src/ship/model/ship_model.h"
}

class ShipModelTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&msg, 0, sizeof(msg));
  }
  ship_msg_t msg;
};

TEST_F(ShipModelTest, HelloToJson) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_READY;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
}

TEST_F(ShipModelTest, HelloFromJson) {
  const char* json_str = "{\"connectionHello\":[{\"phase\":\"ready\"}]}";
  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json_str, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_HELLO, parsed.kind);
  EXPECT_EQ(SHIP_HELLO_PHASE_READY, parsed.hello.phase);
  ship_msg_free(&parsed);
}

TEST_F(ShipModelTest, HandshakeToJson) {
  msg.kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE;
  msg.handshake.handshake_type = SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX;
  msg.handshake.version.major = 1;
  msg.handshake.version.minor = 0;
  msg.handshake.formats_count = 1;
  msg.handshake.formats[0] = SHIP_FORMAT_JSON_UTF8;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
}

TEST_F(ShipModelTest, HandshakeFromJson) {
  const char* json_str =
    "{\"messageProtocolHandshake\":["
      "{\"handshakeType\":\"announceMax\"},"
      "{\"version\":[{\"major\":1},{\"minor\":0}]},"
      "{\"formats\":[{\"format\":[\"JSON-UTF8\"]}]}"
    "]}";
  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json_str, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_PROTOCOL_HANDSHAKE, parsed.kind);
  EXPECT_EQ(SHIP_HANDSHAKE_TYPE_ANNOUNCE_MAX, parsed.handshake.handshake_type);
  EXPECT_EQ(1U, parsed.handshake.version.major);
  EXPECT_EQ(0U, parsed.handshake.version.minor);
  EXPECT_EQ(1U, parsed.handshake.formats_count);
  EXPECT_EQ(SHIP_FORMAT_JSON_UTF8, parsed.handshake.formats[0]);
  ship_msg_free(&parsed);
}

TEST_F(ShipModelTest, CloseToJson) {
  msg.kind = SHIP_MSG_KIND_CLOSE;
  msg.close.phase = SHIP_CLOSE_PHASE_ANNOUNCE;
  msg.close.reason_present = true;
  msg.close.reason = SHIP_CLOSE_REASON_UNSPECIFIC;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
}

TEST_F(ShipModelTest, CloseFromJson) {
  const char* json_str = "{\"connectionClose\":[{\"phase\":\"announce\"},{\"reason\":\"unspecific\"}]}";
  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json_str, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_CLOSE, parsed.kind);
  EXPECT_EQ(SHIP_CLOSE_PHASE_ANNOUNCE, parsed.close.phase);
  EXPECT_TRUE(parsed.close.reason_present);
  EXPECT_EQ(SHIP_CLOSE_REASON_UNSPECIFIC, parsed.close.reason);
  ship_msg_free(&parsed);
}

TEST_F(ShipModelTest, DataToJson) {
  msg.kind = SHIP_MSG_KIND_DATA;
  strcpy(msg.data.header.protocol_id, "ee1.0");
  const char* payload = "{\"datagram\":[{}]}";
  msg.data.payload_json = (char*)malloc(strlen(payload) + 1);
  ASSERT_NE(nullptr, msg.data.payload_json);
  strcpy(msg.data.payload_json, payload);
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
  ship_msg_free(&msg);
}

TEST_F(ShipModelTest, DataFromJson) {
  const char* json_str =
    "{\"data\":["
      "{\"header\":[{\"protocolId\":\"ee1.0\"}]},"
      "{\"payload\":{\"datagram\":[]}}"
    "]}";
  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json_str, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_DATA, parsed.kind);
  EXPECT_STREQ("ee1.0", parsed.data.header.protocol_id);
  ASSERT_NE(nullptr, parsed.data.payload_json);
  EXPECT_GT(strlen(parsed.data.payload_json), 0U);
  ship_msg_free(&parsed);
}

TEST_F(ShipModelTest, FrameEncodeDecode) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_READY;
  char* json_str = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json_str);

  size_t encoded_len = 0;
  uint8_t* encoded = ship_frame_encode(SHIP_MSG_TYPE_CONTROL, json_str, &encoded_len);
  ASSERT_NE(nullptr, encoded);
  EXPECT_GT(encoded_len, 0U);

  ship_msg_type_t decoded_type;
  const char* decoded_json;
  size_t decoded_len;
  EXPECT_EQ(0, ship_frame_decode(encoded, encoded_len, &decoded_type, &decoded_json, &decoded_len));
  EXPECT_EQ(SHIP_MSG_TYPE_CONTROL, decoded_type);
  ASSERT_NE(nullptr, decoded_json);
  EXPECT_GT(decoded_len, 0U);

  ship_msg_t decoded_msg;
  memset(&decoded_msg, 0, sizeof(decoded_msg));
  EXPECT_EQ(0, ship_msg_from_json(decoded_json, &decoded_msg));
  EXPECT_EQ(SHIP_MSG_KIND_HELLO, decoded_msg.kind);
  EXPECT_EQ(SHIP_HELLO_PHASE_READY, decoded_msg.hello.phase);
  ship_msg_free(&decoded_msg);

  free(encoded);
  free(json_str);
}

TEST_F(ShipModelTest, HelloWithWaiting) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_READY;
  msg.hello.waiting_present = true;
  msg.hello.waiting = 5;
  msg.hello.prolongation_request = true;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_HELLO, parsed.kind);
  EXPECT_TRUE(parsed.hello.waiting_present);
  EXPECT_TRUE(parsed.hello.prolongation_request);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, HandshakeSelectType) {
  msg.kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE;
  msg.handshake.handshake_type = SHIP_HANDSHAKE_TYPE_SELECT;
  msg.handshake.version.major = 1;
  msg.handshake.version.minor = 0;
  msg.handshake.formats_count = 1;
  msg.handshake.formats[0] = SHIP_FORMAT_JSON_UTF8;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
}

TEST_F(ShipModelTest, PinStateRoundTrip) {
  /* Test all PIN state variants: required, optional, pinOk, none */
  for (int state = SHIP_PIN_STATE_REQUIRED; state <= SHIP_PIN_STATE_NONE; state++) {
    memset(&msg, 0, sizeof(msg));
    msg.kind = SHIP_MSG_KIND_PIN_STATE;
    msg.pin_state.pin_state = (ship_pin_state_t)state;
    char* json = ship_msg_to_json(&msg);
    ASSERT_NE(nullptr, json);

    ship_msg_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
    EXPECT_EQ(state, (int)parsed.pin_state.pin_state);
    ship_msg_free(&parsed);
    free(json);
  }
}

TEST_F(ShipModelTest, AccessMethodsAllFields) {
  msg.kind = SHIP_MSG_KIND_ACCESS_METHODS;
  msg.access_methods.id = "testSki";
  msg.access_methods.dns_sd_mdns = true;
  msg.access_methods.dns_present = true;
  msg.access_methods.dns.uri = (char*)malloc(20);
  ASSERT_NE(nullptr, msg.access_methods.dns.uri);
  strcpy(msg.access_methods.dns.uri, "https://example.com");
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_STREQ("testSki", parsed.access_methods.id);
  EXPECT_TRUE(parsed.access_methods.dns_sd_mdns);
  EXPECT_TRUE(parsed.access_methods.dns_present);
  ASSERT_NE(nullptr, parsed.access_methods.dns.uri);
  EXPECT_GT(strlen(parsed.access_methods.dns.uri), 0U);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, DataWithoutPayload) {
  msg.kind = SHIP_MSG_KIND_DATA;
  strcpy(msg.data.header.protocol_id, "ee1.0");
  msg.data.payload_json = nullptr;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);
  free(json);
  ship_msg_free(&msg);
}

TEST_F(ShipModelTest, EncodeNullReturnsNull) {
  EXPECT_EQ(nullptr, ship_msg_to_json(nullptr));
}

TEST_F(ShipModelTest, DecodeNullReturnsError) {
  EXPECT_EQ(-1, ship_msg_from_json(nullptr, nullptr));
}

TEST_F(ShipModelTest, DecodeInvalidJson) {
  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(-1, ship_msg_from_json("garbage", &parsed));
}

TEST_F(ShipModelTest, FrameEncodeNullPayload) {
  size_t encoded_len = 0;
  uint8_t* encoded = ship_frame_encode(SHIP_MSG_TYPE_CONTROL, nullptr, &encoded_len);
  ASSERT_NE(nullptr, encoded);
  EXPECT_GT(encoded_len, 0U);
  free(encoded);
}

TEST_F(ShipModelTest, FrameDecodeZeroLength) {
  ship_msg_type_t dtype;
  const char* djson;
  size_t dlen;
  EXPECT_EQ(-1, ship_frame_decode(nullptr, 0, &dtype, &djson, &dlen));
}

TEST_F(ShipModelTest, CloseReasonStrInvalid) {
  EXPECT_STREQ("unknown", ship_close_reason_str((ship_close_reason_t)999));
}

TEST_F(ShipModelTest, CloseReasonStrAllValid) {
  EXPECT_NE(nullptr, ship_close_reason_str(SHIP_CLOSE_REASON_NONE));
  EXPECT_NE(nullptr, ship_close_reason_str(SHIP_CLOSE_REASON_UNSPECIFIC));
  EXPECT_NE(nullptr, ship_close_reason_str(SHIP_CLOSE_REASON_REMOVED_CONNECTION));
}

TEST_F(ShipModelTest, HandshakeErrorRoundTrip) {
  msg.kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR;
  msg.handshake_err.error = SHIP_HANDSHAKE_ERR_TIMEOUT;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR, parsed.kind);
  EXPECT_EQ(SHIP_HANDSHAKE_ERR_TIMEOUT, parsed.handshake_err.error);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, PinInputRoundTrip) {
  msg.kind = SHIP_MSG_KIND_PIN_INPUT;
  strcpy(msg.pin_input.pin_str, "1234ABCD");
  msg.pin_input.pin = 0x1234ABCD;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_PIN_INPUT, parsed.kind);
  EXPECT_STREQ("1234ABCD", parsed.pin_input.pin_str);
  EXPECT_EQ(0x1234ABCDULL, parsed.pin_input.pin);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, PinErrorRoundTrip) {
  msg.kind = SHIP_MSG_KIND_PIN_ERROR;
  msg.pin_error.error = SHIP_PIN_ERROR_WRONG_PIN;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_PIN_ERROR, parsed.kind);
  EXPECT_EQ(SHIP_PIN_ERROR_WRONG_PIN, parsed.pin_error.error);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, AccessMethodsRequestRoundTrip) {
  msg.kind = SHIP_MSG_KIND_ACCESS_METHODS_REQUEST;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_MSG_KIND_ACCESS_METHODS_REQUEST, parsed.kind);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, ProlongationRequestRoundTrip) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_READY;
  msg.hello.waiting_present = true;
  msg.hello.waiting = 30000;
  msg.hello.prolongation_request = true;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_HELLO_PHASE_READY, parsed.hello.phase);
  EXPECT_TRUE(parsed.hello.prolongation_request);
  EXPECT_TRUE(parsed.hello.waiting_present);
  EXPECT_EQ(30000U, parsed.hello.waiting);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, HelloPendingPhase) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_PENDING;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_HELLO_PHASE_PENDING, parsed.hello.phase);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, HelloAbortedPhase) {
  msg.kind = SHIP_MSG_KIND_HELLO;
  msg.hello.phase = SHIP_HELLO_PHASE_ABORTED;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_EQ(SHIP_HELLO_PHASE_ABORTED, parsed.hello.phase);
  ship_msg_free(&parsed);
  free(json);
}

TEST_F(ShipModelTest, HandshakeErrorCodeAll) {
  for (int e = 0; e <= 3; e++) {
    memset(&msg, 0, sizeof(msg));
    msg.kind = SHIP_MSG_KIND_PROTOCOL_HANDSHAKE_ERR;
    msg.handshake_err.error = (ship_handshake_error_code_t)e;
    char* json = ship_msg_to_json(&msg);
    ASSERT_NE(nullptr, json);

    ship_msg_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
    EXPECT_EQ(e, (int)parsed.handshake_err.error);
    ship_msg_free(&parsed);
    free(json);
  }
}

TEST_F(ShipModelTest, AccessMethodsWithoutDns) {
  msg.kind = SHIP_MSG_KIND_ACCESS_METHODS;
  msg.access_methods.id = "testSki";
  msg.access_methods.dns_sd_mdns = true;
  msg.access_methods.dns_present = false;
  char* json = ship_msg_to_json(&msg);
  ASSERT_NE(nullptr, json);

  ship_msg_t parsed;
  memset(&parsed, 0, sizeof(parsed));
  EXPECT_EQ(0, ship_msg_from_json(json, &parsed));
  EXPECT_STREQ("testSki", parsed.access_methods.id);
  EXPECT_TRUE(parsed.access_methods.dns_sd_mdns);
  EXPECT_FALSE(parsed.access_methods.dns_present);
  ship_msg_free(&parsed);
  free(json);
}
