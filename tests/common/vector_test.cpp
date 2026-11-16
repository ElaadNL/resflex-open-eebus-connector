// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

extern "C" {
#include "src/common/eebus_vector.h"
}

class VectorTest : public ::testing::Test {
protected:
  void SetUp() override { memset(&v, 0, sizeof(v)); }
  void TearDown() override { eebus_vector_free(&v); }
  eebus_vector_t v;
};

TEST_F(VectorTest, InitIsEmpty) {
  EXPECT_EQ(0U, v.size);
  EXPECT_EQ(0U, v.capacity);
  EXPECT_EQ(nullptr, v.data);
}

TEST_F(VectorTest, PushIncreasesSize) {
  int a = 1;
  ASSERT_EQ(EEBUS_OK, eebus_vector_push(&v, &a));
  EXPECT_EQ(1U, v.size);
  EXPECT_GE(v.capacity, 1U);
}

TEST_F(VectorTest, PushAndPop) {
  int a = 42;
  ASSERT_EQ(EEBUS_OK, eebus_vector_push(&v, &a));
  void* out = nullptr;
  ASSERT_EQ(EEBUS_OK, eebus_vector_pop(&v, &out));
  EXPECT_EQ(&a, out);
  EXPECT_EQ(0U, v.size);
}

TEST_F(VectorTest, PopEmptyReturnsNotFound) {
  void* out = (void*)0xDEAD;
  EXPECT_EQ(EEBUS_ERR_NOT_FOUND, eebus_vector_pop(&v, &out));
  EXPECT_EQ(nullptr, out);
}

TEST_F(VectorTest, PopEmptyWithNullOut) {
  EXPECT_EQ(EEBUS_ERR_NOT_FOUND, eebus_vector_pop(&v, nullptr));
}

TEST_F(VectorTest, RemoveByIndex) {
  int a = 1, b = 2, c = 3;
  eebus_vector_push(&v, &a);
  eebus_vector_push(&v, &b);
  eebus_vector_push(&v, &c);
  ASSERT_EQ(EEBUS_OK, eebus_vector_remove(&v, 1));
  EXPECT_EQ(2U, v.size);
  EXPECT_EQ(&a, v.data[0]);
  EXPECT_EQ(&c, v.data[1]);
}

TEST_F(VectorTest, RemoveOutOfRange) {
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_vector_remove(&v, 0));
}

TEST_F(VectorTest, FindExisting) {
  int a = 1, b = 2;
  eebus_vector_push(&v, &a);
  eebus_vector_push(&v, &b);
  size_t idx = 999;
  EXPECT_EQ(EEBUS_OK, eebus_vector_find(&v, &b, &idx));
  EXPECT_EQ(1U, idx);
}

TEST_F(VectorTest, FindMissing) {
  int a = 1;
  eebus_vector_push(&v, &a);
  int missing = 99;
  EXPECT_EQ(EEBUS_ERR_NOT_FOUND, eebus_vector_find(&v, &missing, nullptr));
}

TEST_F(VectorTest, FindInEmpty) {
  int x = 0;
  EXPECT_EQ(EEBUS_ERR_NOT_FOUND, eebus_vector_find(&v, &x, nullptr));
}

TEST_F(VectorTest, ClearResetsSizeOnly) {
  int a = 1;
  eebus_vector_push(&v, &a);
  eebus_vector_clear(&v);
  EXPECT_EQ(0U, v.size);
  EXPECT_NE(nullptr, v.data);
}

TEST_F(VectorTest, FreeResetsAll) {
  int a = 1;
  eebus_vector_push(&v, &a);
  eebus_vector_free(&v);
  EXPECT_EQ(0U, v.size);
  EXPECT_EQ(0U, v.capacity);
  EXPECT_EQ(nullptr, v.data);
}

TEST_F(VectorTest, NullArgReturnsInvalid) {
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_vector_push(nullptr, nullptr));
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_vector_pop(nullptr, nullptr));
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_vector_remove(nullptr, 0));
  EXPECT_EQ(EEBUS_ERR_INVALID, eebus_vector_find(nullptr, nullptr, nullptr));
}

TEST_F(VectorTest, MultiplePushGrowsCapacity) {
  void* ptrs[32];
  for (int i = 0; i < 32; i++) {
    ptrs[i] = &ptrs[i];
    ASSERT_EQ(EEBUS_OK, eebus_vector_push(&v, ptrs[i]));
  }
  EXPECT_EQ(32U, v.size);
  EXPECT_GE(v.capacity, 32U);
  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(ptrs[i], v.data[i]);
  }
}

TEST_F(VectorTest, FreeNullIsSafe) {
  eebus_vector_free(nullptr);
}

TEST_F(VectorTest, ClearNullIsSafe) {
  eebus_vector_clear(nullptr);
}
