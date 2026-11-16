// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_rfe.h"
}

class RFETest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&filter, 0, sizeof(filter));
  }
  spine_rfe_filter_t filter;
};

TEST_F(RFETest, FilterReadParsesSelectors) {
  const char* json_str =
      R"({"loadControlLimitListData":[{"loadControlLimitData":[[{"limitId":1,"val":"a"},{"limitId":2,"val":"b"}]]}]})";
  cJSON* root = cJSON_Parse(json_str);
  ASSERT_NE(nullptr, root);

  cJSON* selector = cJSON_Parse(R"({"limitId":[1]})");
  ASSERT_NE(nullptr, selector);

  cJSON* result = spine_rfe_filter_read(root, "limitId", selector, NULL);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(root);
  cJSON_Delete(selector);
  cJSON_Delete(result);
}

TEST_F(RFETest, FilterReadNoRfe) {
  const char* json_str =
      R"({"otherListData":[{"otherData":[[{"id":1}]]}]})";
  cJSON* root = cJSON_Parse(json_str);
  ASSERT_NE(nullptr, root);

  cJSON* result = spine_rfe_filter_read(root, "id", NULL, NULL);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(root);
  cJSON_Delete(result);
}

TEST_F(RFETest, ApplyFullReplace) {
  const char* local_str = R"({"items":[{"id":1,"val":"a"},{"id":2,"val":"b"}]})";
  const char* incoming_str = R"({"items":[{"id":3,"val":"c"}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  cJSON* result = spine_rfe_apply(local, incoming, "id", NULL, NULL);
  ASSERT_NE(nullptr, result);
  cJSON* items = cJSON_GetObjectItem(result, "items");
  ASSERT_NE(nullptr, items);
  ASSERT_TRUE(cJSON_IsArray(items));
  EXPECT_EQ(1, cJSON_GetArraySize(items));
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
}

TEST_F(RFETest, BuildFilterWithSelectors) {
  const char* sel_keys[] = {"limitId"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};

  spine_rfe_build_filter(&filter, "loadControlLimitListData", sel_keys, sel_vals, 1, NULL);
  EXPECT_TRUE(filter.present);
  EXPECT_NE(nullptr, filter.selector);
  EXPECT_EQ(nullptr, filter.elements);

  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, BuildFilterNulls) {
  spine_rfe_build_filter(&filter, "loadControlLimitListData", NULL, NULL, 0, NULL);
  EXPECT_TRUE(filter.present);
  EXPECT_EQ(nullptr, filter.selector);
  EXPECT_EQ(nullptr, filter.elements);

  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyWithTouched) {
  const char* local_str = R"({"items":[{"id":1,"val":"a"},{"id":2,"val":"b"}]})";
  const char* incoming_str = R"({"items":[{"id":1,"val":"x"}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  spine_rfe_touched_t touched;
  memset(&touched, 0, sizeof(touched));
  cJSON* result = spine_rfe_apply_with_touched(local, incoming, "id", NULL, NULL, &touched);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
}

TEST_F(RFETest, FreeFilter) {
  spine_rfe_build_filter(&filter, "loadControlLimitListData", NULL, NULL, 0, NULL);
  spine_rfe_free_filter(&filter);
  SUCCEED();
}

