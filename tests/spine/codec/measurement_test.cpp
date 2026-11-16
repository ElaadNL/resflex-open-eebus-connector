// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_measurement.h"
}

class MeasurementTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&meas, 0, sizeof(meas));
    memset(&desc, 0, sizeof(desc));
  }
  meas_list_t meas;
  meas_desc_list_t desc;
};

TEST_F(MeasurementTest, MeasDataRoundTrip) {
  meas.count = 1;
  meas.items[0].measurement_id = 1;
  meas.items[0].value.number = 3500;
  meas.items[0].value.scale = 0;
  meas.items[0].value_present = true;

  cJSON* json = meas_list_encode(&meas);
  ASSERT_NE(nullptr, json);

  meas_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].measurement_id);
  EXPECT_EQ(3500, decoded.items[0].value.number);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, MeasDataMultiple) {
  meas.count = 2;
  meas.items[0].measurement_id = 1;
  meas.items[0].value.number = 100;
  meas.items[0].value_present = true;
  meas.items[1].measurement_id = 2;
  meas.items[1].value.number = 200;
  meas.items[1].value_present = true;

  cJSON* json = meas_list_encode(&meas);
  ASSERT_NE(nullptr, json);

  meas_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_list_decode(json, &decoded));
  EXPECT_EQ(2U, decoded.count);
  EXPECT_EQ(200, decoded.items[1].value.number);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, MeasDescRoundTrip) {
  desc.count = 1;
  desc.items[0].measurement_id = 1;
  strcpy(desc.items[0].unit, "W");
  desc.items[0].unit_present = true;

  cJSON* json = meas_desc_list_encode(&desc);
  ASSERT_NE(nullptr, json);

  meas_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].measurement_id);
  EXPECT_STREQ("W", decoded.items[0].unit);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, EncodeNullReturnsNull) {
  EXPECT_EQ(nullptr, meas_list_encode(nullptr));
  EXPECT_EQ(nullptr, meas_desc_list_encode(nullptr));
}

TEST_F(MeasurementTest, DecodeNullReturnsInvalid) {
  EXPECT_EQ(SPINE_ERR_INVALID, meas_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, meas_desc_list_decode(nullptr, nullptr));
}

TEST_F(MeasurementTest, DecodeWrongKeyReturnsParseError) {
  meas_list_t out;
  cJSON* wrong = cJSON_Parse(R"({"wrongKey":[{}]})");
  ASSERT_NE(nullptr, wrong);
  EXPECT_EQ(SPINE_ERR_PARSE, meas_list_decode(wrong, &out));
  cJSON_Delete(wrong);

  meas_desc_list_t out2;
  cJSON* wrong2 = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, meas_desc_list_decode(wrong2, &out2));
  cJSON_Delete(wrong2);
}

TEST_F(MeasurementTest, MeasDataRoundTripAllOptionals) {
  meas.count = 1;
  meas.items[0].measurement_id = 1;
  meas.items[0].value.number = 3500;
  meas.items[0].value.scale = 0;
  meas.items[0].value_present = true;
  strcpy(meas.items[0].value_type, "averageValue");
  meas.items[0].value_type_present = true;
  strcpy(meas.items[0].timestamp, "2025-01-15T12:00:00Z");
  meas.items[0].timestamp_present = true;
  strcpy(meas.items[0].value_source, "measuredValue");
  meas.items[0].value_source_present = true;

  cJSON* json = meas_list_encode(&meas);
  ASSERT_NE(nullptr, json);

  meas_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(3500, decoded.items[0].value.number);
  EXPECT_STREQ("averageValue", decoded.items[0].value_type);
  EXPECT_STREQ("measuredValue", decoded.items[0].value_source);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, MeasDescAllFields) {
  desc.count = 1;
  desc.items[0].measurement_id = 7;
  strcpy(desc.items[0].unit, "kWh");
  desc.items[0].unit_present = true;
  strcpy(desc.items[0].scope_type, "activeEnergy");
  desc.items[0].scope_type_present = true;

  cJSON* json = meas_desc_list_encode(&desc);
  ASSERT_NE(nullptr, json);

  meas_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_desc_list_decode(json, &decoded));
  EXPECT_STREQ("kWh", decoded.items[0].unit);
  EXPECT_STREQ("activeEnergy", decoded.items[0].scope_type);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, MeasDataEvalPeriodTendencyState) {
  meas.count = 1;
  meas.items[0].measurement_id = 1;
  meas.items[0].value.number = 5000;
  meas.items[0].value.scale = 0;
  meas.items[0].value_present = true;
  strcpy(meas.items[0].evaluation_period.start_time, "2025-06-01T00:00:00Z");
  meas.items[0].evaluation_period.start_time_present = true;
  strcpy(meas.items[0].evaluation_period.end_time, "2025-06-01T01:00:00Z");
  meas.items[0].evaluation_period.end_time_present = true;
  strcpy(meas.items[0].value_tendency, "stable");
  meas.items[0].value_tendency_present = true;
  strcpy(meas.items[0].value_state, "clean");
  meas.items[0].value_state_present = true;

  cJSON* json = meas_list_encode(&meas);
  ASSERT_NE(nullptr, json);

  meas_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("2025-06-01T00:00:00Z", decoded.items[0].evaluation_period.start_time);
  EXPECT_TRUE(decoded.items[0].evaluation_period.start_time_present);
  EXPECT_STREQ("2025-06-01T01:00:00Z", decoded.items[0].evaluation_period.end_time);
  EXPECT_TRUE(decoded.items[0].evaluation_period.end_time_present);
  EXPECT_STREQ("stable", decoded.items[0].value_tendency);
  EXPECT_TRUE(decoded.items[0].value_tendency_present);
  EXPECT_STREQ("clean", decoded.items[0].value_state);
  EXPECT_TRUE(decoded.items[0].value_state_present);
  cJSON_Delete(json);
}

TEST_F(MeasurementTest, MeasDescMeasurementTypeCommodityType) {
  desc.count = 1;
  desc.items[0].measurement_id = 5;
  strcpy(desc.items[0].measurement_type, "power");
  desc.items[0].measurement_type_present = true;
  strcpy(desc.items[0].commodity_type, "electricity");
  desc.items[0].commodity_type_present = true;
  strcpy(desc.items[0].unit, "W");
  desc.items[0].unit_present = true;

  cJSON* json = meas_desc_list_encode(&desc);
  ASSERT_NE(nullptr, json);

  meas_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, meas_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("power", decoded.items[0].measurement_type);
  EXPECT_STREQ("electricity", decoded.items[0].commodity_type);
  cJSON_Delete(json);
}
