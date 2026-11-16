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

static int g_send_count = 0;
static char g_last_json[8192];
static int cb_data = 0;

static void test_send_fn(const char* session_id, const char* json, void* ud) {
  (void)session_id; (void)ud;
  g_send_count++;
  snprintf(g_last_json, sizeof(g_last_json), "%s", json);
}

static void peer_ready_fn(spine_device_t* dev,
                           const spine_nm_uc_entry_t* uc_infos,
                           uint8_t uc_count, void* ud) {
  (void)dev; (void)uc_infos; (void)uc_count; (void)ud;
  cb_data = 1;
}

static void peer_changed_fn(spine_device_t* dev, void* ud) {
  (void)dev; (void)ud;
  cb_data = 2;
}

class NmIntgTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr; router = nullptr;
    g_send_count = 0;
    g_last_json[0] = '\0';
    cb_data = 0;
  }
  void TearDown() override {
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupAll() {
    ASSERT_EQ(SPINE_OK,
              spine_device_create_local(&local, "localDev", "localSKI",
                                         "EnergyManagementSystem"));
    ASSERT_EQ(SPINE_OK, spine_router_create(&router, local, test_send_fn, nullptr));
  }

  void AddUseCase(const char* name, bool available) {
    spine_addr_t ua;
    spine_addr_clear(&ua);
    strcpy(ua.device, "localDev");
    spine_nm_uc_entry_t uc;
    memset(&uc, 0, sizeof(uc));
    strcpy(uc.actor, "EnergyGuard");
    strcpy(uc.use_case_name, name);
    uc.address = ua;
    uc.available = available;
    spine_device_add_usecase(local, &uc);
  }

  spine_device_t* local;
  spine_router_t* router;
};

/* ── Device lifecycle ────────────────────────────────────────────────── */

TEST_F(NmIntgTest, CreateDevice) {
  ASSERT_EQ(SPINE_OK,
            spine_device_create_local(&local, "localDev", "localSKI",
                                       "EnergyManagementSystem"));
}

TEST_F(NmIntgTest, DestroyDevice) {
  ASSERT_EQ(SPINE_OK,
            spine_device_create_local(&local, "localDev", "localSKI",
                                       "EnergyManagementSystem"));
  spine_device_destroy(local);
  local = nullptr;
}

/* ── Use case availability ──────────────────────────────────────────── */

TEST_F(NmIntgTest, SetUseCaseAvailableNoChange) {
  SetupAll();
  AddUseCase("testUC", true);
  EXPECT_EQ(SPINE_OK,
            spine_device_set_usecase_available(local, "testUC", true));
}

TEST_F(NmIntgTest, SetUseCaseAvailableChanged) {
  SetupAll();
  AddUseCase("testUC", true);
  EXPECT_EQ(SPINE_OK,
            spine_device_set_usecase_available(local, "testUC", false));
}

TEST_F(NmIntgTest, SetUseCaseAvailableNotFound) {
  SetupAll();
  EXPECT_EQ(SPINE_ERR_NOT_FOUND,
            spine_device_set_usecase_available(local, "nonexistent", true));
}

TEST_F(NmIntgTest, SetUseCaseAvailableNull) {
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_set_usecase_available(nullptr, "testUC", true));
}

/* ── NM notify (OK even on local; no subscribers so no actual send) ─── */

TEST_F(NmIntgTest, NotifyDiscovery) {
  SetupAll();
  EXPECT_EQ(SPINE_OK, spine_device_notify_discovery(local));
}

TEST_F(NmIntgTest, NotifyUseCases) {
  SetupAll();
  AddUseCase("testUC", true);
  EXPECT_EQ(SPINE_OK, spine_device_notify_usecases(local));
}

TEST_F(NmIntgTest, NotifyDiscoveryNull) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_notify_discovery(nullptr));
}

TEST_F(NmIntgTest, NotifyUseCasesNull) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_notify_usecases(nullptr));
}

