// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/uc/uc_lc_eg.h"
#include "src/uc/uc_lc.h"
#include "src/spine/spine_device.h"
#include "src/spine/spine_router.h"
#include "src/spine/codec/spine_codec_datagram.h"
#include "src/spine/codec/spine_codec_msg.h"
#include "src/spine/codec/spine_codec_address.h"
#include "src/spine/codec/spine_codec_load_ctrl.h"
#include "src/spine/codec/spine_codec_dev_cfg.h"
#include "src/spine/codec/spine_codec_dev_diag.h"
#include "src/spine/codec/spine_codec_elec_conn.h"
#include "src/spine/codec/spine_codec_result.h"
}

static int g_send_count = 0;
static char g_last_json[16384];

static void test_send_fn(const char* session_id, const char* json, void* ud) {
  (void)session_id; (void)ud;
  g_send_count++;
  snprintf(g_last_json, sizeof(g_last_json), "%s", json);
}

class EgDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr; remote = nullptr; router = nullptr;
    eg = nullptr;
    g_send_count = 0; g_last_json[0] = '\0';
  }

  void TearDown() override {
    if (eg) uc_lc_eg_destroy(eg);
    if (remote) spine_device_destroy(remote);
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupEgLpc() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(
        &local, "localDev", "localSKI", "CEM"));

    memset(&eg_cfg, 0, sizeof(eg_cfg));
    eg_cfg.device = local;
    spine_addr_make(&eg_cfg.eg_entity_addr, "localDev", 1, 0);
    eg_cfg.entity_type = "CEM";
    eg_cfg.heartbeat_interval_s = 60;

    ASSERT_EQ(SPINE_OK, uc_lc_eg_create(
        &eg, &eg_cfg, true, false));
    ASSERT_NE(nullptr, eg);

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
  uc_lc_eg_t* eg;
  uc_lc_eg_config_t eg_cfg;
};

/* ════════════════════════════════════════════════════════════════════════════
 * EG DIAG server (feature 3) — READ heartbeat → responds with EG HB
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, DiagServerHeartbeatRead) {
  SetupEgLpc();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 3);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 1, false,
                     "deviceDiagnosisHeartbeatData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "deviceDiagnosisHeartbeatData") != nullptr);
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EG DIAG server (feature 3) — non-READ cmd ignored
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, DiagServerNonReadIgnored) {
  SetupEgLpc();
  /* Build a valid heartbeat but send as NOTIFY → handler returns early */
  dev_diag_heartbeat_t hb;
  memset(&hb, 0, sizeof(hb));
  hb.heartbeat_counter = 1;
  hb.heartbeat_counter_present = true;
  cJSON* fn_data = dev_diag_heartbeat_encode(&hb);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 3);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 2, false,
                     "deviceDiagnosisHeartbeatData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  int before = g_send_count;
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_EQ(before, g_send_count);  /* no reply from handler */
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EG LC client (feature 1) — NOTIFY without cache → early return
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, LcNotifyLimitDescNoCache) {
  SetupEgLpc();
  /* Build a valid limit description notify */
  lc_limit_desc_list_t descs;
  memset(&descs, 0, sizeof(descs));
  descs.count = 1;
  descs.items[0].limit_id = 1;
  snprintf(descs.items[0].limit_type, sizeof(descs.items[0].limit_type),
           "signDependentAbsValueLimit");
  descs.items[0].limit_type_present = true;
  snprintf(descs.items[0].limit_direction, sizeof(descs.items[0].limit_direction),
           "consume");
  descs.items[0].limit_direction_present = true;

  cJSON* fn_data = lc_limit_desc_list_encode(&descs);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 10, false,
                     "loadControlLimitDescriptionListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* No cache → early return, no send */
  free(json);
}

