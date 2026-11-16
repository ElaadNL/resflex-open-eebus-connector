// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_msg.h"
#include "src/spine/codec/spine_codec_address.h"
}

class MsgTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&hdr, 0, sizeof(hdr));
    memset(&req_hdr, 0, sizeof(req_hdr));
    memset(&msg, 0, sizeof(msg));
  }
  spine_header_t hdr, req_hdr;
  spine_msg_t msg;
};

TEST_F(MsgTest, CmdStrAllValid) {
  EXPECT_STREQ("read", spine_cmd_str(SPINE_CMD_READ));
  EXPECT_STREQ("reply", spine_cmd_str(SPINE_CMD_REPLY));
  EXPECT_STREQ("write", spine_cmd_str(SPINE_CMD_WRITE));
  EXPECT_STREQ("call", spine_cmd_str(SPINE_CMD_CALL));
  EXPECT_STREQ("notify", spine_cmd_str(SPINE_CMD_NOTIFY));
  EXPECT_STREQ("result", spine_cmd_str(SPINE_CMD_RESULT));
}

TEST_F(MsgTest, CmdStrInvalidReturnsUnknown) {
  EXPECT_STREQ("unknown", spine_cmd_str((spine_cmd_t)99));
}

TEST_F(MsgTest, MakeMsgReqSetsFields) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "dev1", 1, 1);
  spine_addr_make(&dst, "dev2", 1, 2);
  cJSON* data = cJSON_CreateObject();
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 42, true, "testFunction", data);
  EXPECT_EQ(SPINE_CMD_READ, msg.header.cmd);
  EXPECT_TRUE(msg.header.ack_request);
  EXPECT_EQ(42U, msg.header.msg_counter);
  EXPECT_STREQ("testFunction", msg.payload.fn_name);
  EXPECT_EQ(data, msg.payload.fn_data);
  cJSON_Delete(data);
}

TEST_F(MsgTest, MakeMsgResSetsFields) {
  spine_addr_make(&req_hdr.src, "peer", 1, 1);
  spine_addr_make(&req_hdr.dst, "local", 1, 2);
  req_hdr.msg_counter = 7;
  req_hdr.cmd = SPINE_CMD_READ;

  cJSON* data = cJSON_CreateObject();
  spine_make_msg_res(&msg, &req_hdr, SPINE_CMD_REPLY, 100, "testFunction", data);
  EXPECT_EQ(SPINE_CMD_REPLY, msg.header.cmd);
  EXPECT_EQ(100U, msg.header.msg_counter);
  EXPECT_EQ(7U, msg.header.msg_counter_ref);
  EXPECT_TRUE(msg.header.msg_counter_ref_present);
  EXPECT_STREQ("testFunction", msg.payload.fn_name);
  EXPECT_EQ(data, msg.payload.fn_data);
  cJSON_Delete(data);
}

TEST_F(MsgTest, HeaderEncodeDecodeRoundTrip) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "devA", 0, 1);
  spine_addr_make(&dst, "devB", 1, 2);
  memset(&hdr, 0, sizeof(hdr));
  hdr.cmd = SPINE_CMD_WRITE;
  hdr.msg_counter = 12345;
  hdr.ack_request = true;
  memcpy(&hdr.src, &src, sizeof(spine_addr_t));
  memcpy(&hdr.dst, &dst, sizeof(spine_addr_t));
  strcpy(hdr.spec_version, "1.3.0");
  hdr.msg_counter_ref = 999;
  hdr.msg_counter_ref_present = true;

  cJSON* json = spine_header_encode(&hdr);
  ASSERT_NE(nullptr, json);
  spine_header_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_header_decode(json, &decoded));
  EXPECT_EQ(hdr.cmd, decoded.cmd);
  EXPECT_EQ(hdr.msg_counter, decoded.msg_counter);
  EXPECT_EQ(hdr.ack_request, decoded.ack_request);
  cJSON_Delete(json);
}

TEST_F(MsgTest, HeaderEncodeDecodeWithRef) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "devA", 0, 1);
  spine_addr_make(&dst, "devB", 1, 2);
  memset(&hdr, 0, sizeof(hdr));
  hdr.cmd = SPINE_CMD_CALL;
  hdr.msg_counter = 42;
  hdr.ack_request = true;
  memcpy(&hdr.src, &src, sizeof(spine_addr_t));
  memcpy(&hdr.dst, &dst, sizeof(spine_addr_t));
  strcpy(hdr.spec_version, "1.3.0");
  hdr.msg_counter_ref_present = true;
  hdr.msg_counter_ref = 7;

  cJSON* json = spine_header_encode(&hdr);
  ASSERT_NE(nullptr, json);
  spine_header_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_header_decode(json, &decoded));
  EXPECT_TRUE(decoded.msg_counter_ref_present);
  EXPECT_EQ(7U, decoded.msg_counter_ref);
  cJSON_Delete(json);
}

TEST_F(MsgTest, HeaderEncodeDecodeWithoutRef) {
  spine_addr_t src, dst;
  spine_addr_make(&src, "devA", 0, 1);
  spine_addr_make(&dst, "devB", 1, 2);
  memset(&hdr, 0, sizeof(hdr));
  hdr.cmd = SPINE_CMD_RESULT;
  hdr.msg_counter = 99;
  memcpy(&hdr.src, &src, sizeof(spine_addr_t));
  memcpy(&hdr.dst, &dst, sizeof(spine_addr_t));
  strcpy(hdr.spec_version, "1.3.0");

  cJSON* json = spine_header_encode(&hdr);
  ASSERT_NE(nullptr, json);
  spine_header_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_header_decode(json, &decoded));
  EXPECT_FALSE(decoded.msg_counter_ref_present);
  cJSON_Delete(json);
}

TEST_F(MsgTest, MakeMsgResWithoutData) {
  spine_addr_make(&req_hdr.src, "peer", 1, 1);
  spine_addr_make(&req_hdr.dst, "local", 1, 2);
  req_hdr.msg_counter = 7;

  cJSON* null_data = nullptr;
  spine_make_msg_res(&msg, &req_hdr, SPINE_CMD_RESULT, 100, "testFunction", null_data);
  EXPECT_EQ(SPINE_CMD_RESULT, msg.header.cmd);
  EXPECT_EQ(nullptr, msg.payload.fn_data);
}
