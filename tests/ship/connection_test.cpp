// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "src/ship/connection/ship_connection.h"
}

static void stub_send_frame(void* wsi, uint8_t* buf, size_t len, void* ud) {
  free(buf); (void)wsi; (void)len; (void)ud;
}

class ConnectionTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&cfg, 0, sizeof(cfg));
    cfg.send_frame = stub_send_frame;
    conn = nullptr;
  }
  void TearDown() override {
    if (conn) ship_conn_destroy(conn);
  }
  ship_conn_t* conn;
  ship_conn_config_t cfg;
  ship_conn_callbacks_t cbs;
};

static int g_handshake_complete = 0;
static void on_handshake_complete(ship_conn_t* conn,
                                   const char* peer_ski,
                                   void* ud) {
  g_handshake_complete = 1;
  (void)conn; (void)peer_ski; (void)ud;
}

TEST_F(ConnectionTest, CreateDestroy) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  EXPECT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_NE(nullptr, conn);
}

TEST_F(ConnectionTest, RetainedReferenceDefersDestroy) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  ASSERT_TRUE(ship_conn_retain(conn));

  ship_conn_destroy(conn);
  EXPECT_FALSE(ship_conn_retain(conn));
  EXPECT_EQ(SHIP_STATE_CMI_INIT, ship_conn_get_state(conn));

  ship_conn_release(conn);
  conn = nullptr;
}

TEST_F(ConnectionTest, CreateNull) {
  EXPECT_EQ(EEBUS_ERR_INVALID, ship_conn_create(nullptr, nullptr));
}

TEST_F(ConnectionTest, GetStateAfterCreate) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(SHIP_STATE_CMI_INIT, ship_conn_get_state(conn));
}

TEST_F(ConnectionTest, SetGetUserdata) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  int ud = 42;
  ship_conn_set_userdata(conn, &ud);
}

TEST_F(ConnectionTest, CreateWithHandshakeCompleteCallback) {
  g_handshake_complete = 0;
  memset(&cbs, 0, sizeof(cbs));
  cbs.on_handshake_complete = on_handshake_complete;
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_NE(nullptr, conn);
}

TEST_F(ConnectionTest, GetSessionId) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  const char* sid = ship_conn_get_session_id(conn);
  EXPECT_NE(nullptr, sid);
  EXPECT_GT(strlen(sid), 0U);
}

TEST_F(ConnectionTest, Close) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(EEBUS_ERR_STATE, ship_conn_close(conn, SHIP_CLOSE_REASON_UNSPECIFIC));
}

TEST_F(ConnectionTest, SendSpine) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(EEBUS_ERR_STATE, ship_conn_send_spine(conn, "{}"));
}

TEST_F(ConnectionTest, Reject) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  ship_conn_reject(conn);
}

TEST_F(ConnectionTest, GetSetReady) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  ship_conn_set_ready(conn);
}

TEST_F(ConnectionTest, GetPeerSkiEmpty) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_STREQ("", ship_conn_get_peer_ski(conn));
}

TEST_F(ConnectionTest, GetTlsPeerSkiEmpty) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_STREQ("", ship_conn_get_tls_peer_ski(conn));
}

TEST_F(ConnectionTest, GetPeerAccessId) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_STREQ("", ship_conn_get_peer_access_id(conn));
}

TEST_F(ConnectionTest, PendingCloseReason) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(SHIP_CLOSE_REASON_NONE, ship_conn_get_pending_close_reason(conn));
}

TEST_F(ConnectionTest, Receive) {
  memset(&cbs, 0, sizeof(cbs));
  cfg.callbacks = cbs;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  ship_conn_receive(conn, (const uint8_t*)"test", 4);
}
