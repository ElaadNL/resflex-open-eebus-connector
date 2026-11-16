// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_dev_diag.h"
}

TEST(DevDiagTest, HeartbeatRoundTrip) {
  dev_diag_heartbeat_t hb;
  memset(&hb, 0, sizeof(hb));
  hb.heartbeat_counter = 7;
  hb.heartbeat_counter_present = true;
  strcpy(hb.heartbeat_timeout, "PT120S");
  hb.heartbeat_timeout_present = true;

  cJSON* json = dev_diag_heartbeat_encode(&hb);
  ASSERT_NE(nullptr, json);

  dev_diag_heartbeat_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_diag_heartbeat_decode(json, &decoded));
  EXPECT_EQ(7U, decoded.heartbeat_counter);
  EXPECT_STREQ("PT120S", decoded.heartbeat_timeout);
  cJSON_Delete(json);
}

TEST(DevDiagTest, StateRoundTrip) {
  dev_diag_state_t st;
  memset(&st, 0, sizeof(st));
  strcpy(st.operating_state, "NORMAL_OPERATION");
  st.operating_state_present = true;

  cJSON* json = dev_diag_state_encode(&st);
  ASSERT_NE(nullptr, json);

  dev_diag_state_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_diag_state_decode(json, &decoded));
  EXPECT_STREQ("NORMAL_OPERATION", decoded.operating_state);
  cJSON_Delete(json);
}

TEST(DevDiagTest, StateNoData) {
  dev_diag_state_t st;
  memset(&st, 0, sizeof(st));

  cJSON* json = dev_diag_state_encode(&st);
  ASSERT_NE(nullptr, json);

  dev_diag_state_t decoded;
  memset(&decoded, 0xAA, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, dev_diag_state_decode(json, &decoded));
  cJSON_Delete(json);
}