TEST_F(EgDispatchTest, LcNotifyLimitListNoCache) {
  SetupEgLpc();
  lc_limit_list_t lst;
  memset(&lst, 0, sizeof(lst));
  lst.count = 1;
  lst.items[0].limit_id = 1;
  lst.items[0].is_limit_active = true;
  lst.items[0].is_limit_active_present = true;
  lst.items[0].is_limit_changeable = true;
  lst.items[0].is_limit_changeable_present = true;
  spine_scaled_number_from_double(5000.0, &lst.items[0].value);
  lst.items[0].value_present = true;

  cJSON* fn_data = lc_limit_list_encode(&lst);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 11, false,
                     "loadControlLimitListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(EgDispatchTest, LcResultNoCache) {
  SetupEgLpc();
  /* Build a resultData payload with error */
  spine_result_data_t res;
  memset(&res, 0, sizeof(res));
  res.error_number = 0;
  res.description_present = true;
  snprintf(res.description, sizeof(res.description), "ok");
  cJSON* fn_data = spine_result_data_encode(&res);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  /* RESULT msg: use msg_counter_ref to reference original request */
  spine_msg_t msg;
  memset(&msg, 0, sizeof(msg));
  spine_addr_make(&msg.header.src, "peerDev", 0, 0);
  spine_addr_make(&msg.header.dst, "localDev", 1, 1);
  msg.header.cmd = SPINE_CMD_RESULT;
  msg.header.msg_counter = 20;
  msg.header.msg_counter_ref = 10;
  msg.header.msg_counter_ref_present = true;
  msg.header.ack_request = false;
  snprintf(msg.payload.fn_name, sizeof(msg.payload.fn_name), "resultData");
  msg.payload.fn_data = fn_data;

  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* No cache → early return after decode, no send */
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EG CFG client (feature 2) — NOTIFY without cache → early return
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, CfgNotifyKvListNoCache) {
  SetupEgLpc();
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(5000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 30, false,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(EgDispatchTest, CfgNotifyKvDescListNoCache) {
  SetupEgLpc();
  dev_cfg_kv_desc_list_t descs;
  memset(&descs, 0, sizeof(descs));
  descs.count = 1;
  descs.items[0].key_id = 1;
  snprintf(descs.items[0].key_name, sizeof(descs.items[0].key_name), "testKey");
  descs.items[0].key_name_present = true;

  cJSON* fn_data = dev_cfg_kv_desc_list_encode(&descs);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 31, false,
                     "deviceConfigurationKeyValueDescriptionListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

TEST_F(EgDispatchTest, CfgResultNoCache) {
  SetupEgLpc();
  spine_result_data_t res;
  memset(&res, 0, sizeof(res));
  res.error_number = 1;
  snprintf(res.description, sizeof(res.description), "error");
  res.description_present = true;
  cJSON* fn_data = spine_result_data_encode(&res);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  memset(&msg, 0, sizeof(msg));
  spine_addr_make(&msg.header.src, "peerDev", 0, 0);
  spine_addr_make(&msg.header.dst, "localDev", 1, 2);
  msg.header.cmd = SPINE_CMD_RESULT;
  msg.header.msg_counter = 40;
  msg.header.msg_counter_ref = 30;
  msg.header.msg_counter_ref_present = true;
  msg.header.ack_request = false;
  snprintf(msg.payload.fn_name, sizeof(msg.payload.fn_name), "resultData");
  msg.payload.fn_data = fn_data;

  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EG DIAG client (feature 4) — NOTIFY without cache → early return
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, DiagClientNotifyHeartbeatNoCache) {
  SetupEgLpc();
  dev_diag_heartbeat_t hb;
  memset(&hb, 0, sizeof(hb));
  hb.heartbeat_counter = 1;
  hb.heartbeat_counter_present = true;
  cJSON* fn_data = dev_diag_heartbeat_encode(&hb);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 4);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 50, false,
                     "deviceDiagnosisHeartbeatData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EG EC client (feature 5) — NOTIFY without cache → early return
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(EgDispatchTest, EcNotifyCharListNoCache) {
  SetupEgLpc();
  ec_characteristic_list_t chars;
  memset(&chars, 0, sizeof(chars));
  chars.count = 1;
  chars.items[0].characteristic_id = 1;
  chars.items[0].electrical_connection_id = 1;
  chars.items[0].parameter_id = 1;
  chars.items[0].value_present = true;
  spine_scaled_number_from_double(10000.0, &chars.items[0].value);

  cJSON* fn_data = ec_characteristic_list_encode(&chars);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 5);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 60, false,
                     "electricalConnectionCharacteristicListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);
}
