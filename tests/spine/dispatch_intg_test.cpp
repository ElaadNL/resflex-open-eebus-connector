// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/spine_device.h"
#include "src/spine/spine_device_internal.h"
#include "src/spine/spine_router.h"
#include "src/spine/codec/spine_codec_datagram.h"
#include "src/spine/codec/spine_codec_msg.h"
#include "src/spine/codec/spine_codec_address.h"
#include "src/spine/codec/spine_codec_node_mgmt.h"
#include "src/spine/codec/spine_codec_sub_bind.h"
#include "src/spine/codec/spine_codec_result.h"
}

static int g_send_count = 0;
static char g_last_json[16384];

static void test_send_fn(const char* session_id, const char* json, void* ud) {
  (void)session_id; (void)ud;
  g_send_count++;
  snprintf(g_last_json, sizeof(g_last_json), "%s", json);
}

class DispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr; remote = nullptr; router = nullptr;
    g_send_count = 0; g_last_json[0] = '\0';
  }
  void TearDown() override {
    if (remote) spine_device_destroy(remote);
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupFull() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(
        &local, "localDev", "localSKI", "ControllableSystem"));
    static const uint8_t path[] = {1};
    ASSERT_EQ(SPINE_OK, spine_device_add_entity(local, path, 1, "CEM"));
    spine_addr_t addr;
    spine_addr_clear(&addr);
    strcpy(addr.device, "localDev");
    addr.entity[0] = 1; addr.entity_depth = 1; addr.feature = 0;
    ASSERT_EQ(SPINE_OK, spine_device_add_feature(
        local, &addr, "MyActuator", "server", nullptr, nullptr));
    ASSERT_EQ(SPINE_OK, spine_device_add_feature_function(
        local, &addr, "myData", true, false, false, false));
    ASSERT_EQ(SPINE_OK, spine_router_create(
        &router, local, test_send_fn, nullptr));
    ASSERT_EQ(SPINE_OK, spine_device_create_remote(
        &remote, "peerSKI", router, local, "session1"));
  }

  char* build_datagram(const spine_msg_t* msg) {
    cJSON* root = spine_datagram_encode(msg);
    if (!root) return nullptr;
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
  }

  spine_device_t* local;
  spine_device_t* remote;
  spine_router_t* router;
};

/* ═════════════════════════════════════════════════════════════════════
 * NM READ dispatch tests
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmDiscoveryRead) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 100, false,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "nodeManagementDetailedDiscoveryData") != nullptr);
  free(json);
}

TEST_F(DispatchTest, NmDiscoveryReadWithAck) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 101, true,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmUseCaseReadNoEntries) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 102, false,
                     "nodeManagementUseCaseData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmSubscriptionDataRead) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 103, false,
                     "nodeManagementSubscriptionData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmBindingDataRead) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 104, false,
                     "nodeManagementBindingData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM CALL dispatch tests (subscribe / bind)
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmSubscribeCall) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;
  cJSON* fn_data = spine_sb_encode_subscribe(&client, &server, "MyActuator");
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 200, true,
                     "nodeManagementSubscriptionRequestCall", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmBindCall) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;
  cJSON* fn_data = spine_sb_encode_bind(&client, &server, "MyActuator");
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 201, true,
                     "nodeManagementBindingRequestCall", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmSubscribeDeleteCall) {
  SetupFull();
  /* First add a subscription normally */
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;
  cJSON* add_data = spine_sb_encode_subscribe(&client, &server, "MyActuator");
  ASSERT_NE(nullptr, add_data);
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t add_msg;
  spine_make_msg_req(&add_msg, &src, &dst, SPINE_CMD_CALL, 210, true,
                     "nodeManagementSubscriptionRequestCall", add_data);
  char* add_json = build_datagram(&add_msg);
  cJSON_Delete(add_data);
  ASSERT_NE(nullptr, add_json);
  spine_device_dispatch(remote, add_json);
  free(add_json);

  /* Now send a delete */
  cJSON* del_data = spine_sb_encode_delete_subscribe(&client, &server);
  ASSERT_NE(nullptr, del_data);
  spine_msg_t del_msg;
  spine_make_msg_req(&del_msg, &src, &dst, SPINE_CMD_CALL, 211, true,
                     "nodeManagementSubscriptionDeleteCall", del_data);
  char* del_json = build_datagram(&del_msg);
  cJSON_Delete(del_data);
  ASSERT_NE(nullptr, del_json);
  int send_before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, del_json));
  EXPECT_LT(send_before, g_send_count);
  free(del_json);
}