/* ── NM send operations (reject on local device) ────────────────────── */

TEST_F(NmIntgTest, StartDiscoveryRejectedOnLocal) {
  SetupAll();
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_start_discovery(local));
}

TEST_F(NmIntgTest, StartDiscoveryNull) {
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_start_discovery(nullptr));
}

TEST_F(NmIntgTest, SubscribeRejectedOnLocal) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_subscribe(local, &client, &server));
}

TEST_F(NmIntgTest, SubscribeNull) {
  SetupAll();
  spine_addr_t a; spine_addr_clear(&a);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_subscribe(nullptr, &a, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_subscribe(local, nullptr, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_subscribe(local, &a, nullptr));
}

TEST_F(NmIntgTest, BindRejectedOnLocal) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_bind(local, &client, &server));
}

TEST_F(NmIntgTest, BindNull) {
  SetupAll();
  spine_addr_t a; spine_addr_clear(&a);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_bind(nullptr, &a, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_bind(local, nullptr, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_bind(local, &a, nullptr));
}

TEST_F(NmIntgTest, UnsubscribeRejectedOnLocal) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unsubscribe(local, &client, &server));
}

TEST_F(NmIntgTest, UnbindRejectedOnLocal) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unbind(local, &client, &server));
}

TEST_F(NmIntgTest, UnsubscribeNull) {
  SetupAll();
  spine_addr_t a; spine_addr_clear(&a);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unsubscribe(nullptr, &a, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unsubscribe(local, nullptr, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unsubscribe(local, &a, nullptr));
}

TEST_F(NmIntgTest, UnbindNull) {
  SetupAll();
  spine_addr_t a; spine_addr_clear(&a);
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unbind(nullptr, &a, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unbind(local, nullptr, &a));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_unbind(local, &a, nullptr));
}

/* ── Peer ready / changed callbacks ─────────────────────────────────── */

TEST_F(NmIntgTest, PeerReadyCb) {
  SetupAll();
  cb_data = 0;
  EXPECT_EQ(SPINE_OK,
            spine_device_add_peer_ready_cb(local, peer_ready_fn, &cb_data));
}

TEST_F(NmIntgTest, PeerChangedCb) {
  SetupAll();
  cb_data = 0;
  EXPECT_EQ(SPINE_OK,
            spine_device_add_peer_changed_cb(local, peer_changed_fn, &cb_data));
}

TEST_F(NmIntgTest, PeerReadyCbNull) {
  SetupAll();
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_peer_ready_cb(local, nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_peer_ready_cb(nullptr, peer_ready_fn, nullptr));
}

TEST_F(NmIntgTest, PeerChangedCbNull) {
  SetupAll();
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_peer_changed_cb(local, nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_peer_changed_cb(nullptr, peer_changed_fn, nullptr));
}

/* ── Query methods ──────────────────────────────────────────────────── */

TEST_F(NmIntgTest, HasBinding) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_FALSE(spine_device_has_binding(local, &client, &server));
}

TEST_F(NmIntgTest, HasSubscription) {
  SetupAll();
  spine_addr_t client, server;
  spine_addr_clear(&client);
  spine_addr_clear(&server);
  EXPECT_FALSE(spine_device_has_subscription(local, &client, &server));
}

TEST_F(NmIntgTest, HasBindingToServer) {
  SetupAll();
  spine_addr_t server;
  spine_addr_clear(&server);
  EXPECT_FALSE(spine_device_has_binding_to_server(local, &server));
}

TEST_F(NmIntgTest, HasBindingToServerNull) {
  SetupAll();
  EXPECT_FALSE(spine_device_has_binding_to_server(local, nullptr));
}

TEST_F(NmIntgTest, GetEntityTypeRoot) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "localDev");
  addr.entity[0] = 0;
  addr.entity_depth = 1;
  EXPECT_STREQ("DeviceInformation",
               spine_device_get_entity_type(local, &addr));
}

