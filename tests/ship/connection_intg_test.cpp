// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>

extern "C" {
#include "src/ship/connection/ship_connection.h"
}

static int g_send_count = 0;
static int g_handshake_count = 0;
static int g_spine_data_count = 0;
static int g_closed_count = 0;
static int g_error_count = 0;
static int g_close_request_count = 0;
static ship_trust_result_t g_trust_result = SHIP_TRUST_READY;
static ship_conn_t* g_last_callback_conn = nullptr;
static ship_conn_t* g_last_trust_conn = nullptr;

static void stub_send_frame(void* wsi, uint8_t* buf, size_t len, void* ud) {
  (void)wsi; (void)ud;
  g_send_count++;
  free(buf);
}

static void stub_on_close_wsi(void* wsi, void* ud) {
  (void)wsi;
  (void)ud;
  g_close_request_count++;
}

static void on_hs_complete(ship_conn_t* conn, const char* ski, void* ud) { g_last_callback_conn = conn; (void)ski; (void)ud; g_handshake_count++; }
static void on_spine_data(ship_conn_t* conn, const ship_event_spine_data_t* d, void* ud) { g_last_callback_conn = conn; (void)d; (void)ud; g_spine_data_count++; }
static void on_closed(ship_conn_t* conn, ship_close_reason_t r, void* ud) { g_last_callback_conn = conn; (void)r; (void)ud; g_closed_count++; }
static void on_error(ship_conn_t* conn, eebus_err_t e, void* ud) { g_last_callback_conn = conn; (void)e; (void)ud; g_error_count++; }

static ship_trust_result_t stub_trust_check(ship_conn_t* conn, const char* ski, void* ud) {
  g_last_trust_conn = conn;
  (void)ski; (void)ud; return g_trust_result;
}

class ConnectionIntgTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = SHIP_ROLE_CLIENT;
    strcpy(cfg.tls_peer_ski, "0123456789012345678901234567890123456789");
    strcpy(cfg.local_ski, "9876543210987654321098765432109876543210");
    strcpy(cfg.instance_name, "test-instance");
    cfg.send_frame = stub_send_frame;
    cfg.on_close_wsi = stub_on_close_wsi;
    cfg.callbacks.on_handshake_complete = on_hs_complete;
    cfg.callbacks.on_spine_data = on_spine_data;
    cfg.callbacks.on_closed = on_closed;
    cfg.callbacks.on_error = on_error;
    cfg.on_trust_check = stub_trust_check;
    cfg.userdata = this;
    conn = nullptr;
    g_send_count = 0;
    g_handshake_count = 0;
    g_spine_data_count = 0;
    g_closed_count = 0;
    g_error_count = 0;
    g_close_request_count = 0;
    g_last_callback_conn = nullptr;
    g_last_trust_conn = nullptr;
    g_trust_result = SHIP_TRUST_READY;
  }
  void TearDown() override {
    if (conn) ship_conn_destroy(conn);
  }

  ship_conn_config_t cfg;
  ship_conn_t* conn;
};

TEST_F(ConnectionIntgTest, StartClientSendsFrame) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  ASSERT_NE(nullptr, conn);
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_GT(g_send_count, 0);
  EXPECT_EQ(SHIP_STATE_CMI_CLIENT_WAIT, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, StartServerWaits) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(0, g_send_count);
  EXPECT_EQ(SHIP_STATE_CMI_SERVER_WAIT, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, StartNullWsiReturnsInvalid) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(EEBUS_ERR_INVALID, ship_conn_start(conn, nullptr, nullptr));
}

TEST_F(ConnectionIntgTest, StartTwiceReturnsStateError) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(EEBUS_ERR_STATE, ship_conn_start(conn, nullptr, dummy_wsi));
}

TEST_F(ConnectionIntgTest, StartWithoutSendFrame) {
  cfg.send_frame = nullptr;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(0, g_send_count);
  EXPECT_EQ(SHIP_STATE_CMI_CLIENT_WAIT, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, ReceiveNullData) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  EXPECT_EQ(EEBUS_ERR_INVALID, ship_conn_receive(conn, nullptr, 0));
  EXPECT_EQ(EEBUS_ERR_INVALID, ship_conn_receive(conn, nullptr, 5));
}

TEST_F(ConnectionIntgTest, ReceiveBeforeStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  uint8_t data[] = {0x00};
  EXPECT_EQ(EEBUS_OK, ship_conn_receive(conn, data, 1));
}

TEST_F(ConnectionIntgTest, SetUserdataAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  int ud = 42;
  ship_conn_set_userdata(conn, &ud);
}

