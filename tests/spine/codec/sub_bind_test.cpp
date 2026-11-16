// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_sub_bind.h"
}

class SubBindTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&req, 0, sizeof(req));
    memset(&del, 0, sizeof(del));
    memset(&client, 0, sizeof(client));
    memset(&server, 0, sizeof(server));
  }
  spine_sb_request_t req;
  spine_sb_delete_t del;
  nm_addr_t client;
  nm_addr_t server;
};

TEST_F(SubBindTest, DecodeSubscribeRequest) {
  cJSON* json = cJSON_Parse(
    "{\"nodeManagementSubscriptionRequestCall\":["
    "{\"subscriptionRequest\":["
    "{\"clientAddress\":[{\"device\":\"cli\"},{\"entity\":[0]},{\"feature\":1}]},"
    "{\"serverAddress\":[{\"device\":\"srv\"},{\"entity\":[0]},{\"feature\":3}]},"
    "{\"serverFeatureType\":\"LoadControl\"}"
    "]}]}"
  );
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_sb_request_decode(json, &req));
  EXPECT_EQ(SPINE_SB_REQUEST_SUBSCRIBE, req.type);
  EXPECT_STREQ("cli", req.client_addr.device);
  EXPECT_STREQ("srv", req.server_addr.device);
  EXPECT_TRUE(req.server_feature_type_present);
  EXPECT_STREQ("LoadControl", req.server_feature_type);
  cJSON_Delete(json);
}

TEST_F(SubBindTest, DecodeBindRequest) {
  cJSON* json = cJSON_Parse(
    "{\"nodeManagementBindingRequestCall\":["
    "{\"bindingRequest\":["
    "{\"clientAddress\":[{\"device\":\"cli\"},{\"entity\":[0]},{\"feature\":1}]},"
    "{\"serverAddress\":[{\"device\":\"srv\"},{\"entity\":[0,1]},{\"feature\":2}]}"
    "]}]}"
  );
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_sb_request_decode(json, &req));
  EXPECT_EQ(SPINE_SB_REQUEST_BIND, req.type);
  EXPECT_STREQ("srv", req.server_addr.device);
  EXPECT_EQ(2U, req.server_addr.entity_depth);
  cJSON_Delete(json);
}

TEST_F(SubBindTest, EncodeSubscribe) {
  strcpy(client.device, "cliDev");
  client.entity[0] = 0;
  client.entity_depth = 1;
  client.feature = 1;
  client.feature_present = true;
  strcpy(server.device, "srvDev");
  server.entity[0] = 0;
  server.entity_depth = 1;
  server.feature = 3;
  server.feature_present = true;

  cJSON* json = spine_sb_encode_subscribe(&client, &server, "LoadControl");
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_sb_request_decode(json, &req));
  EXPECT_EQ(SPINE_SB_REQUEST_SUBSCRIBE, req.type);
  EXPECT_STREQ("srvDev", req.server_addr.device);
  cJSON_Delete(json);
}

TEST_F(SubBindTest, EncodeBind) {
  strcpy(client.device, "cliDev");
  client.entity[0] = 0;
  client.entity_depth = 1;
  client.feature = 1;
  client.feature_present = true;
  strcpy(server.device, "srvDev");
  server.entity[0] = 0;
  server.entity_depth = 1;
  server.feature = 3;
  server.feature_present = true;

  cJSON* json = spine_sb_encode_bind(&client, &server, NULL);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_sb_request_decode(json, &req));
  EXPECT_EQ(SPINE_SB_REQUEST_BIND, req.type);
  cJSON_Delete(json);
}

TEST_F(SubBindTest, DeleteSubscribeEncodeDecode) {
  strcpy(client.device, "cli");
  client.feature = 1;
  client.feature_present = true;
  strcpy(server.device, "srv");
  server.feature = 3;
  server.feature_present = true;

  cJSON* json = spine_sb_encode_delete_subscribe(&client, &server);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_sb_delete_decode(json, &del));
  EXPECT_EQ(SPINE_SB_REQUEST_SUBSCRIBE, del.type);
  EXPECT_STREQ("srv", del.server_addr.device);
  cJSON_Delete(json);
}

TEST_F(SubBindTest, DeleteBindEncode) {
  strcpy(client.device, "cli");
  client.feature = 1;
  client.feature_present = true;
  strcpy(server.device, "srv");
  server.feature = 3;
  server.feature_present = true;

  cJSON* json = spine_sb_encode_delete_bind(&client, &server);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_sb_delete_decode(json, &del));
  EXPECT_EQ(SPINE_SB_REQUEST_BIND, del.type);
  cJSON_Delete(json);
}
