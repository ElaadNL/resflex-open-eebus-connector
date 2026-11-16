// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "src/spine/codec/spine_codec_node_mgmt.h"
}

class NodeMgmtTest : public ::testing::Test {
protected:
  void SetUp() override {
    memset(&disc, 0, sizeof(disc));
    memset(&uc, 0, sizeof(uc));
    memset(&sub, 0, sizeof(sub));
    memset(&bind, 0, sizeof(bind));
  }
  nm_discovery_data_t disc;
  nm_uc_data_t uc;
  nm_sub_data_t sub;
  nm_bind_data_t bind;
};

TEST_F(NodeMgmtTest, DiscoveryRoundTrip) {
  strcpy(disc.device_address, "d:dev1");
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  disc.feature_count = 2;
  strcpy(disc.features[0].feature_type, "NodeManagement");
  disc.features[0].role[0] = 's';
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 0;
  disc.features[0].feature_addr.feature = 0;
  disc.features[0].feature_addr.feature_present = true;
  strcpy(disc.features[1].feature_type, "LoadControl");
  disc.features[1].role[0] = 's';
  disc.features[1].feature_addr.entity_depth = 1;
  disc.features[1].feature_addr.entity[0] = 0;
  disc.features[1].feature_addr.feature = 1;
  disc.features[1].feature_addr.feature_present = true;

  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);

  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_EQ(2U, decoded.feature_count);
  EXPECT_STREQ("NodeManagement", decoded.features[0].feature_type);
  EXPECT_STREQ("LoadControl", decoded.features[1].feature_type);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, UCRoundTrip) {
  uc.count = 1;
  strcpy(uc.items[0].uc_supports[0].use_case_name, "LPC");
  strcpy(uc.items[0].uc_supports[0].use_case_version, "1.0.0");
  uc.items[0].uc_supports[0].use_case_available = true;
  uc.items[0].uc_support_count = 1;

  cJSON* json = nm_uc_data_encode(&uc);
  ASSERT_NE(nullptr, json);

  nm_uc_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_uc_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("LPC", decoded.items[0].uc_supports[0].use_case_name);
  EXPECT_TRUE(decoded.items[0].uc_supports[0].use_case_available);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, SubDataRoundTrip) {
  sub.count = 1;
  sub.items[0].id = 42;
  sub.items[0].server_addr.feature = 3;
  sub.items[0].server_addr.feature_present = true;

  cJSON* json = nm_sub_data_encode(&sub);
  ASSERT_NE(nullptr, json);

  nm_sub_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_sub_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(3U, decoded.items[0].server_addr.feature);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, BindDataRoundTrip) {
  bind.count = 1;
  bind.items[0].id = 7;
  bind.items[0].client_addr.feature = 1;
  bind.items[0].client_addr.feature_present = true;
  bind.items[0].server_addr.feature = 2;
  bind.items[0].server_addr.feature_present = true;

  cJSON* json = nm_bind_data_encode(&bind);
  ASSERT_NE(nullptr, json);

  nm_bind_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_bind_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(1U, decoded.items[0].client_addr.feature);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DiscoveryMultipleEntities) {
  disc.entity_count = 2;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  disc.entities[1].entity_addr.entity_depth = 2;
  disc.entities[1].entity_addr.entity[0] = 0;
  disc.entities[1].entity_addr.entity[1] = 1;
  strcpy(disc.entities[1].entity_type, "ControllableSystem");

  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);

  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_EQ(2U, decoded.entity_count);
  EXPECT_STREQ("CEM", decoded.entities[0].entity_type);
  EXPECT_STREQ("ControllableSystem", decoded.entities[1].entity_type);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, EncodeNullReturnsNull) {
  EXPECT_EQ(nullptr, nm_discovery_data_encode(nullptr));
  EXPECT_EQ(nullptr, nm_uc_data_encode(nullptr));
  EXPECT_EQ(nullptr, nm_sub_data_encode(nullptr));
  EXPECT_EQ(nullptr, nm_bind_data_encode(nullptr));
}

TEST_F(NodeMgmtTest, DecodeNullReturnsInvalid) {
  EXPECT_EQ(SPINE_ERR_INVALID, nm_discovery_data_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, nm_uc_data_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, nm_sub_data_decode(nullptr, nullptr));
  EXPECT_EQ(SPINE_ERR_INVALID, nm_bind_data_decode(nullptr, nullptr));
}