TEST_F(ConnectionIntgTest, GetSessionIdAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  const char* sid = ship_conn_get_session_id(conn);
  EXPECT_NE(nullptr, sid);
  EXPECT_GT(strlen(sid), 0U);
}

TEST_F(ConnectionIntgTest, GetPeerSkiAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_STREQ(cfg.tls_peer_ski, ship_conn_get_peer_ski(conn));
}

TEST_F(ConnectionIntgTest, GetTlsPeerSki) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_STREQ(cfg.tls_peer_ski, ship_conn_get_tls_peer_ski(conn));
}

TEST_F(ConnectionIntgTest, GetPeerAccessIdAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_NE(nullptr, ship_conn_get_peer_access_id(conn));
}

TEST_F(ConnectionIntgTest, CloseFromCmi) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(EEBUS_ERR_STATE, ship_conn_close(conn, SHIP_CLOSE_REASON_UNSPECIFIC));
}

TEST_F(ConnectionIntgTest, SendSpineFromCmi) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(EEBUS_ERR_STATE, ship_conn_send_spine(conn, "{}"));
}

TEST_F(ConnectionIntgTest, RejectAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  ship_conn_reject(conn);
}

TEST_F(ConnectionIntgTest, SetReadyAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  ship_conn_set_ready(conn);
}

TEST_F(ConnectionIntgTest, PendingCloseReasonAfterStart) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(SHIP_CLOSE_REASON_NONE, ship_conn_get_pending_close_reason(conn));
}

TEST_F(ConnectionIntgTest, ServerValidCmiExchange) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(SHIP_STATE_CMI_SERVER_WAIT, ship_conn_get_state(conn));

  /* Valid CMI frame: { MessageType=INIT(0x00), CmiHead=0 } */
  uint8_t cmi[] = {0x00, 0x00};
  eebus_err_t err = ship_conn_receive(conn, cmi, sizeof(cmi));
  EXPECT_EQ(EEBUS_OK, err);
  EXPECT_GT(g_send_count, 0);
}

TEST_F(ConnectionIntgTest, ClientValidCmiExchange) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));
  EXPECT_EQ(SHIP_STATE_CMI_CLIENT_WAIT, ship_conn_get_state(conn));

  int before = g_send_count;
  uint8_t cmi[] = {0x00, 0x00};
  eebus_err_t err = ship_conn_receive(conn, cmi, sizeof(cmi));
  EXPECT_EQ(EEBUS_OK, err);
  /* After valid CMI, client enters Hello phase and sends Hello */
  EXPECT_GT(g_send_count, before);
}

TEST_F(ConnectionIntgTest, ClientInvalidCmiTriggersError) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));

  uint8_t bad_cmi[] = {0xFF, 0xFF};
  eebus_err_t err = ship_conn_receive(conn, bad_cmi, sizeof(bad_cmi));
  EXPECT_EQ(EEBUS_ERR_SHIP_HANDSHAKE, err);
  EXPECT_GT(g_error_count, 0);
  EXPECT_GT(g_close_request_count, 0);
}

TEST_F(ConnectionIntgTest, ServerInvalidCmiStillSendsResponse) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* dummy_wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, dummy_wsi));

  int before = g_send_count;
  uint8_t bad_cmi[] = {0xFF, 0xFF};
  eebus_err_t err = ship_conn_receive(conn, bad_cmi, sizeof(bad_cmi));
  EXPECT_EQ(EEBUS_ERR_SHIP_HANDSHAKE, err);
  /* Server always sends CMI{0} back even on invalid frame */
  EXPECT_GT(g_send_count, before);
  EXPECT_GT(g_error_count, 0);
}

/* ── Multi-step state machine flow tests ───────────────────────────────── */

static std::vector<uint8_t> make_ctrl_frame(const char* json) {
  size_t jlen = strlen(json);
  std::vector<uint8_t> f(1 + jlen);
  f[0] = 0x01; /* SHIP_MSG_TYPE_CONTROL */
  memcpy(&f[1], json, jlen);
  return f;
}

TEST_F(ConnectionIntgTest, ClientFullCmiAndHello) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  ASSERT_EQ(SHIP_STATE_CMI_CLIENT_WAIT, ship_conn_get_state(conn));

  /* CMI exchange: receive server CMI response */
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  /* Receive peer Hello{ready} → both ready → Protocol Handshake */
  int before = g_send_count;
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));
  EXPECT_GT(g_send_count, before); /* client sent announceMax */
}

