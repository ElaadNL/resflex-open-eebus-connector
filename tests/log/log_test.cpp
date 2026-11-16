// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "src/log/eebus_log.h"
}

class LogTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&cfg, 0, sizeof(cfg));
  }
  void TearDown() override {
    eebus_log_deinit();
  }
  eebus_log_config_t cfg;
};

TEST_F(LogTest, InitDeinit) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  EXPECT_EQ(EEBUS_OK, eebus_log_init(&cfg));
}

TEST_F(LogTest, InitTwiceReturnsExists) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  EXPECT_EQ(EEBUS_ERR_EXISTS, eebus_log_init(&cfg));
}

TEST_F(LogTest, DeinitWithoutInit) {
  eebus_log_deinit();
}

TEST_F(LogTest, SetGetLevel) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_set_level(EEBUS_LOG_WARN);
  EXPECT_EQ(EEBUS_LOG_WARN, eebus_log_get_level());
  eebus_log_set_level(EEBUS_LOG_DEBUG);
  EXPECT_EQ(EEBUS_LOG_DEBUG, eebus_log_get_level());
}

TEST_F(LogTest, LogWithCallback) {
  int cb_called = 0;
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    (*(int*)ud)++;
    (void)level; (void)tag; (void)msg;
  };
  cfg.callback_userdata = &cb_called;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "test", __FILE__, __LINE__, "hello %d", 42);
  EXPECT_EQ(1, cb_called);
}

TEST_F(LogTest, LogBelowMinLevel) {
  int cb_called = 0;
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.min_level = EEBUS_LOG_WARN;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    (*(int*)ud)++;
    (void)level; (void)tag; (void)msg;
  };
  cfg.callback_userdata = &cb_called;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "test", __FILE__, __LINE__, "should not appear");
  EXPECT_EQ(0, cb_called);
}

TEST_F(LogTest, LogAtExactLevel) {
  int cb_called = 0;
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.min_level = EEBUS_LOG_WARN;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    (*(int*)ud)++;
    (void)level; (void)tag; (void)msg;
  };
  cfg.callback_userdata = &cb_called;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_WARN, "test", __FILE__, __LINE__, "warning message");
  EXPECT_EQ(1, cb_called);
}

TEST_F(LogTest, MultipleLevels) {
  int counts[5] = {0};
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    int* c = (int*)ud;
    c[level]++;
    (void)tag; (void)msg;
  };
  cfg.callback_userdata = counts;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_DEBUG, "t", __FILE__, __LINE__, "d");
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__, "i");
  eebus_log_write(EEBUS_LOG_WARN, "t", __FILE__, __LINE__, "w");
  eebus_log_write(EEBUS_LOG_ERROR, "t", __FILE__, __LINE__, "e");
  eebus_log_write(EEBUS_LOG_FATAL, "t", __FILE__, __LINE__, "f");
  EXPECT_EQ(1, counts[EEBUS_LOG_DEBUG]);
  EXPECT_EQ(1, counts[EEBUS_LOG_INFO]);
  EXPECT_EQ(1, counts[EEBUS_LOG_WARN]);
  EXPECT_EQ(1, counts[EEBUS_LOG_ERROR]);
  EXPECT_EQ(1, counts[EEBUS_LOG_FATAL]);
}

TEST_F(LogTest, InitWithFile) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.file_path = "/tmp/test_eebus_log.txt";
  EXPECT_EQ(EEBUS_OK, eebus_log_init(&cfg));
}

TEST_F(LogTest, WriteBeforeInit) {
  eebus_log_write(EEBUS_LOG_INFO, "test", __FILE__, __LINE__, "pre-init message");
}

TEST_F(LogTest, DeinitNotInit) {
  eebus_log_deinit();
}

TEST_F(LogTest, LogToFile) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.file_path = "/tmp/test_eebus_log_write.txt";
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__, "test message");
  /* File should exist and contain the message */
  FILE* f = fopen("/tmp/test_eebus_log_write.txt", "r");
  ASSERT_NE(nullptr, f);
  char buf[256] = {};
  ASSERT_NE(nullptr, fgets(buf, sizeof(buf), f));
  EXPECT_STRNE("", buf);
  fclose(f);
  remove("/tmp/test_eebus_log_write.txt");
}

TEST_F(LogTest, LogWithConsole) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = true;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    (*(int*)ud)++;
    (void)level; (void)tag; (void)msg;
  };
  int cb_called = 0;
  cfg.callback_userdata = &cb_called;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "test", __FILE__, __LINE__, "console message");
  EXPECT_EQ(1, cb_called);
}

