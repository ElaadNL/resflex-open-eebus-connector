// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_load_ctrl.h"
}

class LoadCtrlTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&list, 0, sizeof(list));
    memset(&desc_list, 0, sizeof(desc_list));
  }
  lc_limit_list_t list;
  lc_limit_desc_list_t desc_list;
};

TEST_F(LoadCtrlTest, LimitRoundTrip) {
  list.count = 1;
  list.items[0].limit_id = 42;
  list.items[0].is_limit_active = true;
  list.items[0].is_limit_active_present = true;
  list.items[0].is_limit_changeable = false;
  list.items[0].is_limit_changeable_present = true;
  list.items[0].value.number = 5000;
  list.items[0].value.scale = 0;
  list.items[0].value_present = true;
  strcpy(list.items[0].time_period.start_time, "PT0S");
  list.items[0].time_period.start_time_present = true;

  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  lc_limit_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(42U, decoded.items[0].limit_id);
  EXPECT_TRUE(decoded.items[0].is_limit_active);
  EXPECT_EQ(5000, decoded.items[0].value.number);

  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitEmptyList) {
  list.count = 0;
  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  memset(&list, 0xFF, sizeof(list));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &list));
  EXPECT_EQ(0U, list.count);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitMultipleItems) {
  list.count = 3;
  for (uint8_t i = 0; i < 3; i++) {
    list.items[i].limit_id = i + 1;
    list.items[i].value.number = (i + 1) * 1000;
    list.items[i].value_present = true;
  }
  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  memset(&list, 0, sizeof(list));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &list));
  EXPECT_EQ(3U, list.count);
  EXPECT_EQ(1U, list.items[0].limit_id);
  EXPECT_EQ(2000, list.items[1].value.number);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitDescRoundTrip) {
  desc_list.count = 1;
  desc_list.items[0].limit_id = 42;
  strcpy(desc_list.items[0].limit_type, "maxValueLimit");
  desc_list.items[0].limit_type_present = true;
  strcpy(desc_list.items[0].limit_direction, "consume");
  desc_list.items[0].limit_direction_present = true;
  strcpy(desc_list.items[0].unit, "W");
  desc_list.items[0].unit_present = true;

  cJSON* json = lc_limit_desc_list_encode(&desc_list);
  ASSERT_NE(nullptr, json);

  lc_limit_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(42U, decoded.items[0].limit_id);
  EXPECT_STREQ("maxValueLimit", decoded.items[0].limit_type);
  EXPECT_STREQ("W", decoded.items[0].unit);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, ScaledNumberEncodeDecode) {
  spine_scaled_number_t v = {12345, -3};
  cJSON* json = spine_scaled_number_encode(&v);
  ASSERT_NE(nullptr, json);
  spine_scaled_number_t decoded;
  EXPECT_EQ(SPINE_OK, spine_scaled_number_decode(json, &decoded));
  EXPECT_EQ(v.number, decoded.number);
  EXPECT_EQ(v.scale, decoded.scale);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, ScaledNumberHelpers) {
  spine_scaled_number_t v = {5000, -2};
  EXPECT_DOUBLE_EQ(50.0, spine_scaled_number_to_double(&v));
  spine_scaled_number_from_double(75.0, &v);
  EXPECT_EQ(75, v.number);
  EXPECT_EQ(0, v.scale);
}