TEST_F(NodeMgmtTest, DecodeWrongKeyReturnsParseError) {
  nm_uc_data_t uc_out;
  cJSON* wrong_uc = cJSON_Parse(R"({"wrongKey":[{}]})");
  ASSERT_NE(nullptr, wrong_uc);
  EXPECT_EQ(SPINE_ERR_PARSE, nm_uc_data_decode(wrong_uc, &uc_out));
  cJSON_Delete(wrong_uc);

  nm_sub_data_t sub_out;
  cJSON* wrong_sub = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, nm_sub_data_decode(wrong_sub, &sub_out));
  cJSON_Delete(wrong_sub);

  nm_bind_data_t bind_out;
  cJSON* wrong_bind = cJSON_Parse(R"({"wrongKey":[{}]})");
  EXPECT_EQ(SPINE_ERR_PARSE, nm_bind_data_decode(wrong_bind, &bind_out));
  cJSON_Delete(wrong_bind);
}

TEST_F(NodeMgmtTest, DiscoveryStrictMissingSpecVersion) {
  /* Discovery JSON without specVersion section */
  cJSON* disc = cJSON_Parse(
      R"({"nodeManagementDetailedDiscoveryData":[{},{}]})");
  ASSERT_NE(nullptr, disc);
  nm_discovery_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_ERR_PARSE, nm_discovery_data_decode(disc, &out));
  cJSON_Delete(disc);
}

TEST_F(NodeMgmtTest, AddrEqualBothNull) {
  EXPECT_FALSE(nm_addr_equal(nullptr, nullptr));
}

TEST_F(NodeMgmtTest, StateChangeStrings) {
  EXPECT_STREQ("added", nm_state_change_str(NM_STATE_CHANGE_ADDED));
  EXPECT_EQ(nullptr, nm_state_change_str((nm_state_change_t)99));
}

TEST_F(NodeMgmtTest, AddrEqual) {
  nm_addr_t n1, n2, n3;
  memset(&n1, 0, sizeof(n1));
  memset(&n2, 0, sizeof(n2));
  memset(&n3, 0, sizeof(n3));
  n1.feature = 3;
  n1.feature_present = true;
  n2.feature = 3;
  n2.feature_present = true;
  n3.feature = 4;
  n3.feature_present = true;
  EXPECT_TRUE(nm_addr_equal(&n1, &n2));
  EXPECT_FALSE(nm_addr_equal(&n1, &n3));
}

TEST_F(NodeMgmtTest, AddrEqualDeviceDiff) {
  nm_addr_t a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  strcpy(a.device, "dev1");
  strcpy(b.device, "dev2");
  EXPECT_FALSE(nm_addr_equal(&a, &b));
}

TEST_F(NodeMgmtTest, AddrEqualEntityDepthDiff) {
  nm_addr_t a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.entity_depth = 1;
  a.entity[0] = 1;
  b.entity_depth = 2;
  b.entity[0] = 1;
  b.entity[1] = 2;
  EXPECT_FALSE(nm_addr_equal(&a, &b));
}

TEST_F(NodeMgmtTest, AddrEqualEntityContentDiff) {
  nm_addr_t a, b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));
  a.entity_depth = 1;
  a.entity[0] = 1;
  b.entity_depth = 1;
  b.entity[0] = 2;
  EXPECT_FALSE(nm_addr_equal(&a, &b));
}

TEST_F(NodeMgmtTest, StateChangeStringsAll) {
  EXPECT_STREQ("added", nm_state_change_str(NM_STATE_CHANGE_ADDED));
  EXPECT_STREQ("removed", nm_state_change_str(NM_STATE_CHANGE_REMOVED));
  EXPECT_STREQ("modified", nm_state_change_str(NM_STATE_CHANGE_MODIFIED));
  EXPECT_EQ(nullptr, nm_state_change_str((nm_state_change_t)99));
}

TEST_F(NodeMgmtTest, ParseStateChangeAll) {
  EXPECT_EQ(NM_STATE_CHANGE_NONE, nm_parse_state_change(nullptr));
  EXPECT_EQ(NM_STATE_CHANGE_NONE, nm_parse_state_change("invalid"));
  EXPECT_EQ(NM_STATE_CHANGE_ADDED, nm_parse_state_change("added"));
  EXPECT_EQ(NM_STATE_CHANGE_REMOVED, nm_parse_state_change("removed"));
  EXPECT_EQ(NM_STATE_CHANGE_MODIFIED, nm_parse_state_change("modified"));
}

