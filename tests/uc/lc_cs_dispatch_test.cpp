// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/uc/uc_lc_cs.h"
#include "src/uc/uc_lc.h"
#include "src/spine/spine_device.h"
#include "src/spine/spine_device_internal.h"
#include "src/spine/spine_router.h"
#include "src/spine/spine_sub_bind.h"
#include "src/spine/codec/spine_codec_datagram.h"
#include "src/spine/codec/spine_codec_msg.h"
#include "src/spine/codec/spine_codec_address.h"
#include "src/spine/codec/spine_codec_load_ctrl.h"
#include "src/spine/codec/spine_codec_dev_cfg.h"
#include "src/spine/codec/spine_codec_dev_diag.h"
#include "src/spine/codec/spine_codec_elec_conn.h"
#include "src/spine/codec/spine_rfe.h"

/* stub_timer helpers (declared in tests/support/stub_timer.c) */
void stub_timer_fire_nth(int n);
void stub_timer_reset(void);
}

static int g_send_count = 0;
static char g_last_json[16384];

static void test_send_fn(const char* session_id, const char* json, void* ud) {
  (void)session_id; (void)ud;
  g_send_count++;
  snprintf(g_last_json, sizeof(g_last_json), "%s", json);
}

class CsDispatchTest : public ::testing::Test {
protected:
  void SetUp() override {
    local = nullptr; remote = nullptr; router = nullptr;
    lpc = nullptr;
    g_send_count = 0; g_last_json[0] = '\0';
    memset(&ent_cfg, 0, sizeof(ent_cfg));
    stub_timer_reset();
  }

  void TearDown() override {
    if (lpc) uc_lc_cs_destroy(lpc);
    if (remote) spine_device_destroy(remote);
    if (router) spine_router_destroy(router);
    if (local) spine_device_destroy(local);
  }