TEST_F(NmIntgTest, GetEntityTypeNotFound) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "localDev");
  addr.entity[0] = 99;
  addr.entity_depth = 1;
  EXPECT_STREQ("", spine_device_get_entity_type(local, &addr));
}

TEST_F(NmIntgTest, GetEntityTypeNull) {
  SetupAll();
  EXPECT_STREQ("", spine_device_get_entity_type(nullptr, nullptr));
}

TEST_F(NmIntgTest, NextMsgCounter) {
  SetupAll();
  uint64_t c1 = spine_device_next_msg_counter(local);
  uint64_t c2 = spine_device_next_msg_counter(local);
  EXPECT_EQ(c1 + 1, c2);
}

TEST_F(NmIntgTest, SessionId) {
  SetupAll();
  const char* sid = spine_device_get_session_id(local);
  ASSERT_NE(nullptr, sid);
  EXPECT_STREQ("", sid);
}

TEST_F(NmIntgTest, SetSessionId) {
  SetupAll();
  spine_device_set_session_id(local, "customSession");
  EXPECT_STREQ("customSession", spine_device_get_session_id(local));
}

TEST_F(NmIntgTest, IsRemote) {
  SetupAll();
  EXPECT_FALSE(spine_device_is_remote(local));
}

TEST_F(NmIntgTest, GetIdAndSki) {
  SetupAll();
  EXPECT_STREQ("localDev", spine_device_get_id(local));
  EXPECT_STREQ("localSKI", spine_device_get_ski(local));
}

/* ── Cache slots ────────────────────────────────────────────────────── */

TEST_F(NmIntgTest, CacheSlots) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_get_cache(local, 0));
  int val = 42;
  spine_device_set_cache(local, 0, &val, nullptr);
  EXPECT_EQ(&val, spine_device_get_cache(local, 0));
  spine_device_set_cache(local, 0, nullptr, nullptr);
  EXPECT_EQ(nullptr, spine_device_get_cache(local, 0));
}

TEST_F(NmIntgTest, CacheSlotsOutOfRange) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_get_cache(local, SPINE_CACHE_SLOT_MAX));
  spine_device_set_cache(local, SPINE_CACHE_SLOT_MAX, nullptr, nullptr);
}

/* ── Feature queries ────────────────────────────────────────────────── */

TEST_F(NmIntgTest, FeatureCount) {
  SetupAll();
  EXPECT_EQ(1, spine_device_feature_count(local));
}

TEST_F(NmIntgTest, GetFeature) {
  SetupAll();
  const spine_feature_info_t* fi = spine_device_get_feature(local, 0);
  ASSERT_NE(nullptr, fi);
  EXPECT_STREQ("NodeManagement", fi->feature_type);
}

TEST_F(NmIntgTest, GetFeatureOutOfRange) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_get_feature(local, 99));
}

TEST_F(NmIntgTest, GetFeatureNull) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_get_feature(nullptr, 0));
}

/* ── Use case queries ───────────────────────────────────────────────── */

TEST_F(NmIntgTest, GetUseCases) {
  SetupAll();
  AddUseCase("uc1", true);
  AddUseCase("uc2", true);
  uint8_t count = 0;
  const spine_nm_uc_entry_t* ucs = spine_device_get_usecases(local, &count);
  ASSERT_NE(nullptr, ucs);
  EXPECT_EQ(2, count);
}

TEST_F(NmIntgTest, GetUseCasesNoEntries) {
  SetupAll();
  uint8_t count = 99;
  const spine_nm_uc_entry_t* ucs = spine_device_get_usecases(local, &count);
  ASSERT_NE(nullptr, ucs);
  EXPECT_EQ(0, count);
}

TEST_F(NmIntgTest, GetUseCasesNull) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_get_usecases(nullptr, nullptr));
  uint8_t count;
  EXPECT_EQ(nullptr, spine_device_get_usecases(nullptr, &count));
}