TEST_F(NodeMgmtTest, DiscoveryDeviceAllFields) {
  strcpy(disc.device_address, "d:fullDev");
  strcpy(disc.device_type, "EnergyManagementSystem");
  strcpy(disc.network_feature_set, "smart");
  strcpy(disc.device_label, "My CEM");
  disc.device_label_present = true;
  strcpy(disc.device_description, "Main energy manager");
  disc.device_description_present = true;
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  strcpy(disc.entities[0].label, "Entity label");
  disc.entities[0].label_present = true;
  strcpy(disc.entities[0].description, "Entity desc");
  disc.entities[0].description_present = true;
  disc.feature_count = 1;
  strcpy(disc.features[0].feature_type, "LoadControl");
  disc.features[0].role[0] = 's';
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 0;
  disc.features[0].feature_addr.feature = 1;
  disc.features[0].feature_addr.feature_present = true;
  strcpy(disc.features[0].label, "Feat label");
  disc.features[0].label_present = true;
  strcpy(disc.features[0].description, "Feat desc");
  disc.features[0].description_present = true;

  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);

  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_STREQ("d:fullDev", decoded.device_address);
  EXPECT_STREQ("EnergyManagementSystem", decoded.device_type);
  EXPECT_STREQ("smart", decoded.network_feature_set);
  EXPECT_TRUE(decoded.device_label_present);
  EXPECT_STREQ("My CEM", decoded.device_label);
  EXPECT_TRUE(decoded.device_description_present);
  EXPECT_STREQ("Main energy manager", decoded.device_description);
  ASSERT_EQ(1U, decoded.entity_count);
  EXPECT_TRUE(decoded.entities[0].label_present);
  EXPECT_STREQ("Entity label", decoded.entities[0].label);
  EXPECT_TRUE(decoded.entities[0].description_present);
  EXPECT_STREQ("Entity desc", decoded.entities[0].description);
  ASSERT_EQ(1U, decoded.feature_count);
  EXPECT_TRUE(decoded.features[0].label_present);
  EXPECT_STREQ("Feat label", decoded.features[0].label);
  EXPECT_TRUE(decoded.features[0].description_present);
  EXPECT_STREQ("Feat desc", decoded.features[0].description);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DiscoveryAllStateChanges) {
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  disc.entities[0].last_state_change = NM_STATE_CHANGE_MODIFIED;
  disc.feature_count = 1;
  strcpy(disc.features[0].feature_type, "LoadControl");
  disc.features[0].role[0] = 's';
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 0;
  disc.features[0].feature_addr.feature = 1;
  disc.features[0].feature_addr.feature_present = true;
  disc.features[0].last_state_change = NM_STATE_CHANGE_ADDED;

  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);

  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.entity_count);
  EXPECT_EQ(NM_STATE_CHANGE_MODIFIED, decoded.entities[0].last_state_change);
  ASSERT_EQ(1U, decoded.feature_count);
  EXPECT_EQ(NM_STATE_CHANGE_ADDED, decoded.features[0].last_state_change);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DiscoverySupportedFunctions) {
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  disc.feature_count = 1;
  strcpy(disc.features[0].feature_type, "LoadControl");
  disc.features[0].role[0] = 's';
  disc.features[0].feature_addr.entity_depth = 1;
  disc.features[0].feature_addr.entity[0] = 0;
  disc.features[0].feature_addr.feature = 1;
  disc.features[0].feature_addr.feature_present = true;
  disc.features[0].supported_fn_count = 2;
  strcpy(disc.features[0].supported_fns[0].function_name, "loadControlLimitListData");
  disc.features[0].supported_fns[0].ops.read_present = true;
  disc.features[0].supported_fns[0].ops.read_partial = true;
  disc.features[0].supported_fns[0].ops.write_present = true;
  strcpy(disc.features[0].supported_fns[1].function_name, "loadControlLimitDescriptionListData");
  disc.features[0].supported_fns[1].ops.read_present = true;

  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);

  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.feature_count);
  ASSERT_EQ(2U, decoded.features[0].supported_fn_count);
  EXPECT_STREQ("loadControlLimitListData",
               decoded.features[0].supported_fns[0].function_name);
  EXPECT_TRUE(decoded.features[0].supported_fns[0].ops.read_present);
  EXPECT_TRUE(decoded.features[0].supported_fns[0].ops.read_partial);
  EXPECT_TRUE(decoded.features[0].supported_fns[0].ops.write_present);
  EXPECT_STREQ("loadControlLimitDescriptionListData",
               decoded.features[0].supported_fns[1].function_name);
  cJSON_Delete(json);
}