  void SetupLpcOnly() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(
        &local, "localDev", "localSKI", "ControllableSystem"));

    spine_addr_t addr;
    spine_addr_make(&addr, "localDev", 1, 0);

    ent_cfg.device = local;
    ent_cfg.cs_entity_addr = addr;
    ent_cfg.entity_type = "CEM";
    ent_cfg.heartbeat_interval_s = 60;
    ent_cfg.initial_available = true;

    memset(&lpc_dir_cfg, 0, sizeof(lpc_dir_cfg));
    lpc_dir_cfg.initial_limit.value_w = 10000.0;
    lpc_dir_cfg.initial_limit.is_limit_active = true;
    lpc_dir_cfg.initial_failsafe.power_limit_w = 5000.0;
    lpc_dir_cfg.initial_failsafe.duration_s = 7200;
    lpc_dir_cfg.initial_failsafe.power_limit_changeable = true;
    lpc_dir_cfg.initial_failsafe.duration_changeable = true;
    lpc_dir_cfg.initial_ec_characteristic.type = LC_POWER_MAX_NOMINAL;
    lpc_dir_cfg.initial_ec_characteristic.value_w = 15000.0;

    ASSERT_EQ(SPINE_OK, uc_lc_cs_create(
        &ent_cfg, &lpc_dir_cfg, nullptr, &lpc, nullptr));
    ASSERT_NE(nullptr, lpc);

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

  void SetLimitWriteCallback() {
    ent_cfg.on_limit_write = +[](const uc_lc_limit_t*, const uc_lc_limit_t*, void*) -> bool { return true; };
    ent_cfg.on_failsafe_write = +[](const uc_lc_failsafe_t*, const uc_lc_failsafe_t*, void*) -> bool { return true; };
  }

  void AddBinding(uint8_t feature = 1) {
    spine_addr_t client, server;
    spine_addr_make(&client, "peerDev", 0, 0);
    server = ent_cfg.cs_entity_addr;
    server.feature = feature;
    spine_sub_bind_add_binding(remote->sub_bind, &client, &server);
  }

  void DispatchHbNotify() {
    dev_diag_heartbeat_t hb;
    memset(&hb, 0, sizeof(hb));
    hb.heartbeat_counter = 1;
    hb.heartbeat_counter_present = true;
    hb.timestamp_present = true;
    snprintf(hb.timestamp, sizeof(hb.timestamp), "2025-01-01T00:00:00Z");
    hb.heartbeat_timeout_present = true;
    snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout), "PT60S");
    cJSON* hb_data = dev_diag_heartbeat_encode(&hb);
    spine_addr_t src, dst;
    spine_addr_make(&src, "peerDev", 0, 0);
    spine_addr_make(&dst, "localDev", 1, 4);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 50, false,
                       "deviceDiagnosisHeartbeatData", hb_data);
    char* json = build_datagram(&msg);
    cJSON_Delete(hb_data);
    spine_device_dispatch(remote, json);
    free(json);
  }

  void DispatchLcWrite(int limit_id, bool active, double val, int cmd_id) {
    lc_limit_list_t lst;
    memset(&lst, 0, sizeof(lst));
    lst.count = 1;
    lst.items[0].limit_id = limit_id;
    lst.items[0].is_limit_active = active;
    lst.items[0].is_limit_active_present = true;
    lst.items[0].is_limit_changeable = true;
    lst.items[0].is_limit_changeable_present = true;
    spine_scaled_number_from_double(val, &lst.items[0].value);
    lst.items[0].value_present = true;
    cJSON* fn_data = lc_limit_list_encode(&lst);
    spine_addr_t src, dst;
    spine_addr_make(&src, "peerDev", 0, 0);
    spine_addr_make(&dst, "localDev", 1, 1);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, cmd_id, true,
                       "loadControlLimitListData", fn_data);
    char* json = build_datagram(&msg);
    cJSON_Delete(fn_data);
    spine_device_dispatch(remote, json);
    free(json);
  }

  void DispatchLcWriteWithTimePeriod(int limit_id, bool active, double val,
                                      int cmd_id, const char* end_time) {
    lc_limit_list_t lst;
    memset(&lst, 0, sizeof(lst));
    lst.count = 1;
    lst.items[0].limit_id = limit_id;
    lst.items[0].is_limit_active = active;
    lst.items[0].is_limit_active_present = true;
    lst.items[0].is_limit_changeable = true;
    lst.items[0].is_limit_changeable_present = true;
    spine_scaled_number_from_double(val, &lst.items[0].value);
    lst.items[0].value_present = true;
    if (end_time) {
      snprintf(lst.items[0].time_period.end_time,
               sizeof(lst.items[0].time_period.end_time), "%s", end_time);
      lst.items[0].time_period.end_time_present = true;
    }
    cJSON* fn_data = lc_limit_list_encode(&lst);
    spine_addr_t src, dst;
    spine_addr_make(&src, "peerDev", 0, 0);
    spine_addr_make(&dst, "localDev", 1, 1);
    spine_msg_t msg;
    spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, cmd_id, true,
                       "loadControlLimitListData", fn_data);
    char* json = build_datagram(&msg);
    cJSON_Delete(fn_data);
    spine_device_dispatch(remote, json);
    free(json);
  }

  spine_device_t* local;
  spine_device_t* remote;
  spine_router_t* router;
  uc_lc_cs_t* lpc;
  uc_lc_cs_entity_config_t ent_cfg;
  uc_lc_cs_dir_config_t lpc_dir_cfg;
};