TEST_F(DispatchTest, NmBindDeleteCall) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;
  cJSON* add_data = spine_sb_encode_bind(&client, &server, "MyActuator");
  ASSERT_NE(nullptr, add_data);
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t add_msg;
  spine_make_msg_req(&add_msg, &src, &dst, SPINE_CMD_CALL, 220, true,
                     "nodeManagementBindingRequestCall", add_data);
  char* add_json = build_datagram(&add_msg);
  cJSON_Delete(add_data);
  ASSERT_NE(nullptr, add_json);
  spine_device_dispatch(remote, add_json);
  free(add_json);

  cJSON* del_data = spine_sb_encode_delete_bind(&client, &server);
  ASSERT_NE(nullptr, del_data);
  spine_msg_t del_msg;
  spine_make_msg_req(&del_msg, &src, &dst, SPINE_CMD_CALL, 221, true,
                     "nodeManagementBindingDeleteCall", del_data);
  char* del_json = build_datagram(&del_msg);
  cJSON_Delete(del_data);
  ASSERT_NE(nullptr, del_json);
  int send_before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, del_json));
  EXPECT_LT(send_before, g_send_count);
  free(del_json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM dispatch error edge cases
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmFunctionNotSupported) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 300, false,
                     "nonExistentFunction", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "errorNumber") != nullptr ||
              strstr(g_last_json, "cmdNotSupported") != nullptr);
  free(json);
}

TEST_F(DispatchTest, NmDestinationFeatureNotFound) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 99);  /* feature 99 doesn't exist */
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 400, false,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_ERR_NOT_FOUND, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(DispatchTest, NmNotifyWithAck) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 500, true,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(DispatchTest, NmNotifyWithoutAck) {
  SetupFull();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 501, false,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_EQ(before, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmCallWithoutAck) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;
  cJSON* fn_data = spine_sb_encode_subscribe(&client, &server, "MyActuator");
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 600, false,
                     "nodeManagementSubscriptionRequestCall", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* NM handler always sends RESULT for CALL, even without ack_request */
  EXPECT_LT(before, g_send_count);
  free(json);
}