TEST_F(LogTest, LogWithConsoleAllLevels) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = true;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_DEBUG, "t", __FILE__, __LINE__, "d");
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__, "i");
  eebus_log_write(EEBUS_LOG_WARN, "t", __FILE__, __LINE__, "w");
  eebus_log_write(EEBUS_LOG_ERROR, "t", __FILE__, __LINE__, "e");
  eebus_log_write(EEBUS_LOG_FATAL, "t", __FILE__, __LINE__, "f");
}

TEST_F(LogTest, LogRotate) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.file_path = "/tmp/test_eebus_log_rotate.txt";
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  EXPECT_EQ(EEBUS_OK, eebus_log_rotate());
  remove("/tmp/test_eebus_log_rotate.txt.1");
  remove("/tmp/test_eebus_log_rotate.txt");
  EXPECT_EQ(EEBUS_OK, eebus_log_rotate());
  remove("/tmp/test_eebus_log_rotate.txt.1");
  remove("/tmp/test_eebus_log_rotate.txt");
}

TEST_F(LogTest, LogRotateNoFile) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.file_path = nullptr;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  EXPECT_EQ(EEBUS_OK, eebus_log_rotate());
}

TEST_F(LogTest, LogWriteWithFileRotation) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.file_path = "/tmp/test_eebus_log_rotate2.txt";
  cfg.max_file_bytes = 50;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__, "short");
  /* Write a longer message to trigger size-based rotation */
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__, "this is a longer message that should exceed 50 bytes and trigger rotation");
  remove("/tmp/test_eebus_log_rotate2.txt");
  remove("/tmp/test_eebus_log_rotate2.txt.1");
}

TEST_F(LogTest, InvalidLevelLabel) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.callback = [](eebus_log_level_t level, const char* tag, const char* msg, void* ud) {
    (*(int*)ud)++;
    (void)level; (void)tag; (void)msg;
  };
  int cb_called = 0;
  cfg.callback_userdata = &cb_called;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  /* Pass level 99 to trigger default case in level_label */
  eebus_log_write(static_cast<eebus_log_level_t>(99), "t", __FILE__, __LINE__, "bad level");
  EXPECT_EQ(1, cb_called);
}

TEST_F(LogTest, InitFileOpenFailure) {
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  /* Non-existent directory → fopen fails */
  cfg.file_path = "/nonexistent_dir_xyz/test_eebus_log.txt";
  EXPECT_EQ(EEBUS_ERR_IO, eebus_log_init(&cfg));
}

TEST_F(LogTest, InitNull) {
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_log_init(nullptr));
}

TEST_F(LogTest, SanitizeStripsControlChars) {
  char dst[64];
  const char* out = eebus_log_sanitize(dst, sizeof(dst), "abc\r\ndef\tERROR");
  EXPECT_STREQ("abc__def_ERROR", out);
}

TEST_F(LogTest, SanitizeHandlesNullAndEmpty) {
  char dst[8];
  EXPECT_STREQ("", eebus_log_sanitize(dst, sizeof(dst), nullptr));
  EXPECT_STREQ("", eebus_log_sanitize(dst, sizeof(dst), ""));
}

TEST_F(LogTest, SanitizeTruncatesToBuffer) {
  char dst[4];  /* room for 3 chars + NUL */
  EXPECT_STREQ("abc", eebus_log_sanitize(dst, sizeof(dst), "abcdef"));
}

TEST_F(LogTest, LogWriteScrubsInjectedNewlines) {
  std::string captured;
  cfg = EEBUS_LOG_CONFIG_DEFAULT;
  cfg.console_enabled = false;
  cfg.callback = [](eebus_log_level_t, const char*, const char* msg, void* ud) {
    *static_cast<std::string*>(ud) = msg;
  };
  cfg.callback_userdata = &captured;
  ASSERT_EQ(EEBUS_OK, eebus_log_init(&cfg));
  eebus_log_write(EEBUS_LOG_INFO, "t", __FILE__, __LINE__,
                  "node=%s", "evil\r\nFATAL forged");
  /* The assembled line must not contain a raw CR/LF from the argument. */
  EXPECT_EQ(std::string::npos, captured.find('\n'));
  EXPECT_EQ(std::string::npos, captured.find('\r'));
  EXPECT_NE(std::string::npos, captured.find("evil__FATAL forged"));
}