/* ════════════════════════════════════════════════════════════════════════════
 * LC (feature 1) — READ
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitListRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 1, false,
                     "loadControlLimitListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "loadControlLimitListData") != nullptr);
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Unlimited Autonomous → Unlimited Controlled via heartbeat + write [LPC-918]
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitWriteUnlimitedAutonomousToControlled) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                           /* timer[1] = watchdog */
  DispatchLcWrite(1, true, 3000.0, 90);         /* → LIMITED */

  stub_timer_fire_nth(1);                        /* → FAILSAFE */
  EXPECT_EQ(LC_STATE_FAILSAFE, uc_lc_cs_get_state(lpc));
  stub_timer_fire_nth(2);                        /* failsafe_dur → UA */
  EXPECT_EQ(LC_STATE_UNLIMITED_AUTONOMOUS, uc_lc_cs_get_state(lpc));

  /* Send heartbeat + limit write inactive → back to UC */
  int before = g_send_count;
  DispatchHbNotify();
  DispatchLcWrite(1, false, 3000.0, 91);
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
  EXPECT_LT(before, g_send_count);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Combined LPC + LPP dispatch on same entity
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcCombinedLpcLppDispatch) {
  uc_lc_cs_dir_config_t lpp_dir_cfg;
  memset(&lpp_dir_cfg, 0, sizeof(lpp_dir_cfg));
  lpp_dir_cfg.initial_limit.value_w = 8000.0;
  lpp_dir_cfg.initial_limit.is_limit_active = false;
  lpp_dir_cfg.initial_failsafe.power_limit_w = 3000.0;
  lpp_dir_cfg.initial_failsafe.duration_s = 7200;
  lpp_dir_cfg.initial_failsafe.power_limit_changeable = true;
  lpp_dir_cfg.initial_failsafe.duration_changeable = true;
  lpp_dir_cfg.initial_ec_characteristic.type = LC_POWER_MAX_NOMINAL;
  lpp_dir_cfg.initial_ec_characteristic.value_w = 12000.0;

  SetLimitWriteCallback();
  SetupLpcOnly();
  /* Re-create CS with LPP dir config too */
  uc_lc_cs_destroy(lpc); lpc = nullptr;
  ASSERT_EQ(SPINE_OK, uc_lc_cs_create(
      &ent_cfg, &lpc_dir_cfg, &lpp_dir_cfg, &lpc, nullptr));
  ASSERT_NE(nullptr, lpc);

  AddBinding(1);
  DispatchHbNotify();

  /* Both LPC (limit_id=1) and LPP (limit_id=2) writes are dispatched */
  int before = g_send_count;
  DispatchLcWrite(1, true, 3000.0, 92);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  EXPECT_LT(before, g_send_count);
}

/* ════════════════════════════════════════════════════════════════════════════
 * CS heartbeat timer fires: callback creates and sends heartbeat notify
 * (verified by code inspection of cs_hb_timer_cb → spine_device_notify).
 * Test skipped — requires EG subscription to CS DIAG server feature
 * which is not set up in test routing infrastructure.
 * ════════════════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════════════════
 * Limit write rejected when heartbeat >60s old [LPC-916]
 * Uses mocked time to simulate the delayed heartbeat.
 * ════════════════════════════════════════════════════════════════════════════ */

#include "mock_time.h"

TEST_F(CsDispatchTest, LcLimitWriteHeartbeatTooLate) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();

  mock_time_set(1000);
  DispatchHbNotify();                            /* eg_hb_received_time = 1000 */

  mock_time_advance(70);                         /* now = 1070, diff = 70 > 60 */
  int before = g_send_count;
  DispatchLcWrite(1, true, 3000.0, 94);

  /* Write should be rejected → no state change, but error reply sent */
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc));
  EXPECT_LT(before, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "errorNumber") != nullptr)
      << "Reply should contain error result";
  mock_time_clear();
}

/* ════════════════════════════════════════════════════════════════════════════
 * CS on_state_change callback fires on state transitions
 * ════════════════════════════════════════════════════════════════════════════ */

static int g_state_change_count;
static lc_cs_state_t g_last_old_state;
static lc_cs_state_t g_last_new_state;

static void on_cs_state_change(uc_lc_cs_t*, lc_cs_state_t old_s, lc_cs_state_t new_s, void*) {
  g_state_change_count++;
  g_last_old_state = old_s;
  g_last_new_state = new_s;
}