TEST_F(ConnectionIntgTest, ServerFullCmiAndHello) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  ASSERT_EQ(SHIP_STATE_CMI_SERVER_WAIT, ship_conn_get_state(conn));

  /* CMI exchange: receive client CMI init */
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  /* Receive peer Hello{ready} → both ready → Protocol Handshake */
  int before = g_send_count;
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_EQ(SHIP_STATE_PROT_HS_LISTEN, ship_conn_get_state(conn));
  /* Server does NOT send anything in enter_protocol_handshake (listens first) */
  EXPECT_EQ(g_send_count, before);
}

TEST_F(ConnectionIntgTest, TrustPendingFlow) {
  g_trust_result = SHIP_TRUST_PENDING;
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  EXPECT_EQ(SHIP_STATE_HELLO_PENDING, ship_conn_get_state(conn));
  EXPECT_EQ(conn, g_last_trust_conn);

  /* Receive peer Hello{ready} with waiting → PENDING_LISTEN rule 2b ok */
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"},{"waiting":60000}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_EQ(SHIP_STATE_HELLO_PENDING, ship_conn_get_state(conn));

  /* Now set_ready → hello_peer_ready already true → advances to Protocol HS */
  int before = g_send_count;
  ship_conn_set_ready(conn);
  EXPECT_EQ(SHIP_STATE_PROT_HS_LISTEN, ship_conn_get_state(conn));
  EXPECT_GT(g_send_count, before);
}

TEST_F(ConnectionIntgTest, TrustRejectFlow) {
  g_trust_result = SHIP_TRUST_REJECT;
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  eebus_err_t err = ship_conn_receive(conn, cmi, sizeof(cmi));
  /* Trust reject → hello_abort → on_error */
  EXPECT_EQ(EEBUS_ERR_SHIP_REJECTED, err);
  EXPECT_GT(g_error_count, 0);
  EXPECT_GT(g_send_count, 0); /* sent CMI response + Hello{aborted} */
}

TEST_F(ConnectionIntgTest, NonHelloDuringHelloPhaseAborts) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  /* Send a non-Hello control message during Hello phase → abort */
  auto nf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  eebus_err_t err = ship_conn_receive(conn, nf.data(), nf.size());
  EXPECT_EQ(EEBUS_OK, err);
  /* State should transition to error/closed */
  EXPECT_NE(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, ClientFullProtocolHandshake) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));

  /* CMI */
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  /* Hello{ready} from peer → client sends announceMax → PROT_HS_ANNOUNCE */
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));

  /* Protocol handshake select from server → echoes → enter_pin */
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  /* enter_pin sends pinState{none} → advances to PIN_CHECK */
  EXPECT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, ClientFullFlowToDataExchange) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  ASSERT_EQ(SHIP_STATE_CMI_CLIENT_WAIT, ship_conn_get_state(conn));

  /* CMI exchange */
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  /* Receive Hello{ready} → both ready → Protocol HS (client sends announceMax) */
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));

  /* Receive protocol handshake select → echoes → PIN (sends pinState → CHECK) */
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  ASSERT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));

  /* Receive peer pinState{none} → enter_data_exchange (sends accessMethodsRequest) */
  EXPECT_EQ(0, g_handshake_count);
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
  /* on_handshake_complete NOT yet fired (need accessMethods response) */
  EXPECT_EQ(0, g_handshake_count);
  /* Frames: CMI init + Hello ready + announceMax + echo + pinState + accessMethodsRequest = 6 */
  EXPECT_EQ(6, g_send_count);

  /* Receive peer accessMethods → fires on_handshake_complete */
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
  EXPECT_EQ(1, g_handshake_count);
  EXPECT_EQ(conn, g_last_callback_conn);
}

TEST_F(ConnectionIntgTest, SendSpineInDataExchange) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  /* Now send_spine should succeed */
  EXPECT_EQ(EEBUS_OK, ship_conn_send_spine(conn, R"({"foo":"bar"})"));
}

TEST_F(ConnectionIntgTest, ReceiveSpineDataInDataExchange) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  /* Receive SPINE data frame (type=0x02) */
  EXPECT_EQ(0, g_spine_data_count);
  std::vector<uint8_t> df = {0x02}; /* SHIP_MSG_TYPE_DATA */
  const char* data_json = R"({"data":[{"header":[{"protocolId":"ee1.0"}]},{"payload":{"datagram":{"header":{"specification":"SPINE"}}}}]})";
  size_t jlen = strlen(data_json);
  df.resize(1 + jlen);
  memcpy(&df[1], data_json, jlen);
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, df.data(), df.size()));
  EXPECT_EQ(1, g_spine_data_count);
}