TEST_F(RFETest, ApplyPartialMergeByIdMatch) {
  /* Two-level SPINE list structure for get_item_list to work */
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}],[{"id":2},{"val":"b"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);
  cJSON* result = spine_rfe_apply(local, incoming, "id", &filter, NULL);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyPartialMergeByIdAppend) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":2},{"val":"b"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(2)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);
  cJSON* result = spine_rfe_apply(local, incoming, "id", &filter, NULL);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyPartialMergeNoId) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  spine_rfe_build_filter(&filter, "lcListData", NULL, NULL, 0, NULL);
  cJSON* result = spine_rfe_apply(local, incoming, "id", &filter, NULL);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyDeleteBySelector) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}],[{"id":2},{"val":"b"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  ASSERT_NE(nullptr, local);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);
  cJSON* result = spine_rfe_apply(local, NULL, "id", NULL, &filter);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyDeleteFieldsBySelector) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"},{"extra":"x"}],[{"id":2},{"val":"b"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  ASSERT_NE(nullptr, local);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  const char* elem_paths[] = {"extra", NULL};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, elem_paths);
  cJSON* result = spine_rfe_apply(local, NULL, "id", NULL, &filter);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyPartialAndDelete) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}],[{"id":2},{"val":"b"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t del_vals[] = {SPINE_RFE_INT(2)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, del_vals, 1, NULL);
  spine_rfe_filter_t part_filter;
  memset(&part_filter, 0, sizeof(part_filter));
  spine_rfe_val_t part_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&part_filter, "lcListData", sel_keys, part_vals, 1, NULL);

  cJSON* result = spine_rfe_apply(local, incoming, "id", &part_filter, &filter);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
  spine_rfe_free_filter(&part_filter);
}

TEST_F(RFETest, ApplyWithTouchedReportsIds) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}],[{"id":2},{"val":"b"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);

  spine_rfe_touched_t touched;
  memset(&touched, 0, sizeof(touched));
  cJSON* result = spine_rfe_apply_with_touched(local, incoming, "id", &filter, NULL, &touched);
  ASSERT_NE(nullptr, result);
  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyNullExistingReturnsNull) {
  cJSON* incoming = cJSON_Parse(R"({"items":[{"id":1}]})");
  ASSERT_NE(nullptr, incoming);
  EXPECT_EQ(nullptr, spine_rfe_apply(nullptr, incoming, "id", NULL, NULL));
  cJSON_Delete(incoming);
}

TEST_F(RFETest, BuildFilterInt64DoubleBool) {
  const char* sel_keys[] = {"val"};

  spine_rfe_val_t int64_val = SPINE_RFE_INT64(9223372036854775807LL);
  spine_rfe_build_filter(&filter, "items", sel_keys, &int64_val, 1, NULL);
  EXPECT_TRUE(filter.present);
  spine_rfe_free_filter(&filter);

  spine_rfe_val_t double_val = SPINE_RFE_DOUBLE(3.14);
  spine_rfe_build_filter(&filter, "items", sel_keys, &double_val, 1, NULL);
  EXPECT_TRUE(filter.present);
  spine_rfe_free_filter(&filter);

  spine_rfe_val_t bool_val = SPINE_RFE_BOOL(true);
  spine_rfe_build_filter(&filter, "items", sel_keys, &bool_val, 1, NULL);
  EXPECT_TRUE(filter.present);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, FilterReadSelectorOnly) {
  const char* json_str =
      R"({"itemsListData":[{"itemsData":[[{"id":1,"val":"a"},{"id":2,"val":"b"}]]}]})";
  cJSON* root = cJSON_Parse(json_str);
  ASSERT_NE(nullptr, root);

  cJSON* selector = cJSON_Parse(R"({"id":[1]})");
  ASSERT_NE(nullptr, selector);

  cJSON* result = spine_rfe_filter_read(root, "id", selector, NULL);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(root);
  cJSON_Delete(selector);
  cJSON_Delete(result);
}

TEST_F(RFETest, FilterReadElementsOnly) {
  const char* json_str =
      R"({"itemsListData":[{"itemsData":[[{"id":1,"val":"a","extra":"x"}]]}]})";
  cJSON* root = cJSON_Parse(json_str);
  ASSERT_NE(nullptr, root);

  cJSON* elements = cJSON_Parse(R"({"val":{}})");
  ASSERT_NE(nullptr, elements);

  cJSON* result = spine_rfe_filter_read(root, "id", NULL, elements);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(root);
  cJSON_Delete(elements);
  cJSON_Delete(result);
}

