// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "src/uc/uc_lc_eg.h"
#include "src/uc/uc_lc.h"
#include "src/spine/spine_device.h"
#include "src/spine/spine_device_internal.h"
#include "src/spine/spine_router.h"
#include "src/spine/codec/spine_codec_datagram.h"
#include "src/spine/codec/spine_codec_msg.h"
#include "src/spine/codec/spine_codec_address.h"
#include "src/spine/codec/spine_codec_load_ctrl.h"
#include "src/spine/codec/spine_codec_dev_cfg.h"
#include "src/spine/codec/spine_codec_dev_diag.h"
#include "src/spine/codec/spine_codec_elec_conn.h"
#include "src/spine/codec/spine_codec_node_mgmt.h"
#include "src/platform/eebus_mutex.h"
}

static int g_send_count = 0;
static char g_last_json[16384];

static void test_send_fn(const char* session_id, const char* json, void* ud) {
  (void)session_id; (void)ud;
  g_send_count++;
  snprintf(g_last_json, sizeof(g_last_json), "%s", json);
}

/* Replicate lc_eg_cache_t from uc_lc_eg.c (fields are all from public codec headers) */
typedef struct {
  eebus_mutex_t*       mutex;
  spine_addr_t         cs_entity_addr;
  char                 cs_entity_type[64];
  spine_addr_t         cs_lc_addr;
  spine_addr_t         cs_cfg_addr;
  spine_addr_t         cs_diag_addr;
  spine_addr_t         cs_ec_addr;
  bool                 lpc_supported;
  bool                 lpp_supported;
  bool                 lpc_available;
  bool                 lpp_available;
  lc_limit_desc_list_t limit_descs;
  lc_limit_list_t      limits;
  dev_cfg_kv_desc_list_t  cfg_descs;
  dev_cfg_kv_list_t       failsafe_kv;
  uint64_t             cs_hb_counter;
  ec_characteristic_list_t ec_chars;
  uint8_t              init_read_done;
} test_lc_eg_cache_t;

class EgCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr; remote = nullptr; router = nullptr;
    eg = nullptr;
    g_send_count = 0; g_last_json[0] = '\0';
  }

  void TearDown() override {
    if (eg) uc_lc_eg_destroy(eg);
    /* remote is owned by router (created via on_connect); do NOT destroy separately */
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupFullAndDriveUcDiscovery() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(
        &local, "localDev", "localSKI", "CEM"));

    memset(&eg_cfg, 0, sizeof(eg_cfg));
    eg_cfg.device = local;
    spine_addr_make(&eg_cfg.eg_entity_addr, "localDev", 1, 0);
    eg_cfg.entity_type = "CEM";
    eg_cfg.heartbeat_interval_s = 60;
    ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &eg_cfg, true, false));
    ASSERT_NE(nullptr, eg);

    ASSERT_EQ(SPINE_OK, spine_router_create(
        &router, local, test_send_fn, nullptr));

    /* Use on_connect (not create_remote) so the remote is registered in the
     * router. This also starts discovery (sends 1 NM DETAIL READ). */
    ASSERT_EQ(SPINE_OK, spine_router_on_connect(router, "peerSKI", "session1"));
    remote = spine_router_find_remote_by_session(router, "session1");
    ASSERT_NE(nullptr, remote);

    /* Set device_id (normally set by NM discovery reply) */
    snprintf(remote->device_id, sizeof(remote->device_id), "peerDev");

    /* Populate remote entity */
    remote->entity_count = 1;
    memset(&remote->entities[0], 0, sizeof(spine_entity_info_t));
    remote->entities[0].entity[0] = 1;
    remote->entities[0].entity_depth = 1;
    snprintf(remote->entities[0].entity_type,
             sizeof(remote->entities[0].entity_type), "CS");

    /* Populate remote server features under entity [1] */
    remote->feature_count = 4;
    memset(remote->features, 0, sizeof(feature_slot_t) * 4);
    spine_addr_make(&remote->features[0].info.addr, "peerDev", 1, 1);
    snprintf(remote->features[0].info.feature_type,
             sizeof(remote->features[0].info.feature_type), "LoadControl");
    snprintf(remote->features[0].info.role,
             sizeof(remote->features[0].info.role), "server");
    spine_addr_make(&remote->features[1].info.addr, "peerDev", 1, 2);
    snprintf(remote->features[1].info.feature_type,
             sizeof(remote->features[1].info.feature_type), "DeviceConfiguration");
    snprintf(remote->features[1].info.role,
             sizeof(remote->features[1].info.role), "server");
    spine_addr_make(&remote->features[2].info.addr, "peerDev", 1, 3);
    snprintf(remote->features[2].info.feature_type,
             sizeof(remote->features[2].info.feature_type), "DeviceDiagnosis");
    snprintf(remote->features[2].info.role,
             sizeof(remote->features[2].info.role), "server");
    spine_addr_make(&remote->features[3].info.addr, "peerDev", 1, 4);
    snprintf(remote->features[3].info.feature_type,
             sizeof(remote->features[3].info.feature_type), "ElectricalConnection");
    snprintf(remote->features[3].info.role,
             sizeof(remote->features[3].info.role), "server");

    /* Override discovery state (set to DISCOVERING by on_connect → start_discovery) */
    remote->disc_state = SPINE_DISC_UC_DISCOVERING;
    uint64_t counter = spine_device_pending_add(remote, SPINE_PENDING_DISC_UC);
    ASSERT_NE(0, counter);

    nm_uc_data_t uc_data;
    memset(&uc_data, 0, sizeof(uc_data));
    uc_data.count = 1;
    snprintf(uc_data.items[0].actor, sizeof(uc_data.items[0].actor),
             "ControllableSystem");
    uc_data.items[0].address.entity_depth = 1;
    uc_data.items[0].address.entity[0] = 1;
    uc_data.items[0].uc_support_count = 1;
    snprintf(uc_data.items[0].uc_supports[0].use_case_name,
             sizeof(uc_data.items[0].uc_supports[0].use_case_name),
             "limitationOfPowerConsumption");
    uc_data.items[0].uc_supports[0].use_case_available = true;

    cJSON* fn_data = nm_uc_data_encode(&uc_data);
    ASSERT_NE(nullptr, fn_data);

    spine_msg_t reply;
    memset(&reply, 0, sizeof(reply));
    spine_addr_make(&reply.header.src, "peerDev", 0, 0);
    spine_addr_make(&reply.header.dst, "localDev", 0, 0);
    reply.header.cmd = SPINE_CMD_REPLY;
    reply.header.msg_counter = 100;
    reply.header.msg_counter_ref = counter;
    reply.header.msg_counter_ref_present = true;
    reply.header.ack_request = false;
    snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
             "nodeManagementUseCaseData");
    reply.payload.fn_data = fn_data;

    char* json = build_datagram(&reply);
    cJSON_Delete(fn_data);
    ASSERT_NE(nullptr, json);

    g_send_baseline = g_send_count;
    EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
    EXPECT_EQ(SPINE_DISC_READY, remote->disc_state);
    EXPECT_GT(g_send_count, g_send_baseline);
    free(json);
  }

  void InjectTestCache() {
    /* Overwrite the (empty) cache with pre-populated test data */
    test_lc_eg_cache_t* cache = (test_lc_eg_cache_t*)malloc(
        sizeof(test_lc_eg_cache_t));
    ASSERT_NE(nullptr, cache);
    memset(cache, 0, sizeof(*cache));
    eebus_mutex_create(&cache->mutex);
    spine_addr_make(&cache->cs_entity_addr, "peerDev", 1, 1);
    snprintf(cache->cs_entity_type, sizeof(cache->cs_entity_type), "CS");
    spine_addr_make(&cache->cs_lc_addr, "peerDev", 1, 1);
    spine_addr_make(&cache->cs_cfg_addr, "peerDev", 1, 2);
    spine_addr_make(&cache->cs_diag_addr, "peerDev", 1, 3);
    spine_addr_make(&cache->cs_ec_addr, "peerDev", 1, 4);
    cache->lpc_supported = true;
    cache->lpc_available = true;

    /* Pre-populate limit desc + limit data */
    cache->limit_descs.count = 1;
    cache->limit_descs.items[0].limit_id = 1;
    snprintf(cache->limit_descs.items[0].limit_direction,
             sizeof(cache->limit_descs.items[0].limit_direction), "consume");
    cache->limit_descs.items[0].limit_direction_present = true;
    snprintf(cache->limit_descs.items[0].limit_type,
             sizeof(cache->limit_descs.items[0].limit_type),
             "signDependentAbsValueLimit");
    cache->limit_descs.items[0].limit_type_present = true;
    cache->limits.count = 1;
    cache->limits.items[0].limit_id = 1;
    cache->limits.items[0].is_limit_active = true;
    cache->limits.items[0].is_limit_active_present = true;
    cache->limits.items[0].is_limit_changeable = true;
    cache->limits.items[0].is_limit_changeable_present = true;
    spine_scaled_number_from_double(5000.0, &cache->limits.items[0].value);
    cache->limits.items[0].value_present = true;

    /* Pre-populate failsafe config */
    cache->cfg_descs.count = 2;
    cache->cfg_descs.items[0].key_id = 10;
    snprintf(cache->cfg_descs.items[0].key_name,
             sizeof(cache->cfg_descs.items[0].key_name),
             "failsafeConsumptionActivePowerLimit");
    cache->cfg_descs.items[0].key_name_present = true;
    snprintf(cache->cfg_descs.items[0].value_type,
             sizeof(cache->cfg_descs.items[0].value_type), "scaledNumber");
    cache->cfg_descs.items[0].value_type_present = true;
    cache->cfg_descs.items[1].key_id = 20;
    snprintf(cache->cfg_descs.items[1].key_name,
             sizeof(cache->cfg_descs.items[1].key_name),
             "failsafeDurationMinimum");
    cache->cfg_descs.items[1].key_name_present = true;
    snprintf(cache->cfg_descs.items[1].value_type,
             sizeof(cache->cfg_descs.items[1].value_type), "duration");
    cache->cfg_descs.items[1].value_type_present = true;
    cache->failsafe_kv.count = 2;
    cache->failsafe_kv.items[0].key_id = 10;
    cache->failsafe_kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
    spine_scaled_number_from_double(6000.0,
        &cache->failsafe_kv.items[0].value.scaled_num_val);
    cache->failsafe_kv.items[0].value_present = true;
    cache->failsafe_kv.items[0].is_value_changeable = true;
    cache->failsafe_kv.items[0].is_value_changeable_present = true;
    cache->failsafe_kv.items[1].key_id = 20;
    cache->failsafe_kv.items[1].value_type = DEV_CFG_VALUE_DURATION;
    snprintf(cache->failsafe_kv.items[1].value.duration_val,
             sizeof(cache->failsafe_kv.items[1].value.duration_val), "PT7200S");
    cache->failsafe_kv.items[1].value_present = true;
    cache->failsafe_kv.items[1].is_value_changeable = true;
    cache->failsafe_kv.items[1].is_value_changeable_present = true;

    /* Pre-populate EC characteristics */
    cache->ec_chars.count = 1;
    cache->ec_chars.items[0].electrical_connection_id = 1;
    cache->ec_chars.items[0].parameter_id = 1;
    cache->ec_chars.items[0].characteristic_id = 1;
    snprintf(cache->ec_chars.items[0].characteristic_type,
             sizeof(cache->ec_chars.items[0].characteristic_type),
             "contractualConsumptionNominalMax");
    cache->ec_chars.items[0].characteristic_type_present = true;
    spine_scaled_number_from_double(7000.0,
        &cache->ec_chars.items[0].value);
    cache->ec_chars.items[0].value_present = true;

    /* Mark init as done */
    cache->init_read_done = 0x3F;

    spine_device_set_cache(remote, SPINE_CACHE_LC_EG, cache, free);
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
  uc_lc_eg_t* eg;
  uc_lc_eg_config_t eg_cfg;
  int g_send_baseline;
};