TEST_F(ConnectionIntgTest, CloseFromDataExchange) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  /* Close should succeed from DATA_EXCHANGE */
  EXPECT_EQ(EEBUS_OK, ship_conn_close(conn, SHIP_CLOSE_REASON_UNSPECIFIC));
}

TEST_F(ConnectionIntgTest, AccessMethodsWithDns) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));

  /* Send accessMethods with dns and dnsSd_mDns */
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"},{"dnsSd_mDns":[]},{"dns":[{"uri":"wss://1.2.3.4:12345"}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
  EXPECT_NE(nullptr, ship_conn_get_peer_access_id(conn));
  EXPECT_STREQ("peer-id", ship_conn_get_peer_access_id(conn));
}

/* ── Timer timeout tests ─────────────────────────────────────────────────── */

extern "C" void stub_timer_fire_nth(int n);
extern "C" void stub_timer_fire_last(void);

TEST_F(ConnectionIntgTest, CmiTimeoutFiresOnError) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  int before_err = g_error_count;
  stub_timer_fire_nth(0);
  EXPECT_GT(g_error_count, before_err);
}

TEST_F(ConnectionIntgTest, HelloTimeoutAfterCmi) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));
  int before_err = g_error_count;
  stub_timer_fire_nth(1);
  EXPECT_GT(g_error_count, before_err);
}

/* ── Hello edge cases ──────────────────────────────────────────────────── */

TEST_F(ConnectionIntgTest, PeerAbortedHelloCloses) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_READY, ship_conn_get_state(conn));

  int before_err = g_error_count;
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"aborted"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_GT(g_error_count, before_err);
}

TEST_F(ConnectionIntgTest, ReadyWithoutWaitingWhilePendingAborts) {
  g_trust_result = SHIP_TRUST_PENDING;
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_PENDING, ship_conn_get_state(conn));

  int before_err = g_error_count;
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_GT(g_error_count, before_err);
}

TEST_F(ConnectionIntgTest, ProlongationRequestReceived) {
  g_trust_result = SHIP_TRUST_PENDING;
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  ASSERT_EQ(SHIP_STATE_HELLO_PENDING, ship_conn_get_state(conn));

  int before_send = g_send_count;
  auto hf = make_ctrl_frame(
      R"({"connectionHello":[{"phase":"pending"},{"prolongationRequest":true}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  EXPECT_GT(g_send_count, before_send);
}

/* ── Protocol handshake error paths ──────────────────────────────────────── */

TEST_F(ConnectionIntgTest, ServerRejectsIncompatibleVersion) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_LISTEN, ship_conn_get_state(conn));

  int before_send = g_send_count;
  int before_err = g_error_count;
  auto af = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"announceMax"},{"version":[{"major":2},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_GT(g_send_count, before_send);
  EXPECT_GT(g_error_count, before_err);
}

TEST_F(ConnectionIntgTest, ServerRejectsNoJsonUtf8) {
  cfg.role = SHIP_ROLE_SERVER;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_LISTEN, ship_conn_get_state(conn));

  int before_send = g_send_count;
  auto af = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"announceMax"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF16"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_GT(g_send_count, before_send);
  EXPECT_GT(g_error_count, 0);
}

TEST_F(ConnectionIntgTest, ClientRejectsIncompatibleVersion) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));

  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":2},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  EXPECT_GT(g_error_count, 0);
}

TEST_F(ConnectionIntgTest, ClientRejectsUnsupportedFormat) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));

  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF16"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  EXPECT_GT(g_error_count, 0);
}

/* ── PIN phase variants ────────────────────────────────────────────────── */

TEST_F(ConnectionIntgTest, PinStateOptionalContinues) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  ASSERT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));

  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"optional"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
}

TEST_F(ConnectionIntgTest, PinStateRequiredCloses) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  ASSERT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));

  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"required"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  EXPECT_GT(g_error_count, 0);
}

TEST_F(ConnectionIntgTest, PinStatePinOkContinues) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  ASSERT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));

  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"pinOk"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
}

/* ── Access methods fallback ────────────────────────────────────────────── */