/* Build a valid basic discovery payload using cJSON APIs */
static cJSON* disc_valid_json(void) {
  cJSON* seq = cJSON_CreateArray();
  /* specificationVersionList */
  {
    cJSON* svl = cJSON_CreateArray();
    cJSON* sv_o = cJSON_CreateObject();
    cJSON_AddItemToObject(sv_o, "specificationVersion",
        cJSON_CreateString("1.3.0"));
    cJSON_AddItemToArray(svl, sv_o);
    cJSON* so = cJSON_CreateObject();
    cJSON_AddItemToObject(so, "specificationVersionList", svl);
    cJSON_AddItemToArray(seq, so);
  }
  /* deviceInformation */
  {
    cJSON* dev_desc = cJSON_CreateArray();
    cJSON* dev_addr = cJSON_CreateArray();
    cJSON* da = cJSON_CreateObject();
    cJSON_AddStringToObject(da, "device", "d:dev");
    cJSON_AddItemToArray(dev_addr, da);
    cJSON* dam = cJSON_CreateObject();
    cJSON_AddItemToObject(dam, "deviceAddress", dev_addr);
    cJSON_AddItemToArray(dev_desc, dam);
    cJSON* dtm = cJSON_CreateObject();
    cJSON_AddItemToObject(dtm, "deviceType", cJSON_CreateString("CEM"));
    cJSON_AddItemToArray(dev_desc, dtm);
    cJSON* nfm = cJSON_CreateObject();
    cJSON_AddItemToObject(nfm, "networkFeatureSet", cJSON_CreateString("simple"));
    cJSON_AddItemToArray(dev_desc, nfm);
    cJSON* di = cJSON_CreateObject();
    cJSON_AddItemToObject(di, "description", dev_desc);
    cJSON* di_seq = cJSON_CreateArray();
    cJSON_AddItemToArray(di_seq, di);
    cJSON* dio = cJSON_CreateObject();
    cJSON_AddItemToObject(dio, "deviceInformation", di_seq);
    cJSON_AddItemToArray(seq, dio);
  }
  /* entityInformation */
  {
    cJSON* ei_arr2 = cJSON_CreateArray();
    cJSON* ed = cJSON_CreateArray();
    cJSON* e_arr = cJSON_CreateArray();
    cJSON_AddItemToArray(e_arr, cJSON_CreateNumber(0));
    cJSON* entm = cJSON_CreateObject();
    cJSON_AddItemToObject(entm, "entity", e_arr);
    cJSON* ent_addr = cJSON_CreateArray();
    cJSON_AddItemToArray(ent_addr, entm);
    cJSON* eam = cJSON_CreateObject();
    cJSON_AddItemToObject(eam, "entityAddress", ent_addr);
    cJSON_AddItemToArray(ed, eam);
    cJSON* etm = cJSON_CreateObject();
    cJSON_AddItemToObject(etm, "entityType", cJSON_CreateString("CEM"));
    cJSON_AddItemToArray(ed, etm);
    cJSON* de = cJSON_CreateObject();
    cJSON_AddItemToObject(de, "description", ed);
    cJSON* ei_block = cJSON_CreateArray();
    cJSON_AddItemToArray(ei_block, de);
    cJSON_AddItemToArray(ei_arr2, ei_block);
    cJSON* eio = cJSON_CreateObject();
    cJSON_AddItemToObject(eio, "entityInformation", ei_arr2);
    cJSON_AddItemToArray(seq, eio);
  }
  return seq;
}

TEST_F(NodeMgmtTest, DecodeWrongMajorVersion) {
  cJSON* seq = disc_valid_json();
  ASSERT_NE(nullptr, seq);
  cJSON* svl_obj = cJSON_GetObjectItem(cJSON_GetArrayItem(seq, 0),
      "specificationVersionList");
  ASSERT_NE(nullptr, svl_obj);
  cJSON* sv_arr = cJSON_GetObjectItem(
      cJSON_GetArrayItem(svl_obj, 0), "specificationVersion");
  ASSERT_NE(nullptr, sv_arr);
  cJSON_Delete(cJSON_GetArrayItem(sv_arr, 0));
  cJSON_AddItemToArray(sv_arr, cJSON_CreateString("2.0.0"));
  cJSON* disc = cJSON_CreateObject();
  cJSON_AddItemToObject(disc, "nodeManagementDetailedDiscoveryData", seq);
  nm_discovery_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_ERR_PARSE, nm_discovery_data_decode(disc, &out));
  cJSON_Delete(disc);
}

