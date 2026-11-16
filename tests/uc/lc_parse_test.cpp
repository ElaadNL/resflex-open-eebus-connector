// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

extern "C" {
#include "src/uc/uc_lc.h"
}

TEST(LcParseTest, DurationZero) {
  EXPECT_EQ(0U, uc_lc_parse_duration_s("PT0S"));
}

TEST(LcParseTest, DurationSeconds) {
  EXPECT_EQ(3600U, uc_lc_parse_duration_s("PT3600S"));
  EXPECT_EQ(7200U, uc_lc_parse_duration_s("PT7200S"));
}

TEST(LcParseTest, DurationMinutes) {
  EXPECT_EQ(60U, uc_lc_parse_duration_s("PT1M"));
  EXPECT_EQ(3600U, uc_lc_parse_duration_s("PT60M"));
  EXPECT_EQ(7200U, uc_lc_parse_duration_s("PT120M"));
}

TEST(LcParseTest, DurationHours) {
  EXPECT_EQ(3600U, uc_lc_parse_duration_s("PT1H"));
  EXPECT_EQ(7200U, uc_lc_parse_duration_s("PT2H"));
}

TEST(LcParseTest, DurationCombined) {
  EXPECT_EQ(3660U, uc_lc_parse_duration_s("PT1H1M"));
  EXPECT_EQ(3661U, uc_lc_parse_duration_s("PT1H1M1S"));
}

TEST(LcParseTest, DurationDays) {
  EXPECT_EQ(86400U, uc_lc_parse_duration_s("P1D"));
  EXPECT_EQ(172800U, uc_lc_parse_duration_s("P2D"));
}

TEST(LcParseTest, DurationDaysAndTime) {
  EXPECT_EQ(90060U, uc_lc_parse_duration_s("P1DT1H1M"));
  EXPECT_EQ(86401U, uc_lc_parse_duration_s("P1DT1S"));
}

TEST(LcParseTest, DurationNullReturnsZero) {
  EXPECT_EQ(0U, uc_lc_parse_duration_s(nullptr));
}

TEST(LcParseTest, DurationInvalidReturnsZero) {
  EXPECT_EQ(0U, uc_lc_parse_duration_s(""));
  EXPECT_EQ(0U, uc_lc_parse_duration_s("PT"));
  EXPECT_EQ(0U, uc_lc_parse_duration_s("PXY"));
  EXPECT_EQ(0U, uc_lc_parse_duration_s("notADuration"));
}

TEST(LcParseTest, DurationNoPrefixReturnsZero) {
  EXPECT_EQ(0U, uc_lc_parse_duration_s("T1H"));
}

TEST(LcParseTest, TimePeriodNoEndTime) {
  spine_time_period_t tp = {};
  EXPECT_EQ(0U, uc_lc_parse_time_period_ms(&tp));
}

TEST(LcParseTest, TimePeriodDuration) {
  spine_time_period_t tp = {};
  tp.end_time_present = true;
  strcpy(tp.end_time, "PT3600S");
  EXPECT_EQ(3600000U, uc_lc_parse_time_period_ms(&tp));
}

TEST(LcParseTest, TimePeriodShortDuration) {
  spine_time_period_t tp = {};
  tp.end_time_present = true;
  strcpy(tp.end_time, "PT30S");
  EXPECT_EQ(30000U, uc_lc_parse_time_period_ms(&tp));
}

TEST(LcParseTest, TimePeriodInvalidFormat) {
  spine_time_period_t tp = {};
  tp.end_time_present = true;
  strcpy(tp.end_time, "notADurationOrTimestamp");
  EXPECT_EQ(0U, uc_lc_parse_time_period_ms(&tp));
}

TEST(LcParseTest, TimePeriodPastTimestamp) {
  spine_time_period_t tp = {};
  tp.end_time_present = true;
  /* A timestamp in the distant past (pre-2000 epoch) */
  strcpy(tp.end_time, "1970-01-01T00:00:00Z");
  EXPECT_EQ(0U, uc_lc_parse_time_period_ms(&tp));
}