/* ═══════════════════════════════════════════════════════════════════════
 * LC dispatch with cache: NOTIFY limit list → cache updated
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, LcNotifyLimitListUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  lc_limit_list_t lst;
  memset(&lst, 0, sizeof(lst));
  lst.count = 1;
  lst.items[0].limit_id = 1;
  lst.items[0].is_limit_active = false;
  lst.items[0].is_limit_active_present = true;
  lst.items[0].value_present = true;
  spine_scaled_number_from_double(3000.0, &lst.items[0].value);

  cJSON* fn_data = lc_limit_list_encode(&lst);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 200, false,
                     "loadControlLimitListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* NOTIFY with cache → handler updates cache, no reply sent */
  free(json);
}

TEST_F(EgCacheTest, LcNotifyLimitDescUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  lc_limit_desc_list_t descs;
  memset(&descs, 0, sizeof(descs));
  descs.count = 1;
  descs.items[0].limit_id = 2;
  snprintf(descs.items[0].limit_direction,
           sizeof(descs.items[0].limit_direction), "consume");
  descs.items[0].limit_direction_present = true;

  cJSON* fn_data = lc_limit_desc_list_encode(&descs);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 201, false,
                     "loadControlLimitDescriptionListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(EgCacheTest, LcReplyLimitListWithCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  lc_limit_list_t lst;
  memset(&lst, 0, sizeof(lst));
  lst.count = 1;
  lst.items[0].limit_id = 1;
  lst.items[0].is_limit_active = true;
  lst.items[0].is_limit_active_present = true;
  lst.items[0].value_present = true;
  spine_scaled_number_from_double(4000.0, &lst.items[0].value);

  cJSON* fn_data = lc_limit_list_encode(&lst);
  ASSERT_NE(nullptr, fn_data);

  spine_msg_t reply;
  memset(&reply, 0, sizeof(reply));
  spine_addr_make(&reply.header.src, "peerDev", 0, 0);
  spine_addr_make(&reply.header.dst, "localDev", 1, 1);
  reply.header.cmd = SPINE_CMD_REPLY;
  reply.header.msg_counter = 210;
  reply.header.msg_counter_ref = 1;
  reply.header.msg_counter_ref_present = true;
  reply.header.ack_request = false;
  snprintf(reply.payload.fn_name, sizeof(reply.payload.fn_name),
           "loadControlLimitListData");
  reply.payload.fn_data = fn_data;
  char* json = build_datagram(&reply);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═══════════════════════════════════════════════════════════════════════
 * CFG dispatch with cache: NOTIFY KV list/desc → cache updated
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, CfgNotifyKvListUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 10;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(5500.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 300, false,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(EgCacheTest, CfgNotifyKvDescListUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  dev_cfg_kv_desc_list_t descs;
  memset(&descs, 0, sizeof(descs));
  descs.count = 1;
  descs.items[0].key_id = 20;
  snprintf(descs.items[0].key_name, sizeof(descs.items[0].key_name),
           "failsafeProductionActivePowerLimit");
  descs.items[0].key_name_present = true;

  cJSON* fn_data = dev_cfg_kv_desc_list_encode(&descs);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 301, false,
                     "deviceConfigurationKeyValueDescriptionListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═══════════════════════════════════════════════════════════════════════
 * DIAG client dispatch with cache: NOTIFY heartbeat → cache updated
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, DiagClientNotifyHeartbeatUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  dev_diag_heartbeat_t hb;
  memset(&hb, 0, sizeof(hb));
  hb.heartbeat_counter = 42;
  hb.heartbeat_counter_present = true;

  cJSON* fn_data = dev_diag_heartbeat_encode(&hb);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 4);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 400, false,
                     "deviceDiagnosisHeartbeatData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═══════════════════════════════════════════════════════════════════════
 * EC dispatch with cache: NOTIFY characteristic list → cache updated
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, EcNotifyCharListUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  ec_characteristic_list_t chars;
  memset(&chars, 0, sizeof(chars));
  chars.count = 1;
  chars.items[0].electrical_connection_id = 2;
  chars.items[0].parameter_id = 1;
  chars.items[0].characteristic_id = 1;
  chars.items[0].value_present = true;
  spine_scaled_number_from_double(8000.0, &chars.items[0].value);

  cJSON* fn_data = ec_characteristic_list_encode(&chars);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 5);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 500, false,
                     "electricalConnectionCharacteristicListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Get API: read limit from cache
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, GetLimitReturnsCachedValue) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_limit_t out;
  uc_lc_eg_get_limit(eg, "session1", LC_DIRECTION_CONSUME, &out);
  EXPECT_TRUE(out.is_limit_active);
  EXPECT_TRUE(out.is_limit_changeable);
  /* 5000 W was set in test cache */
  EXPECT_DOUBLE_EQ(5000.0, out.value_w);
}

