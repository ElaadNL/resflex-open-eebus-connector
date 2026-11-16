// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_elec_conn.h"
}

TEST(ElecConnTest, ParamDescListRoundTrip) {
  ec_param_desc_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].parameter_id = 1;
  strcpy(list.items[0].description, "Voltage L1");
  list.items[0].description_present = true;

  cJSON* json = ec_param_desc_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_param_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_param_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].parameter_id);
  EXPECT_STREQ("Voltage L1", decoded.items[0].description);
  cJSON_Delete(json);
}

TEST(ElecConnTest, PermittedSetListRoundTrip) {
  ec_permitted_set_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].parameter_id = 1;
  list.items[0].ranges[0].min.number = 0;
  list.items[0].ranges[0].min.scale = 0;
  list.items[0].ranges[0].min_present = true;
  list.items[0].ranges[0].max.number = 10000;
  list.items[0].ranges[0].max.scale = 0;
  list.items[0].ranges[0].max_present = true;

  cJSON* json = ec_permitted_set_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_permitted_set_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_permitted_set_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].parameter_id);
  cJSON_Delete(json);
}

TEST(ElecConnTest, CharacteristicListRoundTrip) {
  ec_characteristic_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].parameter_id = 1;
  list.items[0].value.number = 23000;
  list.items[0].value.scale = -2;

  cJSON* json = ec_characteristic_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_characteristic_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_characteristic_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].parameter_id);
  cJSON_Delete(json);
}

TEST(ElecConnTest, DescriptionListRoundTrip) {
  ec_description_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  strcpy(list.items[0].power_supply_type, "ac");
  list.items[0].power_supply_type_present = true;

  cJSON* json = ec_description_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_description_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_description_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].electrical_connection_id);
  EXPECT_STREQ("ac", decoded.items[0].power_supply_type);
  cJSON_Delete(json);
}

TEST(ElecConnTest, EncodeNullReturnsNull) {
  EXPECT_EQ(nullptr, ec_param_desc_list_encode(nullptr));
  EXPECT_EQ(nullptr, ec_permitted_set_list_encode(nullptr));
  EXPECT_EQ(nullptr, ec_characteristic_list_encode(nullptr));
  EXPECT_EQ(nullptr, ec_description_list_encode(nullptr));
}

TEST(ElecConnTest, DecodeNullReturnsInvalid) {
  EXPECT_EQ(SPINE_ERR_INVALID, ec_param_desc_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, ec_permitted_set_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, ec_characteristic_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, ec_description_list_decode(nullptr, nullptr));
}

TEST(ElecConnTest, DecodeWrongKeyReturnsParseError) {
  ec_param_desc_list_t out;
  cJSON* wrong = cJSON_Parse(R"({"wrongKey":[{}]})");
  ASSERT_NE(nullptr, wrong);
  EXPECT_EQ(SPINE_ERR_PARSE, ec_param_desc_list_decode(wrong, &out));
  cJSON_Delete(wrong);

  ec_permitted_set_list_t out2;
  cJSON* wrong2 = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, ec_permitted_set_list_decode(wrong2, &out2));
  cJSON_Delete(wrong2);

  ec_characteristic_list_t out3;
  cJSON* wrong3 = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, ec_characteristic_list_decode(wrong3, &out3));
  cJSON_Delete(wrong3);

  ec_description_list_t out4;
  cJSON* wrong4 = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, ec_description_list_decode(wrong4, &out4));
  cJSON_Delete(wrong4);
}

TEST(ElecConnTest, ParamDescListEmpty) {
  ec_param_desc_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 0;
  cJSON* json = ec_param_desc_list_encode(&list);
  ASSERT_NE(nullptr, json);
  ec_param_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_param_desc_list_decode(json, &decoded));
  EXPECT_EQ(0U, decoded.count);
  cJSON_Delete(json);
}

TEST(ElecConnTest, MultipleParamDescs) {
  ec_param_desc_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 3;
  for (uint8_t i = 0; i < 3; i++) {
    list.items[i].parameter_id = i + 1;
  }

  cJSON* json = ec_param_desc_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_param_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_param_desc_list_decode(json, &decoded));
  EXPECT_EQ(3U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].parameter_id);
  EXPECT_EQ(3U, decoded.items[2].parameter_id);
  cJSON_Delete(json);
}