TEST_F(NodeMgmtTest, DecodeMissingEntityInfo) {
  /* Build a discovery JSON without entityInformation */
  cJSON* seq = cJSON_CreateArray();
  cJSON* svl = cJSON_CreateArray();
  cJSON* sv_o = cJSON_CreateObject();
  cJSON_AddItemToObject(sv_o, "specificationVersion",
      cJSON_CreateString("1.3.0"));
  cJSON_AddItemToArray(svl, sv_o);
  cJSON* so = cJSON_CreateObject();
  cJSON_AddItemToObject(so, "specificationVersionList", svl);
  cJSON_AddItemToArray(seq, so);

  cJSON* dev_desc = cJSON_CreateArray();
  cJSON* dev_addr = cJSON_CreateArray();
  cJSON* da = cJSON_CreateObject();
  cJSON_AddStringToObject(da, "device", "d:dev");
  cJSON_AddItemToArray(dev_addr, da);
  cJSON* dam = cJSON_CreateObject();
  cJSON_AddItemToObject(dam, "deviceAddress", dev_addr);
  cJSON_AddItemToArray(dev_desc, dam);
  cJSON* dtm = cJSON_CreateObject();
  cJSON_AddItemToObject(dtm, "deviceType", cJSON_CreateString("CEM"));
  cJSON_AddItemToArray(dev_desc, dtm);
  cJSON* nfm = cJSON_CreateObject();
  cJSON_AddItemToObject(nfm, "networkFeatureSet", cJSON_CreateString("simple"));
  cJSON_AddItemToArray(dev_desc, nfm);
  cJSON* di = cJSON_CreateObject();
  cJSON_AddItemToObject(di, "description", dev_desc);
  cJSON* di_seq = cJSON_CreateArray();
  cJSON_AddItemToArray(di_seq, di);
  cJSON* dio = cJSON_CreateObject();
  cJSON_AddItemToObject(dio, "deviceInformation", di_seq);
  cJSON_AddItemToArray(seq, dio);

  cJSON* disc = cJSON_CreateObject();
  cJSON_AddItemToObject(disc, "nodeManagementDetailedDiscoveryData", seq);
  nm_discovery_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_ERR_PARSE, nm_discovery_data_decode(disc, &out));
  cJSON_Delete(disc);
}

static void add_feature_info(cJSON* seq, cJSON* feat_list) {
  cJSON* fo = cJSON_CreateObject();
  cJSON_AddItemToObject(fo, "featureInformation", feat_list);
  cJSON_AddItemToArray(seq, fo);
}
/* Build a featureInformation block. If omit is non-NULL, skip that element. */
static cJSON* mk_feature_block_without(const char* omit) {
  cJSON* feat = cJSON_CreateArray();
  cJSON* fd = cJSON_CreateArray();
  if (!omit || strcmp(omit, "featureAddress") != 0) {
    cJSON* fa = cJSON_CreateArray();
    {
      cJSON* fe = cJSON_CreateArray();
      cJSON_AddItemToArray(fe, cJSON_CreateNumber(0));
      cJSON* o1 = cJSON_CreateObject();
      cJSON_AddItemToObject(o1, "entity", fe);
      cJSON_AddItemToArray(fa, o1);
    }
    {
      cJSON* o2 = cJSON_CreateObject();
      cJSON_AddItemToObject(o2, "feature", cJSON_CreateNumber(0));
      cJSON_AddItemToArray(fa, o2);
    }
    cJSON* fm = cJSON_CreateObject();
    cJSON_AddItemToObject(fm, "featureAddress", fa);
    cJSON_AddItemToArray(fd, fm);
  }
  if (!omit || strcmp(omit, "featureType") != 0) {
    cJSON* fm = cJSON_CreateObject();
    cJSON_AddItemToObject(fm, "featureType", cJSON_CreateString("NM"));
    cJSON_AddItemToArray(fd, fm);
  }
  if (!omit || strcmp(omit, "role") != 0) {
    cJSON* fm = cJSON_CreateObject();
    cJSON_AddItemToObject(fm, "role", cJSON_CreateString("special"));
    cJSON_AddItemToArray(fd, fm);
  }
  cJSON* fi = cJSON_CreateObject();
  cJSON_AddItemToObject(fi, "description", fd);
  cJSON_AddItemToArray(feat, fi);
  return feat;
}

