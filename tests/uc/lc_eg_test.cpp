// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/uc/uc_lc_eg.h"
#include "src/uc/uc_lc.h"
#include "src/spine/spine_device.h"
#include "src/spine/codec/spine_codec_address.h"
}

class LcEgTest : public ::testing::Test {
protected:
  void TearDown() override {
    if (eg) uc_lc_eg_destroy(eg);
    if (local) spine_device_destroy(local);
  }

  void InitDevice() {
    ASSERT_EQ(SPINE_OK, spine_device_create_local(&local, "egDev", "egSKI", "CEM"));

    spine_entity_info_t entity;
    memset(&entity, 0, sizeof(entity));
    strcpy(entity.entity_type, "CEM");
    entity.entity_depth = 1;
    entity.entity[0] = 0;
    spine_device_add_entity(local, entity.entity, entity.entity_depth, entity.entity_type);

    memset(&cfg, 0, sizeof(cfg));
    cfg.device = local;
    spine_addr_make(&cfg.eg_entity_addr, "egDev", 1, 1);
    cfg.entity_type = "CEM";
    cfg.heartbeat_interval_s = 10;
  }

  spine_device_t* local = nullptr;
  uc_lc_eg_t* eg = nullptr;
  uc_lc_eg_config_t cfg = {};
};

TEST_F(LcEgTest, CreateLpcEnabled) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  ASSERT_NE(nullptr, eg);
}

TEST_F(LcEgTest, CreateLppEnabled) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, false, true));
  ASSERT_NE(nullptr, eg);
}

TEST_F(LcEgTest, CreateBothEnabled) {
  InitDevice();
  EXPECT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, true));
  ASSERT_NE(nullptr, eg);
}

TEST_F(LcEgTest, SetAvailable) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  EXPECT_EQ(SPINE_OK, uc_lc_eg_set_available(eg, LC_DIRECTION_CONSUME, true));
}

TEST_F(LcEgTest, GetLimitBeforePeerReady) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_limit_t limit;
  memset(&limit, 0xFF, sizeof(limit));
  uc_lc_eg_get_limit(eg, "nonexistent", LC_DIRECTION_CONSUME, &limit);
}

TEST_F(LcEgTest, GetFailsafeBeforePeerReady) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_failsafe_t fs;
  memset(&fs, 0xFF, sizeof(fs));
  uc_lc_eg_get_failsafe(eg, "nonexistent", LC_DIRECTION_CONSUME, &fs);
}

TEST_F(LcEgTest, GetEcCharacteristicBeforePeerReady) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_ec_characteristic_t ec;
  memset(&ec, 0xFF, sizeof(ec));
  uc_lc_eg_get_ec_characteristic(eg, "nonexistent", LC_DIRECTION_CONSUME, &ec);
}

TEST_F(LcEgTest, WriteLimit) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  limit.value_w = 5000;
  limit.is_limit_active = true;
  uc_lc_eg_write_limit(eg, "session1", LC_DIRECTION_CONSUME, &limit);
}

TEST_F(LcEgTest, WriteFailsafePower) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_eg_write_failsafe_power(eg, "session1", LC_DIRECTION_CONSUME, 1000);
}

TEST_F(LcEgTest, WriteFailsafeDuration) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_eg_write_failsafe_duration(eg, "session1", LC_DIRECTION_CONSUME, 60);
}

TEST_F(LcEgTest, CreateNullCfg) {
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_eg_create(&eg, nullptr, true, false));
}

TEST_F(LcEgTest, CreateBothDisabled) {
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_eg_create(&eg, &cfg, false, false));
}

TEST_F(LcEgTest, DestroyNull) {
  uc_lc_eg_destroy(nullptr);
}

TEST_F(LcEgTest, SetAvailableNull) {
  EXPECT_EQ(SPINE_ERR_INVALID, uc_lc_eg_set_available(nullptr, LC_DIRECTION_CONSUME, true));
}

TEST_F(LcEgTest, WriteLimitNullSession) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  uc_lc_eg_write_limit(eg, nullptr, LC_DIRECTION_CONSUME, &limit);
}

TEST_F(LcEgTest, WriteFailsafeNullSession) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, true, false));
  uc_lc_eg_write_failsafe_power(eg, nullptr, LC_DIRECTION_CONSUME, 1000);
  uc_lc_eg_write_failsafe_duration(eg, nullptr, LC_DIRECTION_CONSUME, 60);
}

TEST_F(LcEgTest, WriteLimitProduce) {
  InitDevice();
  ASSERT_EQ(SPINE_OK, uc_lc_eg_create(&eg, &cfg, false, true));
  uc_lc_limit_t limit;
  memset(&limit, 0, sizeof(limit));
  limit.value_w = 3000;
  uc_lc_eg_write_limit(eg, "session1", LC_DIRECTION_PRODUCE, &limit);
}
