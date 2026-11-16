// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/spine_device.h"
#include "src/spine/spine_device_internal.h"
#include "src/spine/spine_router.h"
}

static int g_send_called = 0;
static void test_send_fn(const char* session_id, const char* json, void* ud) {
  g_send_called++;
  (void)session_id; (void)json; (void)ud;
}

class DeviceTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr;
    remote = nullptr;
    router = nullptr;
    g_send_called = 0;
  }
  void TearDown() override {
    if (remote) spine_device_destroy(remote);
    if (local) spine_device_destroy(local);
    if (router) spine_router_destroy(router);
  }

  void SetupRouter() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "localDev", "localSKI", "MyDevice"));
    ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  }

  spine_device_t* local;
  spine_device_t* remote;
  spine_router_t* router;
};

TEST_F(DeviceTest, CreateLocalDevice) {
  EXPECT_EQ(SPINE_OK, spine_device_create_local(&local, "deviceId", "testSKI", "MyDevice"));
  ASSERT_NE(nullptr, local);
  EXPECT_FALSE(spine_device_is_remote(local));
}

TEST_F(DeviceTest, CreateRemoteDevice) {
  SetupRouter();
  EXPECT_EQ(SPINE_OK, spine_device_create_remote(&remote, "peerSKI", router, local, "session1"));
  ASSERT_NE(nullptr, remote);
  EXPECT_TRUE(spine_device_is_remote(remote));
}

TEST_F(DeviceTest, AddEntityAndFeature) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "testSKI", "MyDevice"));
  uint8_t entity_path[] = {0};
  EXPECT_EQ(SPINE_OK, spine_device_add_entity(local, entity_path, 1, "CEM"));

  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 0;
  addr.entity_depth = 1;
  addr.feature = 1;
  EXPECT_EQ(SPINE_OK, spine_device_add_feature(local, &addr, "NodeManagement", "special", NULL, NULL));
}

TEST_F(DeviceTest, AddFeatureFunction) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "MyDevice"));
  uint8_t entity_path[] = {0};
  spine_device_add_entity(local, entity_path, 1, "CEM");

  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 0;
  addr.entity_depth = 1;
  addr.feature = 1;
  spine_device_add_feature(local, &addr, "NodeManagement", "special", NULL, NULL);

  EXPECT_EQ(SPINE_OK, spine_device_add_feature_function(
      local, &addr, "nodeManagementDetailedDiscoveryData", true, false, false, false));
}

TEST_F(DeviceTest, FindFeature) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "MyDevice"));
  uint8_t entity_path[] = {0};
  spine_device_add_entity(local, entity_path, 1, "CEM");

  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 0;
  addr.entity_depth = 1;
  addr.feature = 1;
  spine_device_add_feature(local, &addr, "LoadControl", "server", NULL, NULL);

  EXPECT_NE(nullptr, spine_device_find_feature(local, &addr));
}

TEST_F(DeviceTest, GetSetSessionId) {
  SetupRouter();
  ASSERT_EQ(SPINE_OK, spine_device_create_remote(&remote, "peer", router, local, "session1"));
  EXPECT_STREQ("session1", spine_device_get_session_id(remote));
  spine_device_set_session_id(remote, "session2");
  EXPECT_STREQ("session2", spine_device_get_session_id(remote));
}

TEST_F(DeviceTest, GetSKI) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "mySKI", "MyDevice"));
  EXPECT_STREQ("mySKI", spine_device_get_ski(local));
}

TEST_F(DeviceTest, GetId) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "myDeviceId", "sk", "MyDevice"));
  EXPECT_STREQ("myDeviceId", spine_device_get_id(local));
}

TEST_F(DeviceTest, AddUseCase) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "MyDevice"));
  uint8_t entity_path[] = {0};
  spine_device_add_entity(local, entity_path, 1, "CEM");

  spine_nm_uc_entry_t entry;
  memset(&entry, 0, sizeof(entry));
  strcpy(entry.actor, "CEM");
  strcpy(entry.use_case_name, "LPC");
  strcpy(entry.use_case_version, "1.0.0");
  entry.available = true;
  EXPECT_EQ(SPINE_OK, spine_device_add_usecase(local, &entry));
}

TEST_F(DeviceTest, FeatureCount) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "MyDevice"));
  /* local device auto-registers entity[0] + NM feature (feature=0 on entity[0]) */
  uint8_t entity_path[] = {1};
  spine_device_add_entity(local, entity_path, 1, "CEM");

  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 1;
  addr.entity_depth = 1;
  addr.feature = 1;
  spine_device_add_feature(local, &addr, "LoadControl", "server", NULL, NULL);

  EXPECT_EQ(2U, spine_device_feature_count(local));
}

TEST_F(DeviceTest, CreateLocalNullOut) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_create_local(nullptr, "d", "sk", "t"));
  EXPECT_EQ(SPINE_OK, spine_device_create_local(&local, nullptr, "sk", "t")); /* NULL device_id is accepted */
  spine_device_destroy(local);
  local = nullptr;
}

TEST_F(DeviceTest, AddEntityNullPath) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "d", "sk", "t"));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_add_entity(local, nullptr, 1, "type"));
}

TEST_F(DeviceTest, AddFeatureFunctionNotFound) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "d", "sk", "t"));
  uint8_t entity_path[] = {0};
  spine_device_add_entity(local, entity_path, 1, "CEM");
  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 0;
  addr.entity_depth = 1;
  addr.feature = 99;
  EXPECT_EQ(SPINE_ERR_NOT_FOUND, spine_device_add_feature_function(
      local, &addr, "someFunction", true, false, false, false));
}

TEST_F(DeviceTest, DispatchInvalidJson) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "t"));
  EXPECT_EQ(SPINE_ERR_PARSE, spine_device_dispatch(local, "{invalid json}"));
}

TEST_F(DeviceTest, DispatchNullJson) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "t"));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_dispatch(local, nullptr));
}

TEST_F(DeviceTest, SendLocalReturnsError) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "t"));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_send(local, nullptr));
}

TEST_F(DeviceTest, CacheSetGet) {
  SetupRouter();
  ASSERT_EQ(SPINE_OK, spine_device_create_remote(&remote, "peerSKI", router, local, "s1"));
  int data = 42;
  spine_device_set_cache(remote, SPINE_CACHE_LC_EG, &data, nullptr);
  void* got = spine_device_get_cache(remote, SPINE_CACHE_LC_EG);
  EXPECT_EQ(&data, got);
  /* Invalid slot returns NULL */
  EXPECT_EQ(nullptr, spine_device_get_cache(remote, 99));
}

TEST_F(DeviceTest, DestroyNullIsSafe) {
  spine_device_destroy(nullptr);
}

TEST_F(DeviceTest, GetEntityType) {
  ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "dev1", "sk", "MyDevice"));
  /* entity[0] is auto-registered as "DeviceInformation" by device creation */
  uint8_t entity_path[] = {1};
  spine_device_add_entity(local, entity_path, 1, "CEM");

  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  addr.entity[0] = 1;
  addr.entity_depth = 1;
  EXPECT_STREQ("CEM", spine_device_get_entity_type(local, &addr));
}