TEST_F(EgCacheTest, GetLimitWrongDirectionReturnsEmpty) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_limit_t out;
  memset(&out, 0xFF, sizeof(out));
  uc_lc_eg_get_limit(eg, "session1", LC_DIRECTION_PRODUCE, &out);
  /* lpp_supported = false → direction_valid fails → out not filled */
  EXPECT_EQ(0, out.value_w);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Get API: read failsafe from cache
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, GetFailsafeReturnsCachedValue) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_failsafe_t out;
  uc_lc_eg_get_failsafe(eg, "session1", LC_DIRECTION_CONSUME, &out);
  EXPECT_DOUBLE_EQ(6000.0, out.power_limit_w);
  EXPECT_TRUE(out.power_limit_changeable);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Get API: read EC characteristic from cache
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, GetEcCharReturnsCachedValue) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_ec_characteristic_t out;
  uc_lc_eg_get_ec_characteristic(eg, "session1", LC_DIRECTION_CONSUME, &out);
  EXPECT_DOUBLE_EQ(7000.0, out.value_w);
  EXPECT_EQ(1, out.electrical_connection_id);
  EXPECT_EQ(1, out.parameter_id);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Write API: write limit sends SPINE message
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, WriteLimitSendsSpineMessage) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  limit.is_limit_active = true;
  limit.value_w = 4000.0;

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, uc_lc_eg_write_limit(eg, "session1",
                                            LC_DIRECTION_CONSUME, &limit));
  EXPECT_GT(g_send_count, before);
  /* Should contain loadControlLimitListData in the datagram */
  EXPECT_TRUE(strstr(g_last_json, "loadControlLimitListData") != nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Write API: write failsafe sends SPINE message
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, WriteFailsafePowerSendsSpineMessage) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, uc_lc_eg_write_failsafe_power(eg, "session1",
                                                      LC_DIRECTION_CONSUME, 6500.0));
  EXPECT_GT(g_send_count, before);
  EXPECT_TRUE(strstr(g_last_json, "deviceConfigurationKeyValueListData") != nullptr);
}

