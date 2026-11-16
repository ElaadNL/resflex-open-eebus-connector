// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/spine_sub_bind.h"
#include "src/spine/codec/spine_codec_address.h"
}

class SubBindContainerTest : public ::testing::Test {
protected:
  void SetUp() override {
    spine_sub_bind_create(&sb);
    spine_addr_clear(&client);
    spine_addr_clear(&server);
    strcpy(client.device, "dev1");
    client.feature = 1;
    strcpy(server.device, "dev2");
    server.feature = 2;
  }
  void TearDown() override { spine_sub_bind_destroy(sb); }
  spine_sub_bind_t* sb = nullptr;
  spine_addr_t client, server;
};

TEST_F(SubBindContainerTest, EmptyCounts) {
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, AddSubscription) {
  EXPECT_EQ(SPINE_OK, spine_sub_bind_add_subscription(sb, &client, &server));
  EXPECT_EQ(1U, spine_sub_bind_subscription_count(sb));
  EXPECT_TRUE(spine_sub_bind_has_subscription(sb, &client, &server));
}

TEST_F(SubBindContainerTest, AddBinding) {
  EXPECT_EQ(SPINE_OK, spine_sub_bind_add_binding(sb, &client, &server));
  EXPECT_EQ(1U, spine_sub_bind_binding_count(sb));
  EXPECT_TRUE(spine_sub_bind_has_binding(sb, &client, &server));
}

TEST_F(SubBindContainerTest, RemoveSubscription) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  spine_sub_bind_remove_subscription(sb, &client, &server);
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
}

TEST_F(SubBindContainerTest, RemoveBinding) {
  spine_sub_bind_add_binding(sb, &client, &server);
  spine_sub_bind_remove_binding(sb, &client, &server);
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, GetSubscription) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  const spine_subscription_t* sub = spine_sub_bind_get_subscription(sb, 0);
  ASSERT_NE(nullptr, sub);
  EXPECT_TRUE(spine_addr_equal(&client, &sub->client_addr));
  EXPECT_TRUE(spine_addr_equal(&server, &sub->server_addr));
}

TEST_F(SubBindContainerTest, GetBinding) {
  spine_sub_bind_add_binding(sb, &client, &server);
  const spine_binding_t* b = spine_sub_bind_get_binding(sb, 0);
  ASSERT_NE(nullptr, b);
  EXPECT_TRUE(spine_addr_equal(&client, &b->client_addr));
  EXPECT_TRUE(spine_addr_equal(&server, &b->server_addr));
}

static bool match_all(uint32_t id, const spine_addr_t* client,
                       const spine_addr_t* server, void* ud) {
  (void)id; (void)client; (void)server; (void)ud;
  return true;
}

TEST_F(SubBindContainerTest, RemoveDeviceById) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  spine_sub_bind_add_binding(sb, &client, &server);
  /* Remove by device id "dev1" — matches client device of both entries */
  spine_sub_bind_remove_device(sb, "dev1");
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, RemoveEntity) {
  /* Set entity path on client so entity-prefix matching works */
  client.entity[0] = 0;
  client.entity_depth = 1;
  spine_sub_bind_add_subscription(sb, &client, &server);
  spine_sub_bind_add_binding(sb, &client, &server);
  spine_addr_t entity;
  spine_addr_clear(&entity);
  strcpy(entity.device, "dev1");
  entity.entity[0] = 0;
  entity.entity_depth = 1;
  spine_sub_bind_remove_entity(sb, &entity);
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, RemoveFeature) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  spine_sub_bind_add_binding(sb, &client, &server);
  /* Remove by feature address — client feature=1 */
  spine_sub_bind_remove_feature(sb, &client);
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, ForeachSubscriber) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  int count = 0;
  spine_sub_bind_foreach_subscriber(
      sb, &server,
      [](const spine_addr_t* client, void* ud) { (*(int*)ud)++; },
      &count);
  EXPECT_EQ(1, count);
}

TEST_F(SubBindContainerTest, OverflowReturnsError) {
  spine_addr_t cli, srv;
  for (uint16_t i = 0; i < SPINE_SUBSCRIPTION_MAX; i++) {
    spine_addr_clear(&cli);
    spine_addr_clear(&srv);
    cli.feature = i;
    srv.feature = i + 100;
    spine_sub_bind_add_subscription(sb, &cli, &srv);
  }
  /* Max+1 should overflow */
  EXPECT_EQ(SPINE_ERR_OVERFLOW, spine_sub_bind_add_subscription(sb, &client, &server));
}

TEST_F(SubBindContainerTest, RemoveNonExistentReturnsNotFound) {
  EXPECT_EQ(SPINE_ERR_NOT_FOUND, spine_sub_bind_remove_subscription(sb, &client, &server));
  EXPECT_EQ(SPINE_ERR_NOT_FOUND, spine_sub_bind_remove_binding(sb, &client, &server));
}

TEST_F(SubBindContainerTest, GetSubscriptionOutOfBounds) {
  EXPECT_EQ(nullptr, spine_sub_bind_get_subscription(sb, 0));
  EXPECT_EQ(nullptr, spine_sub_bind_get_binding(sb, 0));
}

TEST_F(SubBindContainerTest, DestroyNullIsSafe) {
  spine_sub_bind_destroy(nullptr);
}

TEST_F(SubBindContainerTest, RemoveDeviceSubscriptions) {
  spine_sub_bind_add_subscription(sb, &client, &server);
  spine_addr_t other;
  spine_addr_clear(&other);
  strcpy(other.device, "dev1");
  other.feature = 3;
  spine_sub_bind_add_subscription(sb, &other, &server);
  spine_sub_bind_remove_subscriptions_if(sb, match_all, nullptr);
  EXPECT_EQ(0U, spine_sub_bind_subscription_count(sb));
}

TEST_F(SubBindContainerTest, RemoveDeviceBindings) {
  spine_sub_bind_add_binding(sb, &client, &server);
  spine_sub_bind_remove_bindings_if(sb, match_all, nullptr);
  EXPECT_EQ(0U, spine_sub_bind_binding_count(sb));
}

TEST_F(SubBindContainerTest, HasBindingToServer) {
  spine_sub_bind_add_binding(sb, &client, &server);
  EXPECT_TRUE(spine_sub_bind_has_binding_to_server(sb, &server));
  EXPECT_FALSE(spine_sub_bind_has_binding_to_server(sb, &client));
}

TEST_F(SubBindContainerTest, MultipleSubscriptionsSameAddr) {
  for (uint8_t i = 0; i < 5; i++) {
    EXPECT_EQ(SPINE_OK, spine_sub_bind_add_subscription(sb, &client, &server));
  }
  /* Idempotent: duplicate addresses produce 1 entry */
  EXPECT_EQ(1U, spine_sub_bind_subscription_count(sb));
  EXPECT_TRUE(spine_sub_bind_has_subscription(sb, &client, &server));
}
