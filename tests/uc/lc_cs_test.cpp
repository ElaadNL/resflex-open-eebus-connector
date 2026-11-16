// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/uc/uc_lc_cs.h"
#include "src/uc/uc_lc.h"
#include "src/spine/spine_device.h"
#include "src/spine/codec/spine_codec_address.h"

/* stub_timer helpers (declared in tests/support/stub_timer.c) */
void stub_timer_fire_nth(int n);
void stub_timer_reset(void);
}

class LcCsTest : public ::testing::Test {
protected:
  void SetUp() override { stub_timer_reset(); }

  void TearDown() override {
    if (local) spine_device_destroy(local);
    if (lpc_cs) { uc_lc_cs_destroy(lpc_cs); lpp_cs = nullptr; }
    if (lpp_cs) uc_lc_cs_destroy(lpp_cs);
  }

  void InitDevice() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "csDev", "csSKI", "ControllableSystem"));

    spine_entity_info_t entity;
    memset(&entity, 0, sizeof(entity));
    strcpy(entity.entity_type, "ControllableSystem");
    entity.entity_depth = 1;
    entity.entity[0] = 0;
    spine_device_add_entity(local, entity.entity, entity.entity_depth, entity.entity_type);

    spine_addr_t addr;
    memset(&addr, 0, sizeof(addr));
    strcpy(addr.device, "csDev");
    addr.entity[0] = 0;
    addr.entity_depth = 1;

    entity_cfg.device = local;
    memcpy(&entity_cfg.cs_entity_addr, &addr, sizeof(spine_addr_t));
    entity_cfg.entity_type = "ControllableSystem";
    entity_cfg.heartbeat_interval_s = 10;

    lpc_dir_cfg.initial_limit.value_w = 10000;
    lpc_dir_cfg.initial_limit.is_limit_active = true;
    lpc_dir_cfg.initial_failsafe.power_limit_w = 0;
    lpc_dir_cfg.initial_failsafe.duration_s = 7200;
    lpc_dir_cfg.initial_failsafe.power_limit_changeable = true;
    lpc_dir_cfg.initial_failsafe.duration_changeable = true;
    lpc_dir_cfg.initial_ec_characteristic.type = LC_POWER_MAX_NOMINAL;
    lpc_dir_cfg.initial_ec_characteristic.value_w = 22000;
  }

  spine_device_t* local = nullptr;
  uc_lc_cs_t *lpc_cs = nullptr, *lpp_cs = nullptr;
  uc_lc_cs_entity_config_t entity_cfg = {};
  uc_lc_cs_dir_config_t lpc_dir_cfg = {}, lpp_dir_cfg = {};
};

TEST_F(LcCsTest, CreateLpcOnly) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  ASSERT_NE(nullptr, lpc_cs);
}

TEST_F(LcCsTest, CreateLppOnly) {
  InitDevice();
  lpp_dir_cfg = lpc_dir_cfg;
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, nullptr, &lpp_dir_cfg, nullptr, &lpp_cs));
  ASSERT_NE(nullptr, lpp_cs);
}

TEST_F(LcCsTest, CreateCombined) {
  InitDevice();
  lpp_dir_cfg = lpc_dir_cfg;
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, &lpp_dir_cfg, &lpc_cs, &lpp_cs));
  ASSERT_NE(nullptr, lpc_cs);
  ASSERT_NE(nullptr, lpp_cs);
}

TEST_F(LcCsTest, GetLimit) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  uc_lc_cs_get_limit(lpc_cs, &limit);
  EXPECT_TRUE(limit.is_limit_active);
  EXPECT_EQ(10000.0, limit.value_w);
}

TEST_F(LcCsTest, SetLimit) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_limit_t new_limit;
  memset(&new_limit, 0, sizeof(new_limit));
  new_limit.value_w = 5000;
  new_limit.is_limit_active = true;
  EXPECT_EQ(SPINE_OK, uc_lc_cs_set_limit(lpc_cs, &new_limit));

  uc_lc_limit_t read;
  uc_lc_cs_get_limit(lpc_cs, &read);
  EXPECT_EQ(5000.0, read.value_w);
}