TEST_F(LoadCtrlTest, LimitDescAllOptionalFields) {
  desc_list.count = 1;
  desc_list.items[0].limit_id = 1;
  strcpy(desc_list.items[0].limit_type, "maxValueLimit");
  desc_list.items[0].limit_type_present = true;
  strcpy(desc_list.items[0].limit_category, "obligation");
  desc_list.items[0].limit_category_present = true;
  strcpy(desc_list.items[0].limit_direction, "consume");
  desc_list.items[0].limit_direction_present = true;
  desc_list.items[0].measurement_id = 99;
  desc_list.items[0].measurement_id_present = true;
  strcpy(desc_list.items[0].unit, "W");
  desc_list.items[0].unit_present = true;
  strcpy(desc_list.items[0].scope_type, "activePowerLimit");
  desc_list.items[0].scope_type_present = true;
  strcpy(desc_list.items[0].label, "MyLabel");
  desc_list.items[0].label_present = true;
  strcpy(desc_list.items[0].description, "MyDescription");
  desc_list.items[0].description_present = true;

  cJSON* json = lc_limit_desc_list_encode(&desc_list);
  ASSERT_NE(nullptr, json);

  lc_limit_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].limit_id);
  EXPECT_STREQ("maxValueLimit", decoded.items[0].limit_type);
  EXPECT_TRUE(decoded.items[0].limit_type_present);
  EXPECT_STREQ("obligation", decoded.items[0].limit_category);
  EXPECT_TRUE(decoded.items[0].limit_category_present);
  EXPECT_STREQ("consume", decoded.items[0].limit_direction);
  EXPECT_TRUE(decoded.items[0].limit_direction_present);
  EXPECT_EQ(99U, decoded.items[0].measurement_id);
  EXPECT_TRUE(decoded.items[0].measurement_id_present);
  EXPECT_STREQ("W", decoded.items[0].unit);
  EXPECT_STREQ("activePowerLimit", decoded.items[0].scope_type);
  EXPECT_TRUE(decoded.items[0].scope_type_present);
  EXPECT_STREQ("MyLabel", decoded.items[0].label);
  EXPECT_TRUE(decoded.items[0].label_present);
  EXPECT_STREQ("MyDescription", decoded.items[0].description);
  EXPECT_TRUE(decoded.items[0].description_present);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitDescOnlyRequired) {
  desc_list.count = 1;
  desc_list.items[0].limit_id = 7;

  cJSON* json = lc_limit_desc_list_encode(&desc_list);
  ASSERT_NE(nullptr, json);

  lc_limit_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(7U, decoded.items[0].limit_id);
  EXPECT_FALSE(decoded.items[0].limit_type_present);
  EXPECT_FALSE(decoded.items[0].unit_present);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitDescMultipleItems) {
  desc_list.count = 2;
  desc_list.items[0].limit_id = 1;
  strcpy(desc_list.items[0].unit, "W");
  desc_list.items[0].unit_present = true;
  desc_list.items[1].limit_id = 2;
  strcpy(desc_list.items[1].unit, "kW");
  desc_list.items[1].unit_present = true;

  cJSON* json = lc_limit_desc_list_encode(&desc_list);
  ASSERT_NE(nullptr, json);

  lc_limit_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &decoded));
  ASSERT_EQ(2U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].limit_id);
  EXPECT_STREQ("W", decoded.items[0].unit);
  EXPECT_EQ(2U, decoded.items[1].limit_id);
  EXPECT_STREQ("kW", decoded.items[1].unit);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitListOnlyRequired) {
  list.count = 1;
  list.items[0].limit_id = 1;

  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  memset(&list, 0, sizeof(list));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &list));
  ASSERT_EQ(1U, list.count);
  EXPECT_EQ(1U, list.items[0].limit_id);
  EXPECT_FALSE(list.items[0].is_limit_active_present);
  EXPECT_FALSE(list.items[0].is_limit_changeable_present);
  EXPECT_FALSE(list.items[0].value_present);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitListTimePeriodBoth) {
  list.count = 1;
  list.items[0].limit_id = 1;
  list.items[0].value.number = 5000;
  list.items[0].value.scale = 0;
  list.items[0].value_present = true;
  strcpy(list.items[0].time_period.start_time, "PT0S");
  list.items[0].time_period.start_time_present = true;
  strcpy(list.items[0].time_period.end_time, "PT3600S");
  list.items[0].time_period.end_time_present = true;

  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  lc_limit_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_TRUE(decoded.items[0].time_period.start_time_present);
  EXPECT_STREQ("PT0S", decoded.items[0].time_period.start_time);
  EXPECT_TRUE(decoded.items[0].time_period.end_time_present);
  EXPECT_STREQ("PT3600S", decoded.items[0].time_period.end_time);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitListValueAbsent) {
  list.count = 1;
  list.items[0].limit_id = 1;
  list.items[0].is_limit_active = true;
  list.items[0].is_limit_active_present = true;
  list.items[0].is_limit_changeable = false;
  list.items[0].is_limit_changeable_present = true;

  cJSON* json = lc_limit_list_encode(&list);
  ASSERT_NE(nullptr, json);

  lc_limit_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_TRUE(decoded.items[0].is_limit_active);
  EXPECT_FALSE(decoded.items[0].value_present);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitDescEmptyList) {
  desc_list.count = 0;
  cJSON* json = lc_limit_desc_list_encode(&desc_list);
  ASSERT_NE(nullptr, json);

  memset(&desc_list, 0xFF, sizeof(desc_list));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &desc_list));
  EXPECT_EQ(0U, desc_list.count);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitDescDecodeMissingKey) {
  cJSON* json = cJSON_Parse(R"({"loadControlLimitDescriptionListData":[{}]})");
  ASSERT_NE(nullptr, json);
  lc_limit_desc_list_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_OK, lc_limit_desc_list_decode(json, &out));
  EXPECT_EQ(0U, out.count);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, ScaledNumberScaleZero) {
  spine_scaled_number_t v = {5000, 0};
  cJSON* json = spine_scaled_number_encode(&v);
  ASSERT_NE(nullptr, json);

  spine_scaled_number_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_scaled_number_decode(json, &decoded));
  EXPECT_EQ(5000, decoded.number);
  EXPECT_EQ(0, decoded.scale);
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, ScaledNumberToDoublePositive) {
  spine_scaled_number_t v = {5, 2};
  EXPECT_DOUBLE_EQ(500.0, spine_scaled_number_to_double(&v));
}