/* ── Entity / Feature registration ──────────────────────────────────── */

TEST_F(NmIntgTest, AddEntityAndFeature) {
  SetupAll();
  static const uint8_t path[] = {1, 2};
  ASSERT_EQ(SPINE_OK,
            spine_device_add_entity(local, path, 2, "SubDevice"));
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "localDev");
  addr.entity[0] = 1; addr.entity[1] = 2;
  addr.entity_depth = 2;
  addr.feature = 0;
  ASSERT_EQ(SPINE_OK,
            spine_device_add_feature(local, &addr, "MyFeature", "myRole",
                                      nullptr, nullptr));
  ASSERT_EQ(SPINE_OK,
            spine_device_add_feature_function(local, &addr,
              "myFunction", true, false, true, false));
  const spine_feature_info_t* fi = spine_device_find_feature(local, &addr);
  ASSERT_NE(nullptr, fi);
  EXPECT_STREQ("MyFeature", fi->feature_type);
}

TEST_F(NmIntgTest, AddDuplicateFeatureIdempotent) {
  SetupAll();
  static const uint8_t path[] = {1};
  ASSERT_EQ(SPINE_OK,
            spine_device_add_entity(local, path, 1, "CEM"));
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "localDev");
  addr.entity[0] = 1; addr.entity_depth = 1; addr.feature = 0;
  ASSERT_EQ(SPINE_OK,
            spine_device_add_feature(local, &addr, "FeatA", "role",
                                      nullptr, nullptr));
  /* second add with same address is idempotent — does NOT overwrite */
  ASSERT_EQ(SPINE_OK,
            spine_device_add_feature(local, &addr, "FeatB", "role",
                                      nullptr, nullptr));
  const spine_feature_info_t* fi = spine_device_find_feature(local, &addr);
  ASSERT_NE(nullptr, fi);
  EXPECT_STREQ("FeatA", fi->feature_type);
}

TEST_F(NmIntgTest, AddFeatureMax) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "localDev");
  addr.entity[0] = 1; addr.entity_depth = 1;
  static const uint8_t path[] = {1};
  ASSERT_EQ(SPINE_OK, spine_device_add_entity(local, path, 1, "CEM"));
  for (int i = 1; i < SPINE_FEATURE_MAX + 1; i++) {
    addr.feature = (uint8_t)i;
    spine_err_t rc = spine_device_add_feature(local, &addr, "F", "r", nullptr, nullptr);
    if (i < SPINE_FEATURE_MAX) {
      EXPECT_EQ(SPINE_OK, rc) << "i=" << i;
    } else {
      EXPECT_EQ(SPINE_ERR_OVERFLOW, rc) << "i=" << i;
    }
  }
}

TEST_F(NmIntgTest, AddEntityMax) {
  SetupAll();
  uint8_t path[SPINE_ENTITY_DEPTH_MAX] = {0};
  for (int i = 1; i < SPINE_ENTITY_MAX + 1; i++) {
    path[0] = (uint8_t)i;
    spine_err_t rc = spine_device_add_entity(local, path, 1, "E");
    if (i < SPINE_ENTITY_MAX) {
      EXPECT_EQ(SPINE_OK, rc) << "i=" << i;
    } else {
      EXPECT_EQ(SPINE_ERR_OVERFLOW, rc) << "i=" << i;
    }
  }
}

TEST_F(NmIntgTest, AddEntityInvalid) {
  SetupAll();
  static const uint8_t path[] = {1};
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_entity(nullptr, path, 1, "E"));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_entity(local, nullptr, 0, "E"));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_entity(local, path, 0, "E"));
}

TEST_F(NmIntgTest, AddFeatureInvalidArgs) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature(nullptr, &addr, "F", "r", nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature(local, nullptr, "F", "r", nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature(local, &addr, nullptr, "r", nullptr, nullptr));
}

TEST_F(NmIntgTest, AddUseCaseInvalidArgs) {
  SetupAll();
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_add_usecase(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_device_add_usecase(local, nullptr));
}