TEST_F(LcCsTest, GetStateAfterCreate) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc_cs));
}

TEST_F(LcCsTest, GetFailsafe) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_failsafe_t fs;
  memset(&fs, 0, sizeof(fs));
  uc_lc_cs_get_failsafe(lpc_cs, &fs);
  EXPECT_EQ(0.0, fs.power_limit_w);
  EXPECT_EQ(7200U, fs.duration_s);
}

TEST_F(LcCsTest, SetFailsafe) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_failsafe_t fs;
  memset(&fs, 0, sizeof(fs));
  fs.power_limit_w = 500;
  fs.duration_s = 60;
  EXPECT_EQ(SPINE_OK, uc_lc_cs_set_failsafe(lpc_cs, &fs));
}

TEST_F(LcCsTest, SetAvailable) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  EXPECT_EQ(SPINE_OK, uc_lc_cs_set_available(lpc_cs, true));
}

TEST_F(LcCsTest, GetEcCharacteristic) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_ec_characteristic_t ec;
  memset(&ec, 0, sizeof(ec));
  uc_lc_cs_get_ec_characteristic(lpc_cs, &ec);
}

TEST_F(LcCsTest, SetEcCharacteristic) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_ec_characteristic_t ec;
  memset(&ec, 0, sizeof(ec));
  ec.type = LC_POWER_MAX_NOMINAL;
  ec.value_w = 22000;
  EXPECT_EQ(SPINE_OK, uc_lc_cs_set_ec_characteristic(lpc_cs, &ec));
}

TEST_F(LcCsTest, GetHeartbeatInitial) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  time_t t;
  bool active;
  bool received = uc_lc_cs_get_eg_heartbeat(lpc_cs, &t, &active);
  EXPECT_FALSE(received);
}

TEST_F(LcCsTest, CreateNullOut) {
  /* No InitDevice — testing purely null-argument validation */
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_cs_create(nullptr, nullptr, nullptr, nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_cs_create(nullptr, &lpc_dir_cfg, nullptr, nullptr, nullptr));
}

TEST_F(LcCsTest, LimitValueChange) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_limit_t limit;
  uc_lc_cs_get_limit(lpc_cs, &limit);
  EXPECT_EQ(10000.0, limit.value_w);
  limit.value_w = 7500;
  uc_lc_cs_set_limit(lpc_cs, &limit);
  uc_lc_cs_get_limit(lpc_cs, &limit);
  EXPECT_EQ(7500.0, limit.value_w);
}

/* === Phase 1: Init timer fires → UNLIMITED_AUTONOMOUS === */
TEST_F(LcCsTest, InitTimerFires) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  EXPECT_EQ(LC_STATE_INIT, uc_lc_cs_get_state(lpc_cs));
  stub_timer_fire_nth(0);  /* timer[0] = init_timer (LPC) */
  EXPECT_EQ(LC_STATE_UNLIMITED_AUTONOMOUS, uc_lc_cs_get_state(lpc_cs));
}

/* === Phase 1: Duration mismatch → error === */
TEST_F(LcCsTest, CreateDurationMismatch) {
  InitDevice();
  lpp_dir_cfg = lpc_dir_cfg;
  lpp_dir_cfg.initial_failsafe.duration_s = 14400;  /* lpc has 7200 */
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, &lpp_dir_cfg, &lpc_cs, &lpp_cs));
  /* handles are undefined on failure */
  lpc_cs = nullptr; lpp_cs = nullptr;
}

/* === Phase 1: Set EC with NONE type → error === */
TEST_F(LcCsTest, SetEcCharNoneType) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_cs_create(&entity_cfg, &lpc_dir_cfg, nullptr, &lpc_cs, nullptr));
  uc_lc_ec_characteristic_t ec;
  memset(&ec, 0, sizeof(ec));
  ec.type = LC_POWER_MAX_NONE;
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_cs_set_ec_characteristic(lpc_cs, &ec));
}