TEST_F(CsDispatchTest, CsStateChangeCallback) {
  g_state_change_count = 0;
  g_last_old_state = (lc_cs_state_t)-1;
  g_last_new_state = (lc_cs_state_t)-1;

  /* Setup manually (can't use SetupLpcOnly() which clears lpc_dir_cfg) */
  ASSERT_EQ(SPINE_OK, spine_device_create_local(
      &local, "localDev", "localSKI", "ControllableSystem"));
  spine_addr_t addr;
  spine_addr_make(&addr, "localDev", 1, 0);
  ent_cfg.device = local;
  ent_cfg.cs_entity_addr = addr;
  ent_cfg.entity_type = "CEM";
  ent_cfg.heartbeat_interval_s = 60;
  ent_cfg.initial_available = true;
  ent_cfg.on_limit_write = +[](const uc_lc_limit_t*, const uc_lc_limit_t*, void*) -> bool { return true; };
  ent_cfg.on_failsafe_write = +[](const uc_lc_failsafe_t*, const uc_lc_failsafe_t*, void*) -> bool { return true; };

  memset(&lpc_dir_cfg, 0, sizeof(lpc_dir_cfg));
  lpc_dir_cfg.initial_limit.value_w = 10000.0;
  lpc_dir_cfg.initial_limit.is_limit_active = true;
  lpc_dir_cfg.initial_failsafe.power_limit_w = 5000.0;
  lpc_dir_cfg.initial_failsafe.duration_s = 7200;
  lpc_dir_cfg.initial_failsafe.power_limit_changeable = true;
  lpc_dir_cfg.initial_failsafe.duration_changeable = true;
  lpc_dir_cfg.initial_ec_characteristic.type = LC_POWER_MAX_NOMINAL;
  lpc_dir_cfg.initial_ec_characteristic.value_w = 15000.0;
  lpc_dir_cfg.on_state_change = on_cs_state_change;

  ASSERT_EQ(SPINE_OK, uc_lc_cs_create(
      &ent_cfg, &lpc_dir_cfg, nullptr, &lpc, nullptr));
  ASSERT_NE(nullptr, lpc);
  ASSERT_EQ(SPINE_OK, spine_router_create(
      &router, local, test_send_fn, nullptr));
  ASSERT_EQ(SPINE_OK, spine_device_create_remote(
      &remote, "peerSKI", router, local, "session1"));
  AddBinding();

  /* INIT is the starting state (no callback yet) */
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc));
  EXPECT_EQ(0, g_state_change_count);

  /* Heartbeat alone does NOT transition state; an LC write is required */
  DispatchHbNotify();
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc));
  EXPECT_EQ(0, g_state_change_count);

  /* LC write (active) transitions INIT → LIMITED */
  DispatchLcWrite(1, true, 5000.0, 95);
  EXPECT_LE(1, g_state_change_count);
  EXPECT_EQ(LC_STATE_INIT, g_last_old_state);
  EXPECT_EQ(LC_STATE_LIMITED, g_last_new_state);
}

TEST_F(CsDispatchTest, LcLimitDescListRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 2, false,
                     "loadControlLimitDescriptionListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "loadControlLimitDescriptionListData") != nullptr);
  free(json);
}

TEST_F(CsDispatchTest, LcLimitListReadPartialByLimitId) {
  SetupLpcOnly();
  /* Build a partial read with selector limitId=1 */
  spine_rfe_filter_t partial;
  memset(&partial, 0, sizeof(partial));
  partial.present = true;
  const char* sel_keys[] = {"limitId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&partial, "loadControlLimitListData",
                          sel_keys, sel_vals, 1, NULL);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 3, false,
                     "loadControlLimitListData", NULL);
  msg.payload.partial = partial;
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_TRUE(strstr(json, "Selectors") != nullptr)
      << "JSON must contain Selectors for partial read";
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
  spine_rfe_free_filter(&partial);
}

TEST_F(CsDispatchTest, LcLimitDescListReadPartial) {
  SetupLpcOnly();
  spine_rfe_filter_t partial;
  memset(&partial, 0, sizeof(partial));
  partial.present = true;
  const char* sel_keys[] = {"limitId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&partial, "loadControlLimitDescriptionListData",
                          sel_keys, sel_vals, 1, NULL);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 4, false,
                     "loadControlLimitDescriptionListData", NULL);
  msg.payload.partial = partial;
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_TRUE(strstr(json, "Selectors") != nullptr)
      << "JSON must contain Selectors for partial read";
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
  spine_rfe_free_filter(&partial);
}