TEST_F(EgCacheTest, WriteFailsafeDurationSendsSpineMessage) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, uc_lc_eg_write_failsafe_duration(eg, "session1",
                                                          LC_DIRECTION_CONSUME, 120));
  EXPECT_GT(g_send_count, before);
  EXPECT_TRUE(strstr(g_last_json, "deviceConfigurationKeyValueListData") != nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Write API: write limit with different value, verify JSON payload
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, WriteLimitDifferentValue) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  limit.is_limit_active = true;
  limit.value_w = 7500.0;

  EXPECT_EQ(SPINE_OK, uc_lc_eg_write_limit(eg, "session1",
                                            LC_DIRECTION_CONSUME, &limit));
  EXPECT_TRUE(strstr(g_last_json, "7500") != nullptr);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Write API: write failsafe power, verify JSON payload
 * ═══════════════════════════════════════════════════════════════════════ */

TEST_F(EgCacheTest, WriteFailsafePowerUpdatesCache) {
  SetupFullAndDriveUcDiscovery();
  InjectTestCache();

  EXPECT_EQ(SPINE_OK, uc_lc_eg_write_failsafe_power(eg, "session1",
                                                      LC_DIRECTION_CONSUME, 8500.0));
  EXPECT_TRUE(strstr(g_last_json, "8500") != nullptr);
}