TEST_F(DispatchTest, DispatchNull) {
  SetupFull();
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_dispatch(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_dispatch(remote, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_dispatch(nullptr, "{}"));
}

TEST_F(DispatchTest, DispatchInvalidJson) {
  SetupFull();
  EXPECT_EQ(SPINE_ERR_PARSE, spine_device_dispatch(remote, "not json"));
}

TEST_F(DispatchTest, DispatchEmptyJson) {
  SetupFull();
  EXPECT_EQ(SPINE_ERR_PARSE, spine_device_dispatch(remote, "{}"));
}

TEST_F(DispatchTest, DispatchNoHandlerLocalDevice) {
  SetupFull();
  /* Build a valid NM discovery READ datagram */
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 700, false,
                     "nodeManagementDetailedDiscoveryData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  /* Dispatching on the local device directly should work */
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(local, json));
  /* NM handler will try to send a reply but local device rejects sends */
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * Discovery reply → NM state machine
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmDiscoveryReply) {
  SetupFull();
  /* Set discovery state machine to DISCOVERING (simulating a sent discovery READ) */
  remote->disc_state = SPINE_DISC_DISCOVERING;
  uint64_t counter = spine_device_pending_add(remote, SPINE_PENDING_DISC_DETAIL);
  ASSERT_NE(0, counter);

  /* Build a discovery response payload */
  nm_discovery_data_t disc;
  memset(&disc, 0, sizeof(disc));
  snprintf(disc.device_address, sizeof(disc.device_address), "peerDev");
  snprintf(disc.device_type, sizeof(disc.device_type), "EnergyManagementSystem");
  snprintf(disc.network_feature_set, sizeof(disc.network_feature_set), "simple");
  snprintf(disc.spec_versions[0], sizeof(disc.spec_versions[0]), "1.3.0");
  disc.spec_version_count = 1;
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 1;
  snprintf(disc.entities[0].entity_type, sizeof(disc.entities[0].entity_type), "CEM");
  disc.feature_count = 1;
  snprintf(disc.features[0].feature_addr.device,
           sizeof(disc.features[0].feature_addr.device), "peerDev");
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 1;
  disc.features[0].feature_addr.feature = 0;
  disc.features[0].feature_addr.feature_present = true;
  snprintf(disc.features[0].feature_type,
           sizeof(disc.features[0].feature_type), "MyActuator");
  snprintf(disc.features[0].role,
           sizeof(disc.features[0].role), "server");

  cJSON* fn_data = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, fn_data);

  /* Construct REPLY manually:
   *   reply.src = peer NM (device="peerDev",0,0)
   *   reply.dst = local NM (device="localDev",0,0) — matches local feature table
   *   msg_counter_ref = counter (matches pending)
   */
  spine_msg_t reply;
  memset(&reply, 0, sizeof(reply));
  spine_addr_make(&reply.header.src, "peerDev", 0, 0);
  spine_addr_make(&reply.header.dst, "localDev", 0, 0);
  reply.header.cmd = SPINE_CMD_REPLY;
  reply.header.msg_counter = 500;
  reply.header.msg_counter_ref = counter;
  reply.header.msg_counter_ref_present = true;
  reply.header.ack_request = false;
  snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
           "nodeManagementDetailedDiscoveryData");
  reply.payload.fn_data = fn_data;

  char* json = build_datagram(&reply);

  /* Verify the JSON encodes msgCounterReference */
  ASSERT_NE(nullptr, json);
  EXPECT_TRUE(strstr(json, "msgCounterReference") != nullptr)
      << "JSON: " << json;

  cJSON_Delete(fn_data);

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* Discovery reply should trigger subscribe message */
  EXPECT_LT(before, g_send_count) << "JSON: " << json;
  free(json);
}