TEST_F(NodeMgmtTest, DecodeFeatureMissingFeatureAddress) {
  cJSON* seq = disc_valid_json();
  ASSERT_NE(nullptr, seq);
  add_feature_info(seq, mk_feature_block_without("featureAddress"));
  cJSON* disc = cJSON_CreateObject();
  cJSON_AddItemToObject(disc, "nodeManagementDetailedDiscoveryData", seq);
  nm_discovery_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_ERR_PARSE, nm_discovery_data_decode(disc, &out));
  cJSON_Delete(disc);
}

TEST_F(NodeMgmtTest, DecodeFeatureMissingRole) {
  cJSON* seq = disc_valid_json();
  ASSERT_NE(nullptr, seq);
  add_feature_info(seq, mk_feature_block_without("role"));
  cJSON* disc = cJSON_CreateObject();
  cJSON_AddItemToObject(disc, "nodeManagementDetailedDiscoveryData", seq);
  nm_discovery_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_ERR_PARSE, nm_discovery_data_decode(disc, &out));
  cJSON_Delete(disc);
}

TEST_F(NodeMgmtTest, UcFullFields) {
  uc.count = 1;
  strcpy(uc.items[0].actor, "EnergyGuard");
  uc.items[0].address.entity_depth = 1;
  uc.items[0].address.entity[0] = 0;
  uc.items[0].address.feature = 1;
  uc.items[0].address.feature_present = true;
  uc.items[0].uc_support_count = 1;
  strcpy(uc.items[0].uc_supports[0].use_case_name, "LPC");
  strcpy(uc.items[0].uc_supports[0].use_case_version, "1.0.0");
  uc.items[0].uc_supports[0].use_case_available = true;
  uc.items[0].uc_supports[0].scenario_count = 2;
  uc.items[0].uc_supports[0].scenario_support[0] = 1;
  uc.items[0].uc_supports[0].scenario_support[1] = 2;
  strcpy(uc.items[0].uc_supports[0].use_case_doc_sub_revision, "release");

  cJSON* json = nm_uc_data_encode(&uc);
  ASSERT_NE(nullptr, json);

  nm_uc_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_uc_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("EnergyGuard", decoded.items[0].actor);
  ASSERT_EQ(1U, decoded.items[0].uc_support_count);
  EXPECT_STREQ("LPC", decoded.items[0].uc_supports[0].use_case_name);
  EXPECT_STREQ("release", decoded.items[0].uc_supports[0].use_case_doc_sub_revision);
  ASSERT_EQ(2U, decoded.items[0].uc_supports[0].scenario_count);
  EXPECT_EQ(1U, decoded.items[0].uc_supports[0].scenario_support[0]);
  EXPECT_EQ(2U, decoded.items[0].uc_supports[0].scenario_support[1]);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, SubAddrFullDevice) {
  sub.count = 1;
  sub.items[0].id = 42;
  strcpy(sub.items[0].client_addr.device, "clientDev");
  sub.items[0].client_addr.entity_depth = 1;
  sub.items[0].client_addr.entity[0] = 0;
  sub.items[0].client_addr.feature = 1;
  sub.items[0].client_addr.feature_present = true;
  strcpy(sub.items[0].server_addr.device, "serverDev");
  sub.items[0].server_addr.entity_depth = 1;
  sub.items[0].server_addr.entity[0] = 1;
  sub.items[0].server_addr.feature = 3;
  sub.items[0].server_addr.feature_present = true;

  cJSON* json = nm_sub_data_encode(&sub);
  ASSERT_NE(nullptr, json);

  nm_sub_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_sub_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_EQ(42U, decoded.items[0].id);
  EXPECT_STREQ("clientDev", decoded.items[0].client_addr.device);
  EXPECT_EQ(1U, decoded.items[0].client_addr.feature);
  EXPECT_STREQ("serverDev", decoded.items[0].server_addr.device);
  EXPECT_EQ(3U, decoded.items[0].server_addr.feature);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, BindAddrFullDevice) {
  bind.count = 1;
  bind.items[0].id = 7;
  strcpy(bind.items[0].client_addr.device, "cDev");
  bind.items[0].client_addr.entity_depth = 1;
  bind.items[0].client_addr.entity[0] = 0;
  bind.items[0].client_addr.feature = 1;
  bind.items[0].client_addr.feature_present = true;
  strcpy(bind.items[0].server_addr.device, "sDev");
  bind.items[0].server_addr.entity_depth = 1;
  bind.items[0].server_addr.entity[0] = 1;
  bind.items[0].server_addr.feature = 2;
  bind.items[0].server_addr.feature_present = true;

  cJSON* json = nm_bind_data_encode(&bind);
  ASSERT_NE(nullptr, json);

  nm_bind_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_bind_data_decode(json, &decoded));
  ASSERT_EQ(1U, decoded.count);
  EXPECT_STREQ("cDev", decoded.items[0].client_addr.device);
  EXPECT_STREQ("sDev", decoded.items[0].server_addr.device);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, SubBindMaxCount) {
  sub.count = NM_SB_ENTRY_MAX;
  for (uint16_t i = 0; i < NM_SB_ENTRY_MAX; i++) {
    sub.items[i].id = i;
    sub.items[i].server_addr.feature = i;
    sub.items[i].server_addr.feature_present = true;
  }

  cJSON* json = nm_sub_data_encode(&sub);
  ASSERT_NE(nullptr, json);

  nm_sub_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_sub_data_decode(json, &decoded));
  EXPECT_EQ(NM_SB_ENTRY_MAX, decoded.count);
  EXPECT_EQ(0U, decoded.items[0].id);
  EXPECT_EQ(NM_SB_ENTRY_MAX - 1, decoded.items[NM_SB_ENTRY_MAX - 1].id);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DecodeUcMissingUseCaseKey) {
  /* use case entry without useCaseName should decode but with empty name */
  nm_uc_data_t uc_in;
  memset(&uc_in, 0, sizeof(uc_in));
  uc_in.count = 1;
  strcpy(uc_in.items[0].actor, "EG");
  uc_in.items[0].uc_support_count = 1;
  strcpy(uc_in.items[0].uc_supports[0].use_case_version, "1.0.0");
  uc_in.items[0].uc_supports[0].use_case_available = true;
  cJSON* json = nm_uc_data_encode(&uc_in);
  ASSERT_NE(nullptr, json);
  nm_uc_data_t out;
  memset(&out, 0, sizeof(out));
  EXPECT_EQ(SPINE_OK, nm_uc_data_decode(json, &out));
  ASSERT_EQ(1U, out.count);
  ASSERT_EQ(1U, out.items[0].uc_support_count);
  EXPECT_STREQ("", out.items[0].uc_supports[0].use_case_name);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DeviceDescriptionOmittedWhenAbsent) {
  strcpy(disc.device_address, "d:wm");
  strcpy(disc.device_type, "EnergyManagementSystem");
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  /* device_description intentionally NOT set */
  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);
  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_FALSE(decoded.device_description_present);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DeviceDescriptionPassedThroughWhenPresent) {
  strcpy(disc.device_address, "d:wm2");
  strcpy(disc.device_type, "EnergyManagementSystem");
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  strcpy(disc.device_description, "Main energy manager");
  disc.device_description_present = true;
  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);
  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_TRUE(decoded.device_description_present);
  EXPECT_STREQ("Main energy manager", decoded.device_description);
  cJSON_Delete(json);
}

TEST_F(NodeMgmtTest, DeviceDescriptionWithParensPassedThrough) {
  strcpy(disc.device_address, "d:wm3");
  strcpy(disc.device_type, "EnergyManagementSystem");
  disc.entity_count = 1;
  disc.entities[0].entity_addr.entity_depth = 1;
  disc.entities[0].entity_addr.entity[0] = 0;
  strcpy(disc.entities[0].entity_type, "CEM");
  strcpy(disc.device_description, "Main energy manager (grid)");
  disc.device_description_present = true;
  cJSON* json = nm_discovery_data_encode(&disc);
  ASSERT_NE(nullptr, json);
  nm_discovery_data_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  EXPECT_EQ(SPINE_OK, nm_discovery_data_decode(json, &decoded));
  EXPECT_TRUE(decoded.device_description_present);
  EXPECT_STREQ("Main energy manager (grid)", decoded.device_description);
  cJSON_Delete(json);
}
