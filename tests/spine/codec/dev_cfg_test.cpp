// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_dev_cfg.h"
}

TEST(DevCfgTest, KVListRoundTrip) {
  dev_cfg_kv_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 2;
  list.items[0].key_id = 1;
  list.items[0].value_type = DEV_CFG_VALUE_STRING;
  strcpy(list.items[0].value.string_val, "1.0");
  list.items[0].value_present = true;
  list.items[1].key_id = 2;
  list.items[1].value_type = DEV_CFG_VALUE_INTEGER;
  list.items[1].value.integer_val = 5000;
  list.items[1].value_present = true;

  cJSON* json = dev_cfg_kv_list_encode(&list);
  ASSERT_NE(nullptr, json);

  dev_cfg_kv_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json, &decoded));
  ASSERT_EQ(2U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].key_id);
  EXPECT_STREQ("1.0", decoded.items[0].value.string_val);
  EXPECT_EQ(2U, decoded.items[1].key_id);
  EXPECT_EQ(5000, decoded.items[1].value.integer_val);
  cJSON_Delete(json);
}

TEST(DevCfgTest, KVListEmpty) {
  dev_cfg_kv_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 0;
  cJSON* json = dev_cfg_kv_list_encode(&list);
  ASSERT_NE(nullptr, json);
  dev_cfg_kv_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json, &decoded));
  EXPECT_EQ(0U, decoded.count);
  cJSON_Delete(json);
}

TEST(DevCfgTest, KVDescListRoundTrip) {
  dev_cfg_kv_desc_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].key_id = 1;
  strcpy(list.items[0].key_name, "failsafeConsumptionActivePowerLimit");
  list.items[0].key_name_present = true;
  strcpy(list.items[0].unit, "W");
  list.items[0].unit_present = true;

  cJSON* json = dev_cfg_kv_desc_list_encode(&list);
  ASSERT_NE(nullptr, json);

  dev_cfg_kv_desc_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_desc_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].key_id);
  EXPECT_STREQ("failsafeConsumptionActivePowerLimit", decoded.items[0].key_name);
  cJSON_Delete(json);
}

TEST(DevCfgTest, EncodeNullReturnsNull) {
  EXPECT_EQ(nullptr, dev_cfg_kv_list_encode(nullptr));
  EXPECT_EQ(nullptr, dev_cfg_kv_desc_list_encode(nullptr));
}

TEST(DevCfgTest, DecodeNullReturnsInvalid) {
  EXPECT_EQ(SPINE_ERR_INVALID, dev_cfg_kv_list_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, dev_cfg_kv_desc_list_decode(nullptr, nullptr));
}

TEST(DevCfgTest, DecodeWrongKeyReturnsParseError) {
  dev_cfg_kv_list_t out;
  cJSON* wrong = cJSON_Parse(R"({"wrongKey":[{}]})");
  ASSERT_NE(nullptr, wrong);
  EXPECT_EQ(SPINE_ERR_PARSE, dev_cfg_kv_list_decode(wrong, &out));
  cJSON_Delete(wrong);

  dev_cfg_kv_desc_list_t out2;
  cJSON* wrong2 = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, dev_cfg_kv_desc_list_decode(wrong2, &out2));
  cJSON_Delete(wrong2);
}

TEST(DevCfgTest, KVListDurationValueType) {
  dev_cfg_kv_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].key_id = 1;
  list.items[0].value_type = DEV_CFG_VALUE_DURATION;
  strcpy(list.items[0].value.duration_val, "PT7200S");
  list.items[0].value_present = true;

  cJSON* json = dev_cfg_kv_list_encode(&list);
  ASSERT_NE(nullptr, json);

  dev_cfg_kv_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("PT7200S", decoded.items[0].value.duration_val);
  cJSON_Delete(json);
}

TEST(DevCfgTest, KVListDateValueType) {
  dev_cfg_kv_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 1;
  list.items[0].key_id = 1;
  list.items[0].value_type = DEV_CFG_VALUE_DATE;
  strcpy(list.items[0].value.string_val, "2025-01-15");
  list.items[0].value_present = true;

  cJSON* json = dev_cfg_kv_list_encode(&list);
  ASSERT_NE(nullptr, json);

  dev_cfg_kv_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("2025-01-15", decoded.items[0].value.string_val);

  /* dateTime type */
  dev_cfg_kv_list_t list2;
  memset(&list2, 0, sizeof(list2));
  list2.count = 1;
  list2.items[0].key_id = 2;
  list2.items[0].value_type = DEV_CFG_VALUE_DATE_TIME;
  strcpy(list2.items[0].value.string_val, "2025-01-15T12:00:00Z");
  list2.items[0].value_present = true;

  cJSON* json2 = dev_cfg_kv_list_encode(&list2);
  ASSERT_NE(nullptr, json2);
  dev_cfg_kv_list_t decoded2;
  memset(&decoded2, 0, sizeof(decoded2));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json2, &decoded2));
  ASSERT_EQ(1U, decoded2.count);
  EXPECT_STREQ("2025-01-15T12:00:00Z", decoded2.items[0].value.string_val);

  /* time type */
  dev_cfg_kv_list_t list3;
  memset(&list3, 0, sizeof(list3));
  list3.count = 1;
  list3.items[0].key_id = 3;
  list3.items[0].value_type = DEV_CFG_VALUE_TIME;
  strcpy(list3.items[0].value.string_val, "12:00:00");
  list3.items[0].value_present = true;

  cJSON* json3 = dev_cfg_kv_list_encode(&list3);
  ASSERT_NE(nullptr, json3);
  dev_cfg_kv_list_t decoded3;
  memset(&decoded3, 0, sizeof(decoded3));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json3, &decoded3));
  ASSERT_EQ(1U, decoded3.count);
  EXPECT_STREQ("12:00:00", decoded3.items[0].value.string_val);

  cJSON_Delete(json);
  cJSON_Delete(json2);
  cJSON_Delete(json3);
}

TEST(DevCfgTest, KVAllValueTypes) {
  dev_cfg_kv_list_t list;
  memset(&list, 0, sizeof(list));
  list.count = 4;
  list.items[0].key_id = 1;
  list.items[0].value_type = DEV_CFG_VALUE_STRING;
  strcpy(list.items[0].value.string_val, "str");
  list.items[0].value_present = true;
  list.items[1].key_id = 2;
  list.items[1].value_type = DEV_CFG_VALUE_INTEGER;
  list.items[1].value.integer_val = 42;
  list.items[1].value_present = true;
  list.items[2].key_id = 3;
  list.items[2].value_type = DEV_CFG_VALUE_BOOLEAN;
  list.items[2].value.boolean_val = true;
  list.items[2].value_present = true;
  list.items[3].key_id = 4;
  list.items[3].value_type = DEV_CFG_VALUE_SCALED_NUM;
  list.items[3].value.scaled_num_val.number = 5000;
  list.items[3].value.scaled_num_val.scale = -2;
  list.items[3].value_present = true;

  cJSON* json = dev_cfg_kv_list_encode(&list);
  ASSERT_NE(nullptr, json);

  dev_cfg_kv_list_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_cfg_kv_list_decode(json, &decoded));
  ASSERT_EQ(4U, decoded.count);
  EXPECT_STREQ("str", decoded.items[0].value.string_val);
  EXPECT_EQ(42, decoded.items[1].value.integer_val);
  EXPECT_TRUE(decoded.items[2].value.boolean_val);
  EXPECT_EQ(5000, decoded.items[3].value.scaled_num_val.number);
  EXPECT_EQ(-2, decoded.items[3].value.scaled_num_val.scale);
  cJSON_Delete(json);
}