TEST_F(DispatchTest, NmDiscoveryReplyWrongCounter) {
  SetupFull();
  uint64_t counter = spine_device_pending_add(remote, SPINE_PENDING_DISC_DETAIL);
  ASSERT_NE(0, counter);

  nm_discovery_data_t disc;
  memset(&disc, 0, sizeof(disc));
  snprintf(disc.device_address, sizeof(disc.device_address), "peerDev");
  snprintf(disc.spec_versions[0], sizeof(disc.spec_versions[0]), "1.3.0");
  disc.spec_version_count = 1;
  disc.entity_count = 0;

  cJSON* fn_data = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, fn_data);

  /* Wrong msg_counter_ref so pending doesn't match */
  spine_msg_t reply;
  memset(&reply, 0, sizeof(reply));
  spine_addr_make(&reply.header.src, "peerDev", 0, 0);
  spine_addr_make(&reply.header.dst, "localDev", 0, 0);
  reply.header.cmd = SPINE_CMD_REPLY;
  reply.header.msg_counter = 501;
  reply.header.msg_counter_ref = 99999;
  reply.header.msg_counter_ref_present = true;
  reply.header.ack_request = false;
  snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
           "nodeManagementDetailedDiscoveryData");
  reply.payload.fn_data = fn_data;

  char* json = build_datagram(&reply);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_EQ(before, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmUseCaseReply) {
  SetupFull();
  /* Set UC discovery state */
  remote->disc_state = SPINE_DISC_UC_DISCOVERING;
  uint64_t counter = spine_device_pending_add(remote, SPINE_PENDING_DISC_UC);
  ASSERT_NE(0, counter);

  /* Build a use case data payload */
  nm_uc_data_t uc_data;
  memset(&uc_data, 0, sizeof(uc_data));
  uc_data.count = 1;
  snprintf(uc_data.items[0].actor, sizeof(uc_data.items[0].actor), "EnergyGuard");
  uc_data.items[0].address.entity_depth = 1;
  uc_data.items[0].address.entity[0] = 1;
  uc_data.items[0].address.feature = 0;
  uc_data.items[0].address.feature_present = true;
  uc_data.items[0].uc_support_count = 1;
  snprintf(uc_data.items[0].uc_supports[0].use_case_name,
           sizeof(uc_data.items[0].uc_supports[0].use_case_name), "MyUC");
  snprintf(uc_data.items[0].uc_supports[0].use_case_version,
           sizeof(uc_data.items[0].uc_supports[0].use_case_version), "1.0.0");
  uc_data.items[0].uc_supports[0].use_case_available = true;

  cJSON* fn_data = nm_uc_data_encode(&uc_data);
  ASSERT_NE(nullptr, fn_data);

  /* Construct REPLY manually: dst=local NM, src=peer NM */
  spine_msg_t reply;
  memset(&reply, 0, sizeof(reply));
  spine_addr_make(&reply.header.src, "peerDev", 0, 0);
  spine_addr_make(&reply.header.dst, "localDev", 0, 0);
  reply.header.cmd = SPINE_CMD_REPLY;
  reply.header.msg_counter = 502;
  reply.header.msg_counter_ref = counter;
  reply.header.msg_counter_ref_present = true;
  reply.header.ack_request = false;
  snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
           "nodeManagementUseCaseData");
  reply.payload.fn_data = fn_data;

  char* json = build_datagram(&reply);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* UC reply does not send messages; it fires peer_ready callbacks and sets READY */
  EXPECT_EQ(SPINE_DISC_READY, remote->disc_state);
  free(json);
}