TEST(ElecConnTest, ParamDescAllOptionals) {
  ec_param_desc_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 2;
  list.items[0].measurement_id = 42;
  list.items[0].measurement_id_present = true;
  strcpy(list.items[0].voltage_type, "AC");
  list.items[0].voltage_type_present = true;
  strcpy(list.items[0].ac_measured_phases, "phaseNeutral");
  list.items[0].ac_measured_phases_present = true;
  strcpy(list.items[0].ac_measured_in_ref_to, "ground");
  list.items[0].ac_measured_in_ref_to_present = true;
  strcpy(list.items[0].ac_measurement_type, "rms");
  list.items[0].ac_measurement_type_present = true;
  strcpy(list.items[0].ac_measurement_variant, "harmonic");
  list.items[0].ac_measurement_variant_present = true;
  list.items[0].ac_measured_harmonic = 3;
  list.items[0].ac_measured_harmonic_present = true;
  strcpy(list.items[0].scope_type, "electricCurrent");
  list.items[0].scope_type_present = true;
  strcpy(list.items[0].label, "Current L1");
  list.items[0].label_present = true;
  strcpy(list.items[0].description, "Current measurement phase L1");
  list.items[0].description_present = true;

  cJSON* json = ec_param_desc_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_param_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_param_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].electrical_connection_id);
  EXPECT_EQ(42U, decoded.items[0].measurement_id);
  EXPECT_TRUE(decoded.items[0].measurement_id_present);
  EXPECT_STREQ("AC", decoded.items[0].voltage_type);
  EXPECT_STREQ("phaseNeutral", decoded.items[0].ac_measured_phases);
  EXPECT_STREQ("ground", decoded.items[0].ac_measured_in_ref_to);
  EXPECT_STREQ("rms", decoded.items[0].ac_measurement_type);
  EXPECT_STREQ("harmonic", decoded.items[0].ac_measurement_variant);
  EXPECT_EQ(3U, decoded.items[0].ac_measured_harmonic);
  EXPECT_STREQ("electricCurrent", decoded.items[0].scope_type);
  EXPECT_STREQ("Current L1", decoded.items[0].label);
  EXPECT_STREQ("Current measurement phase L1", decoded.items[0].description);
  cJSON_Delete(json);
}

TEST(ElecConnTest, DescriptionListAllFields) {
  ec_description_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  strcpy(list.items[0].power_supply_type, "ac");
  list.items[0].power_supply_type_present = true;
  list.items[0].ac_connected_phases = 3;
  list.items[0].ac_connected_phases_present = true;
  strcpy(list.items[0].ac_rms_period_duration, "PT1S");
  list.items[0].ac_rms_period_duration_present = true;
  strcpy(list.items[0].positive_energy_direction, "consume");
  list.items[0].positive_energy_direction_present = true;
  strcpy(list.items[0].scope_type, "electricPower");
  list.items[0].scope_type_present = true;
  strcpy(list.items[0].label, "Main connection");
  list.items[0].label_present = true;
  strcpy(list.items[0].description, "Grid connection point");
  list.items[0].description_present = true;

  cJSON* json = ec_description_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_description_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_description_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].electrical_connection_id);
  EXPECT_TRUE(decoded.items[0].power_supply_type_present);
  EXPECT_STREQ("ac", decoded.items[0].power_supply_type);
  EXPECT_TRUE(decoded.items[0].ac_connected_phases_present);
  EXPECT_EQ(3U, decoded.items[0].ac_connected_phases);
  EXPECT_TRUE(decoded.items[0].ac_rms_period_duration_present);
  EXPECT_STREQ("PT1S", decoded.items[0].ac_rms_period_duration);
  EXPECT_TRUE(decoded.items[0].positive_energy_direction_present);
  EXPECT_STREQ("consume", decoded.items[0].positive_energy_direction);
  EXPECT_TRUE(decoded.items[0].scope_type_present);
  EXPECT_STREQ("electricPower", decoded.items[0].scope_type);
  EXPECT_TRUE(decoded.items[0].label_present);
  EXPECT_STREQ("Main connection", decoded.items[0].label);
  EXPECT_TRUE(decoded.items[0].description_present);
  EXPECT_STREQ("Grid connection point", decoded.items[0].description);
  cJSON_Delete(json);
}

TEST(ElecConnTest, DescriptionListMultipleItems) {
  ec_description_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 2;
  list.items[0].electrical_connection_id = 1;
  strcpy(list.items[0].power_supply_type, "ac");
  list.items[0].power_supply_type_present = true;
  list.items[1].electrical_connection_id = 2;
  strcpy(list.items[1].power_supply_type, "dc");
  list.items[1].power_supply_type_present = true;

  cJSON* json = ec_description_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_description_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_description_list_decode(json, &decoded));
  ASSERT_EQ(2U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].electrical_connection_id);
  EXPECT_STREQ("ac", decoded.items[0].power_supply_type);
  EXPECT_EQ(2U, decoded.items[1].electrical_connection_id);
  EXPECT_STREQ("dc", decoded.items[1].power_supply_type);
  cJSON_Delete(json);
}

