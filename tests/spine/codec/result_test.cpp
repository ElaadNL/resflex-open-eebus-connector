// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_result.h"
}

class ResultTest : public ::testing::Test {
protected:
  void SetUp() override { memset(&data, 0, sizeof(data)); }
  spine_result_data_t data;
};

TEST_F(ResultTest, EncodeDecodeRoundTrip) {
  data.error_number = 2;
  data.description_present = true;
  strcpy(data.description, "timeout occurred");

  cJSON* json = spine_result_data_encode(&data);
  ASSERT_NE(nullptr, json);

  spine_result_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_result_data_decode(json, &decoded));
  EXPECT_EQ(data.error_number, decoded.error_number);
  EXPECT_STREQ(data.description, decoded.description);

  cJSON_Delete(json);
}

TEST_F(ResultTest, EncodeDecodeNoDescription) {
  data.error_number = 0;
  data.description[0] = '\0';

  cJSON* json = spine_result_data_encode(&data);
  ASSERT_NE(nullptr, json);

  spine_result_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, spine_result_data_decode(json, &decoded));
  EXPECT_EQ(0, decoded.error_number);

  cJSON_Delete(json);
}

TEST_F(ResultTest, EncodeEveryErrorNumber) {
  for (int i = 0; i <= 9; i++) {
    data.error_number = i;
    cJSON* json = spine_result_data_encode(&data);
    ASSERT_NE(nullptr, json) << "failed at error_number=" << i;
    spine_result_data_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    EXPECT_EQ(SPINE_OK, spine_result_data_decode(json, &decoded));
    EXPECT_EQ(i, decoded.error_number);
    cJSON_Delete(json);
  }
}