TEST_F(DispatchTest, NmSubscribeResult) {
  SetupFull();
  /* Add a pending NM_SUBSCRIBE */
  spine_addr_t client, server;
  spine_addr_make(&client, "peerDev", 0, 1);
  spine_addr_make(&server, "localDev", 1, 0);
  uint64_t counter = spine_device_pending_add_sb(remote, SPINE_PENDING_NM_SUBSCRIBE,
                                                  &client, &server);
  ASSERT_NE(0, counter);

  /* Build a RESULT datagram: dst=local NM, src=peer NM */
  spine_msg_t result;
  memset(&result, 0, sizeof(result));
  spine_addr_make(&result.header.src, "peerDev", 0, 0);
  spine_addr_make(&result.header.dst, "localDev", 0, 0);
  result.header.cmd = SPINE_CMD_RESULT;
  result.header.msg_counter = 503;
  result.header.msg_counter_ref = counter;
  result.header.msg_counter_ref_present = true;
  result.header.ack_request = false;
  snprintf(result.payload.fn_name, sizeof(result.payload.fn_name),
           "nodeManagementSubscriptionRequestCall");

  cJSON* root = spine_datagram_encode(&result);
  ASSERT_NE(nullptr, root);
  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(DispatchTest, NmRediscoveringReply) {
  SetupFull();
  remote->disc_state = SPINE_DISC_REDISCOVERING;
  uint64_t counter = spine_device_pending_add(remote, SPINE_PENDING_DISC_DETAIL);
  ASSERT_NE(0, counter);

  nm_discovery_data_t disc;
  memset(&disc, 0, sizeof(disc));
  snprintf(disc.device_address, sizeof(disc.device_address), "peerDev");
  snprintf(disc.device_type, sizeof(disc.device_type), "EnergyManagementSystem");
  snprintf(disc.network_feature_set, sizeof(disc.network_feature_set), "simple");
  snprintf(disc.spec_versions[0], sizeof(disc.spec_versions[0]), "1.3.0");
  disc.spec_version_count = 1;
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 1;
  snprintf(disc.entities[0].entity_type, sizeof(disc.entities[0].entity_type), "CEM");
  disc.feature_count = 1;
  snprintf(disc.features[0].feature_addr.device,
           sizeof(disc.features[0].feature_addr.device), "peerDev");
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 1;
  disc.features[0].feature_addr.feature = 1;
  disc.features[0].feature_addr.feature_present = true;
  snprintf(disc.features[0].feature_type,
           sizeof(disc.features[0].feature_type), "LoadControl");
  snprintf(disc.features[0].role, sizeof(disc.features[0].role), "server");

  cJSON* fn_data = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, fn_data);

  spine_msg_t reply;
  memset(&reply, 0, sizeof(reply));
  spine_addr_make(&reply.header.src, "peerDev", 0, 0);
  spine_addr_make(&reply.header.dst, "localDev", 0, 0);
  reply.header.cmd = SPINE_CMD_REPLY;
  reply.header.msg_counter = 600;
  reply.header.msg_counter_ref = counter;
  reply.header.msg_counter_ref_present = true;
  reply.header.ack_request = false;
  snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
           "nodeManagementDetailedDiscoveryData");
  reply.payload.fn_data = fn_data;

  char* json = build_datagram(&reply);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(before, g_send_count);
  EXPECT_EQ(1, remote->feature_count);
  EXPECT_EQ(1, remote->entity_count);
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM NOTIFY with entity data + peer_changed callback
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmNotifyDiscoveryFiresPeerChanged) {
  SetupFull();
  int cb_val = 0;
  spine_device_add_peer_changed_cb(local,
      +[](spine_device_t* d, void* ud) { (void)d; *(int*)ud = 2; }, &cb_val);

  nm_discovery_data_t disc;
  memset(&disc, 0, sizeof(disc));
  snprintf(disc.device_address, sizeof(disc.device_address), "peerDev");
  snprintf(disc.device_type, sizeof(disc.device_type), "EnergyManagementSystem");
  snprintf(disc.network_feature_set, sizeof(disc.network_feature_set), "simple");
  disc.spec_version_count = 0;
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 1;
  snprintf(disc.entities[0].entity_type, sizeof(disc.entities[0].entity_type), "LoadControl");
  disc.feature_count = 0;

  cJSON* fn_data = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 700, false,
                     "nodeManagementDetailedDiscoveryData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_EQ(2, cb_val);
  free(json);
}