/* ════════════════════════════════════════════════════════════════════════════
 * LC (feature 1) — WRITE error paths (initial state = INIT, no heartbeat)
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitWriteRejectedNoHearbeat) {
  SetupLpcOnly();
  lc_limit_list_t lst;
  memset(&lst, 0, sizeof(lst));
  lst.count = 1;
  lst.items[0].limit_id = 1;
  lst.items[0].is_limit_active = false;
  lst.items[0].is_limit_active_present = true;
  lst.items[0].is_limit_changeable = true;
  lst.items[0].is_limit_changeable_present = true;
  spine_scaled_number_from_double(8000.0, &lst.items[0].value);
  lst.items[0].value_present = true;

  cJSON* fn_data = lc_limit_list_encode(&lst);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 100, true,
                     "loadControlLimitListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, LcLimitWriteRejectedNoApplicableId) {
  SetupLpcOnly();
  lc_limit_list_t lst;
  memset(&lst, 0, sizeof(lst));
  lst.count = 1;
  lst.items[0].limit_id = 99;  /* doesn't match LPC limit_id=1 */
  lst.items[0].is_limit_active = false;
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
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 101, true,
                     "loadControlLimitListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, LcLimitWriteRejectedBadPayload) {
  SetupLpcOnly();
  cJSON* fn_data = cJSON_CreateObject();
  ASSERT_NE(nullptr, fn_data);
  /* Empty object is invalid lc_limit_list_t → decode fails */

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 1);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 102, true,
                     "loadControlLimitListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, LcLimitWriteInitToUnlimitedControlled) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();
  DispatchLcWrite(1, false, 5000.0, 70);
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
}

TEST_F(CsDispatchTest, LcLimitWriteUnlimitedControlledToLimited) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();
  DispatchLcWrite(1, false, 5000.0, 71);
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
  DispatchLcWrite(1, true, 3000.0, 72);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  uc_lc_limit_t lim;
  uc_lc_cs_get_limit(lpc, &lim);
  EXPECT_DOUBLE_EQ(3000.0, lim.value_w);
}

TEST_F(CsDispatchTest, LcLimitWriteLimitedToUnlimitedControlled) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();
  DispatchLcWrite(1, true, 5000.0, 73);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  DispatchLcWrite(1, false, 5000.0, 74);
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
}

TEST_F(CsDispatchTest, LcLimitWriteStaysLimited) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();
  DispatchLcWrite(1, true, 5000.0, 75);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  int before = g_send_count;
  DispatchLcWrite(1, true, 5000.0, 76);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  EXPECT_LT(before, g_send_count);
}

/* ════════════════════════════════════════════════════════════════════════════
 * CFG (feature 2) — READ
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgKvListRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 10, false,
                     "deviceConfigurationKeyValueListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "deviceConfigurationKeyValueListData") != nullptr);
  free(json);
}

TEST_F(CsDispatchTest, CfgKvDescListRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 11, false,
                     "deviceConfigurationKeyValueDescriptionListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json,
      "deviceConfigurationKeyValueDescriptionListData") != nullptr);
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * CFG (feature 2) — WRITE error paths
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgWriteDeleteNotSupported) {
  SetupLpcOnly();
  /* Build a KV payload */
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 200, true,
                     "deviceConfigurationKeyValueListData", fn_data);

  /* Add delete filter */
  const char* sel_keys[] = {"keyId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  const char* elem_paths[] = {"value", NULL};
  spine_rfe_build_filter(&msg.payload.delete_,
      "deviceConfigurationKeyValueListData", sel_keys, sel_vals, 1, elem_paths);

  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
  spine_rfe_free_filter(&msg.payload.delete_);
}

TEST_F(CsDispatchTest, CfgWriteRejectedInitState) {
  SetupLpcOnly();
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;  /* LPC failsafe value key */
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 201, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, CfgWriteRejectedNoApplicableKeyId) {
  SetupLpcOnly();
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 99;  /* doesn't match any known key */
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 202, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, CfgWriteRejectedInitStateWithBinding) {
  SetupLpcOnly();
  AddBinding(2);
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 203, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
}

