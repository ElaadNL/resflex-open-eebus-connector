// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/spine_router.h"
#include "src/spine/spine_device.h"
}

class RouterTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr;
    router = nullptr;
  }
  void TearDown() override {
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupLocalDevice() {
    ASSERT_EQ(SPINE_OK,
              spine_device_create_local(&local, "localDev", "localSKI",
                                         "ControllableSystem"));
    static const uint8_t entity_path[] = {1};
    ASSERT_EQ(SPINE_OK, spine_device_add_entity(local, entity_path, 1, "CEM"));
    spine_addr_t addr;
    spine_addr_clear(&addr);
    strcpy(addr.device, "localDev");
    addr.entity[0] = 1;
    addr.entity_depth = 1;
    addr.feature = 0;
    ASSERT_EQ(SPINE_OK,
              spine_device_add_feature(local, &addr, "NodeManagement",
                                        "special", nullptr, nullptr));
  }

  spine_device_t* local;
  spine_router_t* router;
};

static int g_send_called = 0;
static void test_send_fn(const char* session_id, const char* json, void* ud) {
  g_send_called++;
  (void)session_id; (void)json; (void)ud;
}

TEST_F(RouterTest, CreateDestroy) {
  SetupLocalDevice();
  g_send_called = 0;
  EXPECT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  ASSERT_NE(nullptr, router);
}

TEST_F(RouterTest, GetLocal) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(local, spine_router_get_local(router));
}

TEST_F(RouterTest, OnConnectDisconnect) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(SPINE_OK, spine_router_on_connect(router, "peerSKI", "session1"));
  spine_router_on_disconnect(router, "session1");
}

TEST_F(RouterTest, FindRemoteBySession) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  spine_router_on_connect(router, "peerSKI", "session1");
  spine_device_t* found = spine_router_find_remote_by_session(router, "session1");
  EXPECT_NE(nullptr, found);
  EXPECT_TRUE(spine_device_is_remote(found));
}

TEST_F(RouterTest, FindRemoteBySki) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  spine_router_on_connect(router, "peerSKI", "session1");
  spine_device_t* found = spine_router_find_remote_by_ski(router, "peerSKI");
  EXPECT_NE(nullptr, found);
}

TEST_F(RouterTest, FindRemoteById) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  spine_router_on_connect(router, "peerSKI", "session1");
  spine_device_t* found = spine_router_find_remote_by_session(router, "session1");
  ASSERT_NE(nullptr, found);
  const char* id = spine_device_get_id(found);
  EXPECT_NE(nullptr, id);
}

TEST_F(RouterTest, DispatchOnDisconnected) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(SPINE_ERR_NOT_FOUND,
            spine_router_dispatch(router, "unknownSession", "{}"));
}

TEST_F(RouterTest, ConnectDuplicateSession) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(SPINE_OK, spine_router_on_connect(router, "peerSKI", "session1"));
  /* Duplicate session_id is a no-op */
  EXPECT_EQ(SPINE_OK, spine_router_on_connect(router, "peerSKI", "session1"));
}

TEST_F(RouterTest, ConnectOverflow) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  for (int i = 0; i < SPINE_ROUTER_PEER_MAX + 1; i++) {
    char ski[32], sess[32];
    snprintf(ski, sizeof(ski), "peer%d", i);
    snprintf(sess, sizeof(sess), "s%d", i);
    spine_err_t err = spine_router_on_connect(router, ski, sess);
    if (i < SPINE_ROUTER_PEER_MAX)
      EXPECT_EQ(SPINE_OK, err);
    else
      EXPECT_EQ(SPINE_ERR_OVERFLOW, err);
  }
}

TEST_F(RouterTest, DisconnectUnknownSession) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  spine_router_on_disconnect(router, "nonexistent");
}

TEST_F(RouterTest, DispatchNullSession) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_router_dispatch(router, nullptr, "{}"));
}

TEST_F(RouterTest, FindByNullReturnsNull) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  EXPECT_EQ(nullptr, spine_router_find_remote_by_session(router, nullptr));
  EXPECT_EQ(nullptr, spine_router_find_remote_by_ski(router, nullptr));
}

TEST_F(RouterTest, CreateNullParams) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_router_create(&router, nullptr, nullptr, nullptr));
}

TEST_F(RouterTest, DestroyNullIsSafe) {
  spine_router_destroy(nullptr);
}

TEST_F(RouterTest, ForEachRemote) {
  SetupLocalDevice();
  ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  spine_router_on_connect(router, "peer1", "s1");
  spine_router_on_connect(router, "peer2", "s2");
  int count = 0;
  spine_router_foreach_remote(
      router, [](spine_device_t* dev, void* ud) { (*(int*)ud)++; }, &count);
  EXPECT_EQ(2, count);
}