TEST_F(NmIntgTest, AddUseCaseDuplicate) {
  SetupAll();
  spine_addr_t ua;
  spine_addr_clear(&ua);
  spine_nm_uc_entry_t uc;
  memset(&uc, 0, sizeof(uc));
  strcpy(uc.actor, "A");
  strcpy(uc.use_case_name, "dupUC");
  uc.address = ua;
  uc.available = true;
  EXPECT_EQ(SPINE_OK, spine_device_add_usecase(local, &uc));
  EXPECT_EQ(SPINE_OK, spine_device_add_usecase(local, &uc));
  uint8_t count = 0;
  spine_device_get_usecases(local, &count);
  EXPECT_EQ(1, count) << "duplicate should be idempotent";
}

TEST_F(NmIntgTest, AddUseCaseMax) {
  SetupAll();
  spine_addr_t ua;
  spine_addr_clear(&ua);
  int i;
  for (i = 0; i < SPINE_NM_UC_MAX + 1; i++) {
    char name[64];
    snprintf(name, sizeof(name), "uc%d", i);
    spine_nm_uc_entry_t uc;
    memset(&uc, 0, sizeof(uc));
    strcpy(uc.use_case_name, name);
    uc.address = ua;
    uc.available = true;
    spine_err_t rc = spine_device_add_usecase(local, &uc);
    if (i < SPINE_NM_UC_MAX) {
      EXPECT_EQ(SPINE_OK, rc) << "i=" << i;
    } else {
      EXPECT_EQ(SPINE_ERR_OVERFLOW, rc) << "i=" << i;
    }
  }
}

TEST_F(NmIntgTest, AddFeatureFunctionInvalidArgs) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature_function(nullptr, &addr, "f", true,
                                                false, false, false));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature_function(local, nullptr, "f", true,
                                                false, false, false));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_add_feature_function(local, &addr, nullptr, true,
                                                false, false, false));
}

TEST_F(NmIntgTest, FindFeatureNotFound) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "nonexistent");
  EXPECT_EQ(nullptr, spine_device_find_feature(local, &addr));
}

TEST_F(NmIntgTest, FindFeatureNull) {
  SetupAll();
  EXPECT_EQ(nullptr, spine_device_find_feature(nullptr, nullptr));
}

TEST_F(NmIntgTest, FindFeatureWrongDevice) {
  SetupAll();
  spine_addr_t addr;
  spine_addr_clear(&addr);
  strcpy(addr.device, "wrongDev");
  addr.entity[0] = 0; addr.entity_depth = 0; addr.feature = 0;
  EXPECT_EQ(nullptr, spine_device_find_feature(local, &addr));
}

/* ── Notify (no subscribers → returns OK without sending) ───────────── */

TEST_F(NmIntgTest, NotifyReturnsOk) {
  SetupAll();
  spine_addr_t server;
  spine_addr_clear(&server);
  strcpy(server.device, "localDev");
  server.entity[0] = 0; server.entity_depth = 0; server.feature = 0;
  spine_addr_t src, dst;
  spine_addr_clear(&src);
  spine_addr_clear(&dst);
  strcpy(dst.device, "localDev");
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 1, false,
                     "testFunction", nullptr);
  EXPECT_EQ(SPINE_OK,
            spine_device_notify(local, &server, &msg));
}

TEST_F(NmIntgTest, NotifyNull) {
  SetupAll();
  spine_addr_t server;
  spine_addr_clear(&server);
  spine_msg_t msg;
  spine_make_msg_req(&msg, nullptr, nullptr, SPINE_CMD_NOTIFY, 1, false,
                     "testFunction", nullptr);
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_notify(nullptr, &server, &msg));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_notify(local, nullptr, &msg));
  EXPECT_EQ(SPINE_ERR_INVALID,
            spine_device_notify(local, &server, nullptr));
}