TEST(ElecConnTest, CharacteristicListAllOptionals) {
  ec_characteristic_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 1;
  list.items[0].characteristic_id = 1;
  strcpy(list.items[0].characteristic_context, "entity");
  list.items[0].characteristic_context_present = true;
  strcpy(list.items[0].characteristic_type, "current");
  list.items[0].characteristic_type_present = true;
  list.items[0].value.number = 5000;
  list.items[0].value.scale = -2;
  list.items[0].value_present = true;
  strcpy(list.items[0].unit, "A");
  list.items[0].unit_present = true;

  cJSON* json = ec_characteristic_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_characteristic_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_characteristic_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_TRUE(decoded.items[0].characteristic_context_present);
  EXPECT_STREQ("entity", decoded.items[0].characteristic_context);
  EXPECT_TRUE(decoded.items[0].characteristic_type_present);
  EXPECT_STREQ("current", decoded.items[0].characteristic_type);
  EXPECT_TRUE(decoded.items[0].value_present);
  EXPECT_EQ(5000, decoded.items[0].value.number);
  EXPECT_TRUE(decoded.items[0].unit_present);
  EXPECT_STREQ("A", decoded.items[0].unit);
  cJSON_Delete(json);
}

TEST(ElecConnTest, CharacteristicListWithoutValue) {
  ec_characteristic_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 1;
  list.items[0].characteristic_id = 1;
  strcpy(list.items[0].unit, "V");
  list.items[0].unit_present = true;

  cJSON* json = ec_characteristic_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_characteristic_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_characteristic_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_FALSE(decoded.items[0].value_present);
  EXPECT_TRUE(decoded.items[0].unit_present);
  EXPECT_STREQ("V", decoded.items[0].unit);
  cJSON_Delete(json);
}

TEST(ElecConnTest, DescriptionListEmpty) {
  ec_description_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 0;
  cJSON* json = ec_description_list_encode(&list);
  ASSERT_NE(nullptr, json);
  memset(&list, 0xFF, sizeof(list));
  EXPECT_EQ(SPINE_OK, ec_description_list_decode(json, &list));
  EXPECT_EQ(0U, list.count);
  cJSON_Delete(json);
}

TEST(ElecConnTest, PermittedSetValuesOnly) {
  ec_permitted_set_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 1;
  list.items[0].value_count = 2;
  list.items[0].values[0].number = 1000;
  list.items[0].values[0].scale = 0;
  list.items[0].values[1].number = 2000;
  list.items[0].values[1].scale = 0;

  cJSON* json = ec_permitted_set_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_permitted_set_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_permitted_set_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  ASSERT_EQ(2U, decoded.items[0].value_count);
  EXPECT_EQ(1000, decoded.items[0].values[0].number);
  EXPECT_EQ(2000, decoded.items[0].values[1].number);
  cJSON_Delete(json);
}

TEST(ElecConnTest, PermittedSetRangeMinOnly) {
  ec_permitted_set_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 1;
  list.items[0].range_count = 1;
  list.items[0].ranges[0].min.number = 0;
  list.items[0].ranges[0].min.scale = 0;
  list.items[0].ranges[0].min_present = true;

  cJSON* json = ec_permitted_set_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_permitted_set_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_permitted_set_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  ASSERT_EQ(1U, decoded.items[0].range_count);
  EXPECT_TRUE(decoded.items[0].ranges[0].min_present);
  EXPECT_FALSE(decoded.items[0].ranges[0].max_present);
  cJSON_Delete(json);
}

TEST(ElecConnTest, PermittedSetNoPvs) {
  ec_permitted_set_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].electrical_connection_id = 1;
  list.items[0].parameter_id = 1;

  cJSON* json = ec_permitted_set_list_encode(&list);
  ASSERT_NE(nullptr, json);

  ec_permitted_set_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_permitted_set_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(0U, decoded.items[0].value_count);
  EXPECT_EQ(0U, decoded.items[0].range_count);
  cJSON_Delete(json);
}

TEST(ElecConnTest, DescriptionListMissingInnerData) {
  cJSON* json = cJSON_Parse(
      R"({"electricalConnectionDescriptionListData":[{}]})");
  ASSERT_NE(nullptr, json);
  ec_description_list_t out;
  memset(&out, 0xFF, sizeof(out));
  EXPECT_EQ(SPINE_OK, ec_description_list_decode(json, &out));
  EXPECT_EQ(0U, out.count);
  cJSON_Delete(json);
}

TEST(ElecConnTest, CharacteristicListEmpty) {
  ec_characteristic_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 0;
  cJSON* json = ec_characteristic_list_encode(&list);
  ASSERT_NE(nullptr, json);
  memset(&list, 0xFF, sizeof(list));
  EXPECT_EQ(SPINE_OK, ec_characteristic_list_decode(json, &list));
  EXPECT_EQ(0U, list.count);
  cJSON_Delete(json);
}

TEST(ElecConnTest, CharacteristicListMultipleItems) {
  ec_characteristic_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 2;
  for (uint8_t i = 0; i < 2; i++) {
    list.items[i].electrical_connection_id = i + 1;
    list.items[i].parameter_id = i + 1;
    list.items[i].characteristic_id = i + 1;
  }
  cJSON* json = ec_characteristic_list_encode(&list);
  ASSERT_NE(nullptr, json);
  ec_characteristic_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, ec_characteristic_list_decode(json, &decoded));
  EXPECT_EQ(2U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].electrical_connection_id);
  EXPECT_EQ(2U, decoded.items[1].electrical_connection_id);
  cJSON_Delete(json);
}