TEST_F(RFETest, ApplyPartialMergeByStringSelector) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1,"type":"foo"},{"val":"a"}],[{"id":2,"type":"bar"},{"val":"b"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"type"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_STR("foo")};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);

  cJSON* result = spine_rfe_apply(local, incoming, "id", &filter, NULL);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyDeleteByBoolSelector) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1,"active":true},{"val":"a"}],[{"id":2,"active":false},{"val":"b"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  ASSERT_NE(nullptr, local);

  const char* sel_keys[] = {"active"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_BOOL(true)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);

  cJSON* result = spine_rfe_apply(local, NULL, "id", NULL, &filter);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(local);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyDeleteFieldsAllItems) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"},{"extra":"x"}],[{"id":2},{"val":"b"},{"extra":"y"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  ASSERT_NE(nullptr, local);

  const char* elem_paths[] = {"extra", NULL};
  spine_rfe_build_filter(&filter, "lcListData", NULL, NULL, 0, elem_paths);

  cJSON* result = spine_rfe_apply(local, NULL, "id", NULL, &filter);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(local);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyWithTouchedValidatesIds) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"}],[{"id":2},{"val":"b"}],[{"id":3},{"val":"c"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":2},{"val":"x"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"id"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_INT(2)};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);

  spine_rfe_touched_t touched;
  memset(&touched, 0, sizeof(touched));
  cJSON* result = spine_rfe_apply_with_touched(local, incoming, "id",
                                                &filter, NULL, &touched);
  ASSERT_NE(nullptr, result);
  ASSERT_EQ(1, touched.count);
  EXPECT_EQ(2, touched.ids[0]);
  EXPECT_FALSE(touched.overflow);

  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyPartialMergeBySelectorEmptyIncoming) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1,"type":"foo"},{"val":"a"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* sel_keys[] = {"type"};
  spine_rfe_val_t sel_vals[] = {SPINE_RFE_STR("foo")};
  spine_rfe_build_filter(&filter, "lcListData", sel_keys, sel_vals, 1, NULL);

  cJSON* result = spine_rfe_apply(local, incoming, "id", &filter, NULL);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
}

TEST_F(RFETest, ApplyPartialAndDeleteFields) {
  const char* local_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"a"},{"extra":"x"}],[{"id":2},{"val":"b"},{"extra":"y"}]]}]})";
  const char* incoming_str =
      R"({"lcListData":[{"lcData":[[{"id":1},{"val":"z"}]]}]})";
  cJSON* local = cJSON_Parse(local_str);
  cJSON* incoming = cJSON_Parse(incoming_str);
  ASSERT_NE(nullptr, local);
  ASSERT_NE(nullptr, incoming);

  const char* del_keys[] = {"id"};
  spine_rfe_val_t del_vals[] = {SPINE_RFE_INT(2)};
  const char* elem_paths[] = {"extra", NULL};
  spine_rfe_build_filter(&filter, "lcListData", del_keys, del_vals, 1, elem_paths);

  spine_rfe_filter_t part_filter;
  memset(&part_filter, 0, sizeof(part_filter));
  spine_rfe_val_t part_vals[] = {SPINE_RFE_INT(1)};
  spine_rfe_build_filter(&part_filter, "lcListData", del_keys, part_vals, 1, NULL);

  cJSON* result = spine_rfe_apply(local, incoming, "id", &part_filter, &filter);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(local);
  cJSON_Delete(incoming);
  cJSON_Delete(result);
  spine_rfe_free_filter(&filter);
  spine_rfe_free_filter(&part_filter);
}

TEST_F(RFETest, FilterReadWithElements) {
  const char* json_str =
      R"({"loadControlLimitListData":[{"loadControlLimitData":[[{"limitId":1,"val":"a"},{"limitId":2,"val":"b"}]]}]})";
  cJSON* root = cJSON_Parse(json_str);
  ASSERT_NE(nullptr, root);

  cJSON* selector = cJSON_Parse(R"({"limitId":[1,2]})");
  ASSERT_NE(nullptr, selector);
  cJSON* elements = cJSON_Parse(R"({"val":{}})");
  ASSERT_NE(nullptr, elements);

  cJSON* result = spine_rfe_filter_read(root, "limitId", selector, elements);
  ASSERT_NE(nullptr, result);

  cJSON_Delete(root);
  cJSON_Delete(selector);
  cJSON_Delete(elements);
  cJSON_Delete(result);
}