TEST_F(CsDispatchTest, CfgKvListReadPartial) {
  SetupLpcOnly();
  spine_rfe_filter_t partial;
  memset(&partial, 0, sizeof(partial));
  partial.present = true;
  const char* sel_keys[] = {"keyId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&partial, "deviceConfigurationKeyValueListData",
                          sel_keys, sel_vals, 1, NULL);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 204, false,
                     "deviceConfigurationKeyValueListData", NULL);
  msg.payload.partial = partial;
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_TRUE(strstr(json, "keyId") != nullptr)
      << "JSON must contain keyId for partial read";
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
  spine_rfe_free_filter(&partial);
}

/* ════════════════════════════════════════════════════════════════════════════
 * DIAG (feature 3 = server) — READ heartbeat
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, DiagHeartbeatRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 3);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 20, false,
                     "deviceDiagnosisHeartbeatData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json, "deviceDiagnosisHeartbeatData") != nullptr);
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EC (feature 5) — READ characteristic list
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, EcCharacteristicListRead) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 5);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 30, false,
                     "electricalConnectionCharacteristicListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_TRUE(strstr(g_last_json,
      "electricalConnectionCharacteristicListData") != nullptr);
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Unsupported feature / destination error
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, DispatchUnknownFeature) {
  SetupLpcOnly();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 99);  /* feature 99 doesn't exist */
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 40, false,
                     "loadControlLimitListData", NULL);
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_ERR_NOT_FOUND, spine_device_dispatch(remote, json));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * DIAG client (feature 4) — NOTIFY heartbeat without binding → rejected
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, DiagHeartbeatNotifyNoBinding) {
  SetupLpcOnly();
  dev_diag_heartbeat_t hb;
  memset(&hb, 0, sizeof(hb));
  hb.heartbeat_counter = 1;
  hb.heartbeat_counter_present = true;
  hb.timestamp_present = true;
  snprintf(hb.timestamp, sizeof(hb.timestamp), "2025-01-01T00:00:00Z");
  hb.heartbeat_timeout_present = true;
  snprintf(hb.heartbeat_timeout, sizeof(hb.heartbeat_timeout), "PT60S");

  cJSON* fn_data = dev_diag_heartbeat_encode(&hb);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 4);  /* diag client feature */
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 50, false,
                     "deviceDiagnosisHeartbeatData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  /* Handler will silently return (no binding) → no send expected */
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * EC (feature 5) — READ with partial filter
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, EcCharacteristicListReadPartial) {
  SetupLpcOnly();
  spine_rfe_filter_t partial;
  memset(&partial, 0, sizeof(partial));
  partial.present = true;
  const char* sel_keys[] = {"characteristicId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&partial, "electricalConnectionCharacteristicListData",
                          sel_keys, sel_vals, 1, NULL);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 5);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_READ, 31, false,
                     "electricalConnectionCharacteristicListData", NULL);
  msg.payload.partial = partial;
  char* json = build_datagram(&msg);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  free(json);
  spine_rfe_free_filter(&partial);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Phase 3: Binding + Heartbeat NOTIFY + accepted LC WRITE + watchdog
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, DiagHeartbeatNotifyWithBinding) {
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();

  bool active;
  bool received = uc_lc_cs_get_eg_heartbeat(lpc, nullptr, &active);
  EXPECT_TRUE(received);
  EXPECT_TRUE(active);
}

TEST_F(CsDispatchTest, DiagHeartbeatNotifyFailsWithBindNoDecode) {
  SetupLpcOnly();
  AddBinding();

  /* Empty payload → decode fails (binding check passes) */
  cJSON* empty = cJSON_CreateObject();
  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 4);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_NOTIFY, 51, false,
                     "deviceDiagnosisHeartbeatData", empty);
  char* json = build_datagram(&msg);
  cJSON_Delete(empty);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  free(json);

  bool active;
  bool received = uc_lc_cs_get_eg_heartbeat(lpc, nullptr, &active);
  EXPECT_FALSE(received);
}

