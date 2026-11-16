// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

extern "C" {
#include "src/common/eebus_error.h"
}

TEST(ErrorTest, AllKnownCodesReturnNonEmptyString) {
  EXPECT_STREQ("OK", eebus_err_str(EEBUS_OK));
  EXPECT_STREQ("Internal error", eebus_err_str(EEBUS_ERR_INTERNAL));
  EXPECT_STREQ("Invalid argument", eebus_err_str(EEBUS_ERR_INVALID));
  EXPECT_STREQ("Out of memory", eebus_err_str(EEBUS_ERR_NO_MEMORY));
  EXPECT_STREQ("Invalid state", eebus_err_str(EEBUS_ERR_STATE));
  EXPECT_STREQ("Timeout", eebus_err_str(EEBUS_ERR_TIMEOUT));
  EXPECT_STREQ("Not found", eebus_err_str(EEBUS_ERR_NOT_FOUND));
  EXPECT_STREQ("Already exists", eebus_err_str(EEBUS_ERR_EXISTS));
  EXPECT_STREQ("I/O error", eebus_err_str(EEBUS_ERR_IO));
  EXPECT_STREQ("Buffer overflow", eebus_err_str(EEBUS_ERR_OVERFLOW));
  EXPECT_STREQ("Not supported", eebus_err_str(EEBUS_ERR_UNSUPPORTED));
  EXPECT_STREQ("TLS error", eebus_err_str(EEBUS_ERR_TLS));
  EXPECT_STREQ("Certificate error", eebus_err_str(EEBUS_ERR_CERT));
  EXPECT_STREQ("SHIP handshake error", eebus_err_str(EEBUS_ERR_SHIP_HANDSHAKE));
  EXPECT_STREQ("SHIP connection rejected", eebus_err_str(EEBUS_ERR_SHIP_REJECTED));
  EXPECT_STREQ("SHIP version mismatch", eebus_err_str(EEBUS_ERR_SHIP_VERSION));
  EXPECT_STREQ("SPINE invalid datagram", eebus_err_str(EEBUS_ERR_SPINE_INVALID_DATAGRAM));
  EXPECT_STREQ("SPINE write denied", eebus_err_str(EEBUS_ERR_SPINE_WRITE_DENIED));
  EXPECT_STREQ("SPINE subscription/binding limit", eebus_err_str(EEBUS_ERR_SPINE_LIMIT));
  EXPECT_STREQ("Use case not ready", eebus_err_str(EEBUS_ERR_UC_NOT_READY));
  EXPECT_STREQ("Use case failsafe activated", eebus_err_str(EEBUS_ERR_UC_FAILSAFE));
}

TEST(ErrorTest, UnknownCodeReturnsDefault) {
  EXPECT_STREQ("Unknown error", eebus_err_str(-999));
  EXPECT_STREQ("Unknown error", eebus_err_str(100));
}
