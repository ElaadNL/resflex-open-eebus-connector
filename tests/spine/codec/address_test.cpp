// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_address.h"
}

class AddressTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&a1, 0, sizeof(a1));
    memset(&a2, 0, sizeof(a2));
  }
  spine_addr_t a1, a2;
};

TEST_F(AddressTest, MakeSetsFields) {
  spine_addr_make(&a1, "dev1", 1, 2);
  EXPECT_STREQ("dev1", a1.device);
  EXPECT_EQ(1U, a1.entity[0]);
  EXPECT_EQ(1U, a1.entity_depth);
  EXPECT_EQ(2U, a1.feature);
}

TEST_F(AddressTest, Make2SetsFields) {
  spine_addr_make2(&a1, "dev1", 0, 1, 2);
  EXPECT_STREQ("dev1", a1.device);
  EXPECT_EQ(0U, a1.entity[0]); EXPECT_EQ(1U, a1.entity[1]);
  EXPECT_EQ(2U, a1.entity_depth);
  EXPECT_EQ(2U, a1.feature);
}

TEST_F(AddressTest, EqualSame) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev1", 0, 2);
  EXPECT_TRUE(spine_addr_equal(&a1, &a2));
}

TEST_F(AddressTest, EqualDifferentFeature) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev1", 0, 5);
  EXPECT_FALSE(spine_addr_equal(&a1, &a2));
}

TEST_F(AddressTest, EqualDifferentDevice) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev2", 0, 2);
  EXPECT_FALSE(spine_addr_equal(&a1, &a2));
}

TEST_F(AddressTest, EntityEqualSame) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev1", 0, 5);
  EXPECT_TRUE(spine_addr_entity_equal(&a1, &a2));
}

TEST_F(AddressTest, EntityEqualDifferent) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev2", 1, 2);
  EXPECT_FALSE(spine_addr_entity_equal(&a1, &a2));
}

TEST_F(AddressTest, DeviceEqualSame) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev1", 1, 5);
  EXPECT_TRUE(spine_addr_device_equal(&a1, &a2));
}

TEST_F(AddressTest, EntityIsPrefixSame) {
  spine_addr_make(&a1, "dev1", 0, 2);
  spine_addr_make(&a2, "dev1", 0, 2);
  EXPECT_TRUE(spine_addr_entity_is_prefix(&a1, &a2));
}

TEST_F(AddressTest, EntityIsPrefixShort) {
  spine_addr_make(&a1, "dev1", 0, 0);
  spine_addr_make2(&a2, "dev1", 0, 1, 0);
  EXPECT_TRUE(spine_addr_entity_is_prefix(&a1, &a2));
}

TEST_F(AddressTest, ClearZeroesFields) {
  spine_addr_make(&a1, "dev1", 5, 3);
  spine_addr_clear(&a1);
  EXPECT_EQ(0U, a1.entity_depth);
  EXPECT_EQ(0U, a1.feature);
}

TEST_F(AddressTest, ToStr) {
  spine_addr_make(&a1, "myDevice", 1, 3);
  char buf[512];
  spine_addr_to_str(&a1, buf, sizeof(buf));
  EXPECT_GT(strlen(buf), 0U);
}

TEST_F(AddressTest, EncodeDecodeRoundTrip) {
  spine_addr_make(&a1, "roundTrip", 99, 7);
  cJSON* json = spine_addr_encode(&a1, true);
  ASSERT_NE(nullptr, json);
  spine_err_t err = spine_addr_decode(json, &a2);
  EXPECT_EQ(SPINE_OK, err);
  EXPECT_TRUE(spine_addr_equal(&a1, &a2));
  cJSON_Delete(json);
}

TEST_F(AddressTest, EncodeWithoutDevice) {
  spine_addr_make(&a1, "", 0, 1);
  cJSON* json = spine_addr_encode(&a1, false);
  ASSERT_NE(nullptr, json);
  cJSON_Delete(json);
}

TEST_F(AddressTest, FromNmWithFallback) {
  nm_addr_t nm;
  memset(&nm, 0, sizeof(nm));
  nm.entity_depth = 1;
  nm.entity[0] = 0;
  nm.feature = 3;
  nm.feature_present = true;
  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  spine_addr_from_nm(&addr, &nm, "fallbackDev");
  EXPECT_STREQ("fallbackDev", addr.device);
  EXPECT_EQ(3U, addr.feature);
}

TEST_F(AddressTest, FromNmWithoutFeature) {
  nm_addr_t nm;
  memset(&nm, 0, sizeof(nm));
  strcpy(nm.device, "nmDev");
  nm.entity_depth = 1;
  nm.entity[0] = 0;
  nm.feature_present = false;
  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  spine_addr_from_nm(&addr, &nm, "");
  EXPECT_STREQ("nmDev", addr.device);
  EXPECT_EQ(0U, addr.feature);
}

TEST_F(AddressTest, FromNmNullFallback) {
  nm_addr_t nm;
  memset(&nm, 0, sizeof(nm));
  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  spine_addr_from_nm(&addr, &nm, NULL);
  EXPECT_STREQ("", addr.device);
}

TEST_F(AddressTest, FromNmNullNm) {
  spine_addr_t addr;
  memset(&addr, 0, sizeof(addr));
  spine_addr_from_nm(&addr, NULL, "dev");
  /* should not crash */
}