TEST_F(ConnectionIntgTest, AccessMethodsFallbackUsesSki) {
  cfg.instance_name[0] = '\0';
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  ASSERT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  auto af = make_ctrl_frame(R"({"accessMethods":[]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  EXPECT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));
  EXPECT_NE(nullptr, ship_conn_get_peer_access_id(conn));
}

/* ── Connection close from peer + confirm ──────────────────────────────── */

TEST_F(ConnectionIntgTest, PeerCloseAnnounceClosesGracefully) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  ASSERT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  int before_send = g_send_count;
  int before_closed = g_closed_count;
  auto cf = make_ctrl_frame(R"({"connectionClose":[{"phase":"announce"},{"reason":1}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cf.data(), cf.size()));
  EXPECT_GT(g_send_count, before_send);
  EXPECT_GT(g_closed_count, before_closed);
}

TEST_F(ConnectionIntgTest, CloseConfirmReceived) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  auto pf = make_ctrl_frame(R"({"connectionPinState":[{"pinState":"none"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, pf.data(), pf.size()));
  auto af = make_ctrl_frame(R"({"accessMethods":[{"id":"peer-id"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, af.data(), af.size()));
  ASSERT_EQ(SHIP_STATE_DATA_EXCHANGE, ship_conn_get_state(conn));

  ASSERT_EQ(EEBUS_OK, ship_conn_close(conn, SHIP_CLOSE_REASON_UNSPECIFIC));
  ASSERT_EQ(SHIP_STATE_CLOSE_WAIT, ship_conn_get_state(conn));

  int before_closed = g_closed_count;
  auto cc = make_ctrl_frame(R"({"connectionClose":[{"phase":"confirm"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cc.data(), cc.size()));
  EXPECT_GT(g_closed_count, before_closed);
}

/* ── Receive error paths ───────────────────────────────────────────────── */

TEST_F(ConnectionIntgTest, EmptyPayloadFrame) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));

  uint8_t empty[] = {0x01};
  /* Invalid frame in CMI_WAIT → handle_cmi_frame returns EEBUS_ERR_SHIP_HANDSHAKE */
  eebus_err_t err = ship_conn_receive(conn, empty, sizeof(empty));
  EXPECT_NE(EEBUS_OK, err);
}

TEST_F(ConnectionIntgTest, InvalidJsonPayload) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));

  /* Complete CMI exchange first */
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));

  /* Bad JSON in a control frame → parse fails, returns EEBUS_ERR_SHIP_HANDSHAKE */
  auto bf = make_ctrl_frame(R"({bad json!!!})");
  eebus_err_t err = ship_conn_receive(conn, bf.data(), bf.size());
  EXPECT_NE(EEBUS_OK, err);
}

/* ── send_raw NULL send_frame path ──────────────────────────────────────── */

TEST_F(ConnectionIntgTest, SendRawNullSendFrameWithHello) {
  cfg.send_frame = nullptr;
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));

  /* CMI exchange: receiving {0x00,0x00} succeeds, but enter_hello then
   * calls send_raw which returns EEBUS_ERR_INTERNAL (send_frame=NULL). */
  uint8_t cmi[] = {0x00, 0x00};
  eebus_err_t err = ship_conn_receive(conn, cmi, sizeof(cmi));
  EXPECT_EQ(EEBUS_ERR_INTERNAL, err);
  /* Connection ends up in HELLO_INIT (enter_hello set state, then send_raw
   * failed before transitioning to PENDING).  Verify no crash. */
  EXPECT_EQ(SHIP_STATE_HELLO_INIT, ship_conn_get_state(conn));
}

/* ── Protocol Handshake timeout ─────────────────────────────────────────── */

TEST_F(ConnectionIntgTest, ProtHsTimeoutAfterHandshake) {
  ASSERT_EQ(EEBUS_OK, ship_conn_create(&conn, &cfg));
  void* wsi = (void*)(intptr_t)0x1234;
  ASSERT_EQ(EEBUS_OK, ship_conn_start(conn, nullptr, wsi));
  uint8_t cmi[] = {0x00, 0x00};
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, cmi, sizeof(cmi)));
  auto hf = make_ctrl_frame(R"({"connectionHello":[{"phase":"ready"}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, hf.data(), hf.size()));
  ASSERT_EQ(SHIP_STATE_PROT_HS_ANNOUNCE, ship_conn_get_state(conn));

  auto sf = make_ctrl_frame(
      R"({"messageProtocolHandshake":[{"handshakeType":"select"},{"version":[{"major":1},{"minor":0}]},{"formats":[{"format":["JSON-UTF8"]}]}]})");
  ASSERT_EQ(EEBUS_OK, ship_conn_receive(conn, sf.data(), sf.size()));
  ASSERT_EQ(SHIP_STATE_PIN_CHECK, ship_conn_get_state(conn));

  int before_err = g_error_count;
  stub_timer_fire_last();
  EXPECT_GT(g_error_count, before_err);
}