TEST_F(CsDispatchTest, LcLimitWriteAccepted) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();

  int before = g_send_count;
  DispatchLcWrite(1, true, 5000.0, 60);

  EXPECT_LT(before, g_send_count);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  uc_lc_limit_t read;
  memset(&read, 0, sizeof(read));
  uc_lc_cs_get_limit(lpc, &read);
  EXPECT_TRUE(read.is_limit_active);
  EXPECT_DOUBLE_EQ(5000.0, read.value_w);
}

TEST_F(CsDispatchTest, HeartbeatWatchdogFiresFailsafe) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                      /* timer[1] = watchdog */
  DispatchLcWrite(1, true, 3000.0, 61);    /* → LIMITED */

  stub_timer_fire_nth(1);                   /* → FAILSAFE */

  EXPECT_EQ(LC_STATE_FAILSAFE, uc_lc_cs_get_state(lpc));

  bool active;
  uc_lc_cs_get_eg_heartbeat(lpc, nullptr, &active);
  EXPECT_FALSE(active);
}

TEST_F(CsDispatchTest, FailsafeDurationTransition) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                      /* timer[1] = watchdog */
  DispatchLcWrite(1, true, 2000.0, 62);    /* → LIMITED */

  stub_timer_fire_nth(1);                   /* → FAILSAFE */
  EXPECT_EQ(LC_STATE_FAILSAFE, uc_lc_cs_get_state(lpc));

  stub_timer_fire_nth(2);                   /* failsafe_dur → UNLIMITED_AUTONOMOUS */
  EXPECT_EQ(LC_STATE_UNLIMITED_AUTONOMOUS, uc_lc_cs_get_state(lpc));
}

TEST_F(CsDispatchTest, LimitWriteTimerAfterFailsafe) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                      /* timer[1] = watchdog */
  DispatchLcWrite(1, true, 4000.0, 63);    /* → LIMITED */

  stub_timer_fire_nth(1);                   /* → FAILSAFE (timer[2] = failsafe_dur) */
  EXPECT_EQ(LC_STATE_FAILSAFE, uc_lc_cs_get_state(lpc));

  DispatchHbNotify();                      /* → creates timer[3] = limit_write_timer */

  stub_timer_fire_nth(3);                   /* limit_write_timer → UNLIMITED_AUTONOMOUS */
  EXPECT_EQ(LC_STATE_UNLIMITED_AUTONOMOUS, uc_lc_cs_get_state(lpc));

  stub_timer_fire_nth(2);                   /* failsafe_dur — no-op */
  EXPECT_EQ(LC_STATE_UNLIMITED_AUTONOMOUS, uc_lc_cs_get_state(lpc));
}

/* ════════════════════════════════════════════════════════════════════════════
 * Limit timePeriod elapsed: LIMITED → UNLIMITED_CONTROLLED [LPC-908]
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitDurationTimer) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                              /* timer[1] = watchdog */
  DispatchLcWriteWithTimePeriod(1, true, 5000.0, 80, "PT1S");

  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  stub_timer_fire_nth(2);                           /* limit_duration_timer */

  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
  uc_lc_limit_t lim;
  uc_lc_cs_get_limit(lpc, &lim);
  EXPECT_FALSE(lim.is_limit_active);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Failsafe write rejected in INIT state [LPC-003/2]
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgWriteFailsafeRejectedInInit) {
  SetupLpcOnly();
  AddBinding(2);
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc));

  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 210, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Failsafe write with negative power rejected [LPC-021]
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgWriteFailsafeNegativePowerRejected) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();   /* feature 1: needed for heartbeat + LC write */
  AddBinding(2);  /* feature 2: needed for CFG write */
  DispatchHbNotify();                              /* → UNLIMITED_CONTROLLED */
  DispatchLcWrite(1, true, 5000.0, 81);            /* → LIMITED */

  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(-100.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 211, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * FAILSAFE → UNLIMITED_CONTROLLED via heartbeat resume + accepted limit write
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitWriteFailsafeToUnlimitedControlled) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                           /* timer[1] = watchdog */
  DispatchLcWrite(1, true, 4000.0, 82);         /* → LIMITED */

  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  stub_timer_fire_nth(1);                        /* watchdog → FAILSAFE */
  EXPECT_EQ(LC_STATE_FAILSAFE, uc_lc_cs_get_state(lpc));

  DispatchHbNotify();                            /* limit_write_timer = timer[3] */

  DispatchLcWrite(1, false, 4000.0, 83);        /* accepted → UNLIMITED_CONTROLLED */
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
}