TEST_F(LoadCtrlTest, ScaledNumberToDoubleZero) {
  spine_scaled_number_t v = {42, 0};
  EXPECT_DOUBLE_EQ(42.0, spine_scaled_number_to_double(&v));
}

TEST_F(LoadCtrlTest, ScaledNumberFromDoubleFractional) {
  spine_scaled_number_t v;
  spine_scaled_number_from_double(75.5, &v);
  EXPECT_EQ(75, v.number);
  EXPECT_EQ(0, v.scale);
}

TEST_F(LoadCtrlTest, ScaledNumberEncodeNull) {
  EXPECT_EQ(nullptr, spine_scaled_number_encode(nullptr));
}

TEST_F(LoadCtrlTest, ScaledNumberDecodeNull) {
  spine_scaled_number_t v;
  EXPECT_EQ(SPINE_ERR_INVALID, spine_scaled_number_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, spine_scaled_number_decode(nullptr, &v));
  cJSON* j = cJSON_CreateObject();
  EXPECT_EQ(SPINE_ERR_INVALID, spine_scaled_number_decode(j, nullptr));
  cJSON_Delete(j);
}

TEST_F(LoadCtrlTest, LimitListEncodeNull) {
  EXPECT_EQ(nullptr, lc_limit_list_encode(nullptr));
}

TEST_F(LoadCtrlTest, LimitListDecodeNull) {
  lc_limit_list_t out;
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_list_decode(nullptr, &out));
  cJSON* j = cJSON_CreateObject();
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_list_decode(j, nullptr));
  cJSON_Delete(j);
}

TEST_F(LoadCtrlTest, LimitDescEncodeNull) {
  EXPECT_EQ(nullptr, lc_limit_desc_list_encode(nullptr));
}

TEST_F(LoadCtrlTest, LimitDescDecodeNull) {
  lc_limit_desc_list_t out;
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_desc_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_desc_list_decode(nullptr, &out));
  cJSON* j = cJSON_CreateObject();
  EXPECT_EQ(SPINE_ERR_INVALID, lc_limit_desc_list_decode(j, nullptr));
  cJSON_Delete(j);
}

TEST_F(LoadCtrlTest, LimitListDecodeTopLevelNotArray) {
  cJSON* json = cJSON_Parse(R"({"loadControlLimitListData":"notAnArray"})");
  ASSERT_NE(nullptr, json);
  lc_limit_list_t out;
  EXPECT_EQ(SPINE_ERR_PARSE, lc_limit_list_decode(json, &out));
  cJSON_Delete(json);
}

TEST_F(LoadCtrlTest, LimitListDecodeLimitDataAbsent) {
  cJSON* json = cJSON_Parse(R"({"loadControlLimitListData":[{}]})");
  ASSERT_NE(nullptr, json);
  lc_limit_list_t out;
  memset(&out, 0xFF, sizeof(out));
  EXPECT_EQ(SPINE_OK, lc_limit_list_decode(json, &out));
  EXPECT_EQ(0U, out.count);
  cJSON_Delete(json);
}