TEST_F(DispatchTest, NmNotifyUseCaseFiresPeerChanged) {
  SetupFull();
  int cb_val = 0;
  spine_device_add_peer_changed_cb(local,
      +[](spine_device_t* d, void* ud) { (void)d; *(int*)ud = 2; }, &cb_val);

  nm_uc_data_t uc_data;
  memset(&uc_data, 0, sizeof(uc_data));
  uc_data.count = 1;
  snprintf(uc_data.items[0].actor, sizeof(uc_data.items[0].actor), "EnergyGuard");
  uc_data.items[0].address.entity_depth = 1;
  uc_data.items[0].address.entity[0] = 1;
  uc_data.items[0].uc_support_count = 1;
  snprintf(uc_data.items[0].uc_supports[0].use_case_name,
           sizeof(uc_data.items[0].uc_supports[0].use_case_name), "MyUC");
  uc_data.items[0].uc_supports[0].use_case_available = false;

  cJSON* fn_data = nm_uc_data_encode(&uc_data);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 800, false,
                     "nodeManagementUseCaseData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_EQ(2, cb_val);
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM subscribe error result
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmSubscribeErrorResult) {
  SetupFull();
  spine_addr_t client, server;
  spine_addr_make(&client, "peerDev", 0, 1);
  spine_addr_make(&server, "localDev", 1, 0);
  uint64_t counter = spine_device_pending_add_sb(remote, SPINE_PENDING_NM_SUBSCRIBE,
                                                  &client, &server);
  ASSERT_NE(0, counter);

  spine_result_data_t res;
  memset(&res, 0, sizeof(res));
  res.error_number = 7;
  snprintf(res.description, sizeof(res.description), "rejected");
  res.description_present = true;

  cJSON* fn_data = spine_result_data_encode(&res);
  ASSERT_NE(nullptr, fn_data);

  spine_msg_t result;
  memset(&result, 0, sizeof(result));
  spine_addr_make(&result.header.src, "peerDev", 0, 0);
  spine_addr_make(&result.header.dst, "localDev", 0, 0);
  result.header.cmd = SPINE_CMD_RESULT;
  result.header.msg_counter = 603;
  result.header.msg_counter_ref = counter;
  result.header.msg_counter_ref_present = true;
  result.header.ack_request = false;
  snprintf(result.payload.fn_name, sizeof(result.payload.fn_name),
           "nodeManagementSubscriptionRequestCall");
  result.payload.fn_data = fn_data;

  cJSON* root = spine_datagram_encode(&result);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, root);
  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM CALL decode failure
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmSubscribeDecodeFailure) {
  SetupFull();
  cJSON* bad_data = cJSON_CreateArray();
  ASSERT_NE(nullptr, bad_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 900, true,
                     "nodeManagementSubscriptionRequestCall", bad_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(bad_data);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM CALL feature type mismatch
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmBindFeatureTypeMismatch) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;

  cJSON* fn_data = spine_sb_encode_bind(&client, &server, "WrongType");
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 901, true,
                     "nodeManagementBindingRequestCall", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(before, g_send_count);
  free(json);
}

/* ═════════════════════════════════════════════════════════════════════
 * NM bind to NM feature rejected
 * ═════════════════════════════════════════════════════════════════════ */

TEST_F(DispatchTest, NmBindToNMRejected) {
  SetupFull();
  nm_addr_t client, server;
  memset(&client, 0, sizeof(client));
  snprintf(client.device, sizeof(client.device), "peerDev");
  client.entity_depth = 1; client.entity[0] = 0; client.feature = 1;
  client.feature_present = true;
  memset(&server, 0, sizeof(server));
  snprintf(server.device, sizeof(server.device), "localDev");
  server.entity_depth = 1; server.entity[0] = 1; server.feature = 0;
  server.feature_present = true;

  cJSON* fn_data = spine_sb_encode_bind(&client, &server, "NodeManagement");
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_CALL, 902, true,
                     "nodeManagementBindingRequestCall", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(before, g_send_count);
  free(json);
}

TEST_F(DispatchTest, NmDiscoveryNotifyUseCaseChanged) {
  SetupFull();
  /* Add a use case first */
  spine_addr_t ua;
  spine_addr_clear(&ua);
  spine_nm_uc_entry_t uc;
  memset(&uc, 0, sizeof(uc));
  strcpy(uc.actor, "EnergyGuard");
  strcpy(uc.use_case_name, "MyUC");
  uc.address = ua;
  uc.available = true;
  spine_device_add_usecase(local, &uc);

  /* Build a nodeManagementUseCaseData NOTIFY */
  nm_uc_data_t uc_data;
  memset(&uc_data, 0, sizeof(uc_data));
  uc_data.count = 1;
  snprintf(uc_data.items[0].actor, sizeof(uc_data.items[0].actor), "EnergyGuard");
  uc_data.items[0].address.entity_depth = 0;
  uc_data.items[0].uc_support_count = 1;
  snprintf(uc_data.items[0].uc_supports[0].use_case_name,
           sizeof(uc_data.items[0].uc_supports[0].use_case_name), "MyUC");
  uc_data.items[0].uc_supports[0].use_case_available = false;

  cJSON* fn_data = nm_uc_data_encode(&uc_data);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 0, 0);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 800, false,
                     "nodeManagementUseCaseData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);

  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}