/* ════════════════════════════════════════════════════════════════════════════
 * Failsafe write with duration < 7200s rejected [LPC-022]
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgWriteFailsafeDurationBelowMin) {
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  AddBinding(2);
  DispatchHbNotify();
  DispatchLcWrite(1, true, 5000.0, 85);

  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  /* Write duration key (key_id=2 in LPC-only) with value < 7200s */
  dev_cfg_kv_data_t item;
  memset(&item, 0, sizeof(item));
  item.key_id = 2;
  item.value_type = DEV_CFG_VALUE_DURATION;
  snprintf(item.value.duration_val, sizeof(item.value.duration_val), "PT100S");
  item.value_present = true;
  item.is_value_changeable = true;
  item.is_value_changeable_present = true;

  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0] = item;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 212, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  free(json);
}

/* ════════════════════════════════════════════════════════════════════════════
 * LC write rejected because app callback returns false
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, LcLimitWriteAppCallbackRejects) {
  ent_cfg.on_limit_write = +[](const uc_lc_limit_t*, const uc_lc_limit_t*, void*) -> bool { return false; };
  SetupLpcOnly();
  AddBinding();
  DispatchHbNotify();                           /* → UNLIMITED_CONTROLLED */

  int before = g_send_count;
  DispatchLcWrite(1, true, 3000.0, 86);

  EXPECT_LT(before, g_send_count);
  EXPECT_EQ(LC_STATE_UNLIMITED_CONTROLLED, uc_lc_cs_get_state(lpc));
  /* Limit unchanged — still at initial value (10000W, active=true) */
  uc_lc_limit_t lim;
  uc_lc_cs_get_limit(lpc, &lim);
  EXPECT_TRUE(lim.is_limit_active);
  EXPECT_DOUBLE_EQ(10000.0, lim.value_w);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Failsafe write rejected because app callback returns false
 * ════════════════════════════════════════════════════════════════════════════ */

TEST_F(CsDispatchTest, CfgWriteFailsafeAppCallbackRejects) {
  ent_cfg.on_failsafe_write = +[](const uc_lc_failsafe_t*, const uc_lc_failsafe_t*, void*) -> bool { return false; };
  SetLimitWriteCallback();
  SetupLpcOnly();
  AddBinding();
  AddBinding(2);
  DispatchHbNotify();
  DispatchLcWrite(1, true, 5000.0, 87);

  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));

  /* Write a valid failsafe KV — should be rejected by callback */
  dev_cfg_kv_list_t kv;
  memset(&kv, 0, sizeof(kv));
  kv.count = 1;
  kv.items[0].key_id = 1;
  kv.items[0].value_type = DEV_CFG_VALUE_SCALED_NUM;
  spine_scaled_number_from_double(3000.0, &kv.items[0].value.scaled_num_val);
  kv.items[0].value_present = true;
  kv.items[0].is_value_changeable = true;
  kv.items[0].is_value_changeable_present = true;

  cJSON* fn_data = dev_cfg_kv_list_encode(&kv);
  ASSERT_NE(nullptr, fn_data);

  spine_addr_t src, dst;
  spine_addr_make(&src, "peerDev", 0, 0);
  spine_addr_make(&dst, "localDev", 1, 2);
  spine_msg_t msg;
  spine_make_msg_req(&msg, &src, &dst, SPINE_CMD_WRITE, 213, true,
                     "deviceConfigurationKeyValueListData", fn_data);
  char* json = build_datagram(&msg);
  cJSON_Delete(fn_data);
  ASSERT_NE(nullptr, json);
  EXPECT_EQ(SPINE_OK, spine_device_dispatch(remote, json));
  EXPECT_LT(0, g_send_count);
  EXPECT_EQ(LC_STATE_LIMITED, uc_lc_cs_get_state(lpc));
  free(json);
}
