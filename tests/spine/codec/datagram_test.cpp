// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_datagram.h"
#include "src/spine/codec/spine_codec_address.h"
}

class DatagramTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&msg, 0, sizeof(msg));
    memset(&decoded, 0, sizeof(decoded));
  }
  spine_msg_t msg, decoded;
};

TEST_F(DatagramTest, EncodeDecodeRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "dev1", 0, 1);
  spine_addr_make(&dst, "dev2", 1, 2);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_READ;
  msg.header.msg_counter = 5;
  msg.header.ack_request = true;
  strcpy(msg.payload.fn_name, "deviceConfigurationKeyValueListData");
  msg.payload.fn_data = cJSON_CreateObject();
  cJSON_AddItemToObject(msg.payload.fn_data, msg.payload.fn_name, cJSON_CreateObject());

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);

  spine_err_t err = spine_datagram_decode(json, &decoded);
  EXPECT_EQ(SPINE_OK, err);
  EXPECT_EQ(msg.header.cmd, decoded.header.cmd);
  EXPECT_EQ(msg.header.msg_counter, decoded.header.msg_counter);
  EXPECT_EQ(msg.header.ack_request, decoded.header.ack_request);
  EXPECT_STREQ(msg.payload.fn_name, decoded.payload.fn_name);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
}

TEST_F(DatagramTest, DecodeNullRootReturnsError) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_datagram_decode(nullptr, &decoded));
}

TEST_F(DatagramTest, DecodeInvalidJsonReturnsError) {
  cJSON* root = cJSON_CreateObject();
  EXPECT_EQ(SPINE_ERR_PARSE, spine_datagram_decode(root, &decoded));
  cJSON_Delete(root);
}

TEST_F(DatagramTest, WriteRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "devA", 0, 1);
  spine_addr_make(&dst, "devB", 0, 2);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_WRITE;
  msg.header.msg_counter = 99;
  msg.header.ack_request = true;
  strcpy(msg.payload.fn_name, "loadControlLimitListData");
  msg.payload.fn_data = cJSON_CreateObject();
  cJSON_AddItemToObject(msg.payload.fn_data, msg.payload.fn_name, cJSON_CreateArray());

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_datagram_decode(json, &decoded));
  EXPECT_EQ(SPINE_CMD_WRITE, decoded.header.cmd);
  EXPECT_STREQ("loadControlLimitListData", decoded.payload.fn_name);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
}

TEST_F(DatagramTest, NotifyRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make2(&src, "dev1", 0, 1, 1);
  spine_addr_make(&dst, "dev2", 5, 3);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_NOTIFY;
  msg.header.msg_counter = 7;
  strcpy(msg.payload.fn_name, "measurementListData");
  msg.payload.fn_data = cJSON_CreateObject();
  cJSON_AddItemToObject(msg.payload.fn_data, msg.payload.fn_name, cJSON_CreateArray());

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_datagram_decode(json, &decoded));
  EXPECT_EQ(SPINE_CMD_NOTIFY, decoded.header.cmd);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
}

TEST_F(DatagramTest, EncodeNullMsgReturnsNull) {
  EXPECT_EQ(nullptr, spine_datagram_encode(nullptr));
}

/* Helper: create a simple SELECTOR for testing */
static cJSON* make_test_selector(void) {
  cJSON* sel_arr = cJSON_CreateArray();
  if (!sel_arr) return NULL;
  cJSON* sel_obj = cJSON_CreateObject();
  if (!sel_obj) { cJSON_Delete(sel_arr); return NULL; }
  cJSON_AddNumberToObject(sel_obj, "measurementId", 1);
  cJSON_AddItemToArray(sel_arr, sel_obj);
  return sel_arr;
}

/* Helper: create a simple ELEMENTS for testing */
static cJSON* make_test_elements(void) {
  cJSON* el_arr = cJSON_CreateArray();
  if (!el_arr) return NULL;
  cJSON* el_obj = cJSON_CreateObject();
  if (!el_obj) { cJSON_Delete(el_arr); return NULL; }
  cJSON_AddStringToObject(el_obj, "valueType", "activePower");
  cJSON_AddItemToArray(el_arr, el_obj);
  return el_arr;
}

TEST_F(DatagramTest, RfePartialFilterRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "dev1", 0, 1);
  spine_addr_make(&dst, "dev2", 1, 2);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_NOTIFY;
  msg.header.msg_counter = 5;
  strcpy(msg.payload.fn_name, "measurementListData");

  msg.payload.partial.present = true;
  msg.payload.partial.selector = make_test_selector();
  msg.payload.partial.elements = make_test_elements();
  msg.payload.fn_data = cJSON_Parse(R"({"measurementListData":{"measData":[]}})");
  ASSERT_NE(nullptr, msg.payload.fn_data);

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);

  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_datagram_decode(json, &decoded));
  EXPECT_TRUE(decoded.payload.partial.present);
  EXPECT_STREQ("measurementListData", decoded.payload.fn_name);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
  cJSON_Delete(msg.payload.partial.selector);
  cJSON_Delete(msg.payload.partial.elements);
}

TEST_F(DatagramTest, RfeDeleteFilterRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "dev1", 0, 1);
  spine_addr_make(&dst, "dev2", 1, 2);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_NOTIFY;
  msg.header.msg_counter = 5;
  strcpy(msg.payload.fn_name, "measurementListData");

  msg.payload.delete_.present = true;
  msg.payload.delete_.selector = make_test_selector();
  msg.payload.fn_data = cJSON_Parse(R"({"measurementListData":{"measData":[]}})");
  ASSERT_NE(nullptr, msg.payload.fn_data);

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);

  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_datagram_decode(json, &decoded));
  EXPECT_TRUE(decoded.payload.delete_.present);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
  cJSON_Delete(msg.payload.delete_.selector);
}

TEST_F(DatagramTest, RfeBothFiltersRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "dev1", 0, 1);
  spine_addr_make(&dst, "dev2", 1, 2);
  strcpy(msg.header.spec_version, "1.3.0");
  memcpy(&msg.header.src, &src, sizeof(spine_addr_t));
  memcpy(&msg.header.dst, &dst, sizeof(spine_addr_t));
  msg.header.cmd = SPINE_CMD_NOTIFY;
  msg.header.msg_counter = 5;
  strcpy(msg.payload.fn_name, "loadControlLimitListData");

  msg.payload.delete_.present = true;
  msg.payload.delete_.selector = make_test_selector();
  msg.payload.partial.present = true;
  msg.payload.partial.selector = make_test_selector();
  msg.payload.partial.elements = make_test_elements();
  msg.payload.fn_data = cJSON_Parse(R"({"loadControlLimitListData":[]})");
  ASSERT_NE(nullptr, msg.payload.fn_data);

  cJSON* json = spine_datagram_encode(&msg);
  ASSERT_NE(nullptr, json);

  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_datagram_decode(json, &decoded));
  EXPECT_TRUE(decoded.payload.delete_.present);
  EXPECT_TRUE(decoded.payload.partial.present);

  cJSON_Delete(json);
  cJSON_Delete(msg.payload.fn_data);
  cJSON_Delete(msg.payload.delete_.selector);
  cJSON_Delete(msg.payload.partial.selector);
  cJSON_Delete(msg.payload.partial.elements);
}
