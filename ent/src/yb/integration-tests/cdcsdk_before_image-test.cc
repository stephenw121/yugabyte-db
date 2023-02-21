// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

namespace yb {
namespace cdc {
namespace enterprise {

namespace {

void DisableYsqlPackedRow() {
  ASSERT_OK(SET_FLAG(ysql_enable_packed_row, false));
}

}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestModifyPrimaryKeyBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdatePrimaryKey(1 /* key */, 9 /* value */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 2, 1, 1, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 3, 0, 1, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {1, 3}, {0, 0}, {1, 3}, {9, 3}, {0, 0}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {}, {1, 3}, {}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSchemaChangeBeforeImage)) {
  DisableYsqlPackedRow();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 2)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 3 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 INT"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 4 WHERE key = 1"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (4, 5, 6)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 99 WHERE key = 1"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 99 WHERE key = 4"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_2 = 66 WHERE key = 4"));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {2, 2, 5, 0, 0, 0};
  const uint32_t expected_count_packed_row[] = {2, 3, 4, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {
      {0, 0, 0}, {1, 2, INT_MAX},  {1, 3, INT_MAX}, {0, 0, INT_MAX}, {1, 4, INT_MAX},
      {4, 5, 6}, {1, 99, INT_MAX}, {4, 99, 6},      {4, 99, 66}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {1, 2, INT_MAX}, {}, {1, 3, INT_MAX}, {}, {1, 4, INT_MAX}, {4, 5, 6}, {4, 99, 6}};
  ExpectedRecordWithThreeColumns expected_before_image_records_with_packed_row[] = {
      {}, {}, {0, 0, 0}, {}, {0, 0, 0}, {}, {0, 0, 0}, {4, 5, 6}, {4, 99, 6}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  // If the packed row is enabled and there are multiple tables altered, if CDC fail to get before
  // image row with the current running schema version, then it will ignore the before image tuples.
  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (i <= 6) {
      CheckRecordWithThreeColumns(
          record, expected_records[i], count, true,
          FLAGS_ysql_enable_packed_row ? expected_before_image_records_with_packed_row[i]
                                       : expected_before_image_records[i]);
    } else {
      CheckRecordWithThreeColumns(
          record, expected_records[i], count, true,
          FLAGS_ysql_enable_packed_row ? expected_before_image_records_with_packed_row[i]
                                       : expected_before_image_records[i],
          true);
    }
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  CheckCount(FLAGS_ysql_enable_packed_row ? expected_count_packed_row : expected_count, count);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestBeforeImageRetention)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 1000000;
  constexpr int kCompactionTimeoutSec = 60;

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion)";
  SleepFor(MonoDelta::FromSeconds(2));

  ASSERT_OK(test_client()->FlushTables(
      {table.table_id()}, /* add_indexes = */ false,
      /* timeout_secs = */ kCompactionTimeoutSec, /* is_compaction = */ true));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 3, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {1, 3}, {1, 4}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestBeforeImageExpiration)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  // Testing compaction without compaction file filtering for TTL expiration.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion)";
  SleepFor(MonoDelta::FromSeconds(5));
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);

  auto checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));
  OpId op_id = {change_resp.cdc_sdk_checkpoint().term(), change_resp.cdc_sdk_checkpoint().index()};
  auto set_resp2 =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, op_id, change_resp.safe_hybrid_time()));
  ASSERT_FALSE(set_resp2.has_error());

  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  auto count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_EQ(count_before_compaction, count_after_compaction);

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {0, 0, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {0, 2, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{1, 3}, {1, 4}};
  ExpectedRecord expected_before_image_records[] = {{1, 2}, {1, 3}};

  GetChangesResponsePB change_resp2 =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));

  uint32_t record_size = change_resp2.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp2.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }

  checkpoints = ASSERT_RESULT(GetCDCCheckpoint(stream_id, tablets));
  OpId op_id2 = {
      change_resp.cdc_sdk_checkpoint().term(), change_resp2.cdc_sdk_checkpoint().index()};
  auto set_resp3 =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, op_id2, change_resp2.safe_hybrid_time()));
  ASSERT_FALSE(set_resp2.has_error());

  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_GE(count_after_compaction, 1);
}

// Insert one row, update the inserted row twice and verify before image.
// Expected records: (DDL, INSERT, UPDATE).
TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSingleShardUpdateBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  auto tablets = ASSERT_RESULT(SetUpCluster());
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 1, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 3, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {1, 3}, {1, 4}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {1, 2}, {1, 3}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestMultiShardUpdateBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({1, 88});
  col_val_map.insert({1, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      1 /* start */, 2 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({1, 999});
  col_val_map.insert({2, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 2, 4, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 6, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {0, 0},   {1, 88}, {1, 888}, {0, 0},
                                       {2, 3}, {0, 0}, {1, 999}, {2, 99}, {0, 0}};
  ExpectedRecord expected_before_image_records[] = {{}, {}, {},       {1, 2}, {1, 2}, {},
                                                    {}, {}, {1, 888}, {2, 3}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestSingleMultiShardUpdateBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({2, 88});
  col_val_map.insert({2, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({2, 999});
  col_val_map.insert({3, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      3 /* start */, 4 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 3, 6, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 9, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0},   {1, 2}, {1, 3}, {1, 4}, {2, 3},   {0, 0},  {2, 88},
                                       {2, 888}, {0, 0}, {3, 4}, {0, 0}, {2, 999}, {3, 99}, {0, 0}};
  ExpectedRecord expected_before_image_records[] = {
      {}, {}, {1, 2}, {1, 3}, {}, {}, {2, 3}, {2, 3}, {}, {}, {}, {2, 888}, {3, 4}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestMultiSingleShardUpdateBeforeImage)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  std::multimap<uint32_t, uint32_t> col_val_map;
  col_val_map.insert({1, 88});
  col_val_map.insert({1, 888});

  ASSERT_OK(WriteAndUpdateRowsHelper(
      1 /* start */, 2 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  col_val_map.clear();
  col_val_map.insert({1, 999});
  col_val_map.insert({2, 99});
  ASSERT_OK(WriteAndUpdateRowsHelper(
      2 /* start */, 3 /* end */, &test_cluster_, true, col_val_map, table.table_id()));

  ASSERT_OK(WriteRows(3 /* start */, 4 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(3 /* key */, 5 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(3 /* key */, 6 /* value */, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 3, 6, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 9, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecord expected_records[] = {{0, 0}, {1, 2}, {0, 0}, {1, 88},  {1, 888},
                                       {0, 0}, {2, 3}, {0, 0}, {1, 999}, {2, 99},
                                       {0, 0}, {3, 4}, {3, 5}, {3, 6}};
  ExpectedRecord expected_before_image_records[] = {
      {}, {}, {}, {1, 2}, {1, 2}, {}, {}, {}, {1, 888}, {2, 3}, {}, {}, {3, 4}, {3, 5}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecord(record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(SingleShardUpdateMultiColumnBeforeImage)) {
  uint32_t num_cols = 3;
  map<std::string, uint32_t> col_val_map;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  auto tablets = ASSERT_RESULT(SetUpClusterMultiColumnUsecase(num_cols));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ASSERT_OK(WriteRows(1 /* start */, 4 /* end */, &test_cluster_, num_cols));

  col_val_map.insert(pair<std::string, uint32_t>("col2", 9));
  col_val_map.insert(pair<std::string, uint32_t>("col3", 10));
  ASSERT_OK(UpdateRows(1 /* key */, col_val_map, &test_cluster_));
  ASSERT_OK(UpdateRows(2 /* key */, col_val_map, &test_cluster_));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {1, 3, 2, 0, 0, 0};
  const uint32_t expected_count_with_packed_row[] = {1, 5, 0, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {{0, 0, 0}, {1, 2, 3},  {2, 3, 4},
                                                       {3, 4, 5}, {1, 9, 10}, {2, 9, 10}};

  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 2, 3}, {2, 3, 4}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    LOG(INFO) << change_resp.cdc_sdk_proto_records(i).DebugString();
  }
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecordWithThreeColumns(
        record, expected_records[i], count, true, expected_before_image_records[i], true);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";
  if (FLAGS_ysql_enable_packed_row) {
    // For packed row if all the columns of a row is updated, it come as INSERT record.
    CheckCount(expected_count_with_packed_row, count);
  } else {
    CheckCount(expected_count, count);
  }
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCompactionWithoutBeforeImage)) {
  FLAGS_update_min_cdc_indices_interval_secs = 1;
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) = 0;
  // Testing compaction without compaction file filtering for TTL expiration.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_enable_ttl_file_filter) = false;

  ASSERT_OK(WriteRows(1 /* start */, 2 /* end */, &test_cluster_));
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion)";
  SleepFor(MonoDelta::FromSeconds(5));
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);

  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
  auto count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_LT(count_after_compaction, count_before_compaction);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCompactionWithSnapshotAndNoBeforeImage)) {
  FLAGS_update_min_cdc_indices_interval_secs = 1;
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));
  CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // Read from cdc_state once the snapshot is initiated.
  auto expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);
  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_update = true;
  GetChangesResponsePB change_resp_updated;
  vector<int> excepted_result(2);
  vector<int> actual_result(2);
  while (true) {
    if (do_update) {
      ASSERT_OK(UpdateRows(100, 1001, &test_cluster_));
      ASSERT_OK(DeleteRows(1, &test_cluster_));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
      ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      do_update = false;
    }
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      std::stringstream s;

      if (record.row_message().op() == RowMessage::READ) {
        for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
          s << " " << record.row_message().new_tuple(jdx).datum_int32();
        }
        LOG(INFO) << "row: " << i << " : " << s.str();
        // we should only get row values w.r.t snapshot, not changed values during snapshot.
        if (actual_result[0] == 100) {
          excepted_result[0] = 100;
          excepted_result[1] = 101;
          ASSERT_EQ(actual_result, excepted_result);
        } else if (actual_result[0] == 1) {
          excepted_result[0] = 1;
          excepted_result[1] = 2;
          ASSERT_EQ(actual_result, excepted_result);
        }
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
  }
  ASSERT_EQ(reads_snapshot, 100);
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
  auto result = test_cluster_.mini_cluster_->CompactTablets();

  ASSERT_OK(WriteRows(101 /* start */, 102 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion): "
            << change_resp.cdc_sdk_proto_records_size();
  ASSERT_OK(UpdateRows(1 /* key */, 5 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 10 /* value */, &test_cluster_));
  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  int count_after_compaction;
  ASSERT_OK(WaitFor(
      [&]() {
        auto result = test_cluster_.mini_cluster_->CompactTablets();
        if (!result.ok()) {
          return false;
        }
        count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        if (count_after_compaction <= count_before_compaction) {
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(60),
      "Compaction is resticted for the stream."));
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_LE(count_after_compaction, count_before_compaction);
  // Read from cdc_state once the stream is done, without before image.
  expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_EQ((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestCompactionWithSnapshotAndBeforeImage)) {
  FLAGS_update_min_cdc_indices_interval_secs = 1;
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(WriteRows(1 /* start */, 101 /* end */, &test_cluster_));
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min()));
  ASSERT_FALSE(set_resp.has_error());
  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDCSnapshot(stream_id, tablets));
  // Read from cdc_state once the snapshot is initiated.
  auto expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);
  // Count the number of snapshot READs.
  uint32_t reads_snapshot = 0;
  bool do_update = true;
  GetChangesResponsePB change_resp_updated;
  vector<int> excepted_result(2);
  vector<int> actual_result(2);
  while (true) {
    if (do_update) {
      ASSERT_OK(UpdateRows(100, 1001, &test_cluster_));
      ASSERT_OK(DeleteRows(1, &test_cluster_));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
      ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
      do_update = false;
    }
    change_resp_updated = ASSERT_RESULT(UpdateCheckpoint(stream_id, tablets, &change_resp));
    uint32_t record_size = change_resp_updated.cdc_sdk_proto_records_size();
    if (record_size == 0) {
      break;
    }
    uint32_t read_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp_updated.cdc_sdk_proto_records(i);
      std::stringstream s;

      if (record.row_message().op() == RowMessage::READ) {
        for (int jdx = 0; jdx < record.row_message().new_tuple_size(); jdx++) {
          s << " " << record.row_message().new_tuple(jdx).datum_int32();
        }
        LOG(INFO) << "row: " << i << " : " << s.str();
        // we should only get row values w.r.t snapshot, not changed values during snapshot.
        if (actual_result[0] == 100) {
          excepted_result[0] = 100;
          excepted_result[1] = 101;
          ASSERT_EQ(actual_result, excepted_result);
        } else if (actual_result[0] == 1) {
          excepted_result[0] = 1;
          excepted_result[1] = 2;
          ASSERT_EQ(actual_result, excepted_result);
        }
        read_count++;
      }
    }
    reads_snapshot += read_count;
    change_resp = change_resp_updated;
  }
  ASSERT_EQ(reads_snapshot, 100);
  auto peers = ListTabletPeers(test_cluster(), ListPeersFilter::kLeaders);
  auto result = test_cluster_.mini_cluster_->CompactTablets();

  ASSERT_OK(WriteRows(101 /* start */, 102 /* end */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 3 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 4 /* value */, &test_cluster_));

  change_resp =
      ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
  LOG(INFO) << "Sleeping to expire files according to TTL (history retention prevents deletion): "
            << change_resp.cdc_sdk_proto_records_size();
  ASSERT_OK(UpdateRows(1 /* key */, 5 /* value */, &test_cluster_));
  ASSERT_OK(UpdateRows(1 /* key */, 10 /* value */, &test_cluster_));
  auto count_before_compaction = CountEntriesInDocDB(peers, table.table_id());
  int count_after_compaction;
  ASSERT_OK(WaitFor(
      [&]() {
        auto result = test_cluster_.mini_cluster_->CompactTablets();
        if (!result.ok()) {
          return false;
        }
        count_after_compaction = CountEntriesInDocDB(peers, table.table_id());
        if (count_after_compaction < count_before_compaction) {
          return true;
        }
        return false;
      },
      MonoDelta::FromSeconds(60),
      "Compaction is resticted for the stream."));
  LOG(INFO) << "count_before_compaction: " << count_before_compaction
            << " count_after_compaction: " << count_after_compaction;
  ASSERT_LT(count_after_compaction, count_before_compaction);
  // Read from cdc_state once the stream is done, without before image.
  expected_row = ReadFromCdcStateTable(stream_id, tablets[0].tablet_id());
  if (!expected_row.ok()) {
    FAIL();
  }
  ASSERT_GE((*expected_row).op_id.term, 0);
  ASSERT_GE((*expected_row).op_id.index, 0);
  ASSERT_NE((*expected_row).cdc_sdk_safe_time, HybridTime::kInvalid);
  ASSERT_GE((*expected_row).cdc_sdk_latest_active_time, 0);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestColumnDropBeforeImage)) {
  DisableYsqlPackedRow();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp = ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets));
  ASSERT_FALSE(set_resp.has_error());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 2)"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_1 = 3 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value_2 INT"));
  ASSERT_OK(conn.Execute("UPDATE test_table SET value_2 = 4 WHERE key = 1"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP COLUMN value_2"));

  // The count array stores counts of DDL, INSERT, UPDATE, DELETE, READ, TRUNCATE in that order.
  const uint32_t expected_count[] = {3, 1, 2, 0, 0, 0};
  uint32_t count[] = {0, 0, 0, 0, 0, 0};

  ExpectedRecordWithThreeColumns expected_records[] = {
      {0, 0, 0}, {1, 2, INT_MAX}, {1, 3, INT_MAX}, {0, 0, INT_MAX}, {1, 3, 4}, {}};
  ExpectedRecordWithThreeColumns expected_before_image_records[] = {
      {}, {}, {1, 2, INT_MAX}, {}, {1, 3, INT_MAX}, {}};

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  for (uint32_t i = 0; i < record_size; ++i) {
    const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    CheckRecordWithThreeColumns(
        record, expected_records[i], count, true, expected_before_image_records[i]);
  }
  LOG(INFO) << "Got " << count[1] << " insert record and " << count[2] << " update record";

  CheckCount(expected_count, count);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestLargeTransactionUpdateRowsWithBeforeImage)) {
  EnableVerboseLoggingForModule("cdc_service", 1);
  EnableVerboseLoggingForModule("cdcsdk_producer", 1);
  DisableYsqlPackedRow();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  FLAGS_update_min_cdc_indices_interval_secs = 1;
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(
      &test_cluster_, kNamespaceName, kTableName, 1, true, false, 0, false, "", "public", 3));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min(), kuint64max, false, 0, true));
  ASSERT_FALSE(set_resp.has_error());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Do batch insert into the table.
  uint32_t start_idx = 1;
  uint32_t end_idx = 1001;
  uint32_t batch_range = 1000;
  int batch_count = 4;
  for (int idx = 0; idx < batch_count; idx++) {
    ASSERT_OK(WriteRowsHelper(start_idx /* start */, end_idx /* end */, &test_cluster_, true, 3));
    start_idx = end_idx;
    end_idx += batch_range;
  }

  // Update all row where key is even
  ASSERT_OK(conn.Execute("UPDATE test_table set col2 = col2 + 1 where col1 % 2 = 0"));

  bool first_get_changes = true;
  GetChangesResponsePB change_resp;
  int insert_count = 0;
  int update_count = 0;
  while (true) {
    if (first_get_changes) {
      change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
      first_get_changes = false;

    } else {
      change_resp =
          ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    }

    if (change_resp.cdc_sdk_proto_records_size() == 0) {
      break;
    }
    ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);
        // Old tuples validations
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 0);

        // New tuples validations
        ASSERT_GT(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().new_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().new_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().new_tuple(1).datum_int32(), 4002);
        ASSERT_EQ(record.row_message().table(), kTableName);
        insert_count += 1;
      } else if (record.row_message().op() == RowMessage::UPDATE) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 3);
        ASSERT_EQ(record.row_message().old_tuple_size(), 3);
        // The old tuple key should match the new tuple key.
        ASSERT_EQ(
            record.row_message().old_tuple(0).datum_int32(),
            record.row_message().new_tuple(0).datum_int32());
        // The updated value of value_1 column should be more than 1 of its before image.
        ASSERT_EQ(
            record.row_message().new_tuple(1).datum_int32(),
            record.row_message().old_tuple(1).datum_int32() + 1);
        ASSERT_EQ(record.row_message().table(), kTableName);
        update_count += 1;
      }
    }
  }
  LOG(INFO) << "Total insert count: " << insert_count << " update counts: " << update_count;
  ASSERT_EQ(insert_count, 2 * update_count);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestLargeTransactionDeleteRowsWithBeforeImage)) {
  EnableVerboseLoggingForModule("cdc_service", 1);
  EnableVerboseLoggingForModule("cdcsdk_producer", 1);
  // ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  FLAGS_update_min_cdc_indices_interval_secs = 1;
  FLAGS_cdc_state_checkpoint_update_interval_ms = 0;
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  CDCStreamId stream_id =
      ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT, CDCRecordType::ALL));
  auto set_resp =
      ASSERT_RESULT(SetCDCCheckpoint(stream_id, tablets, OpId::Min(), kuint64max, false, 0, true));
  ASSERT_FALSE(set_resp.has_error());

  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));

  // Do batch insert into the table.
  uint32_t start_idx = 1;
  uint32_t end_idx = 1001;
  uint32_t batch_range = 1000;
  int batch_count = 4;
  for (int idx = 0; idx < batch_count; idx++) {
    ASSERT_OK(WriteRowsHelper(start_idx /* start */, end_idx /* end */, &test_cluster_, true));
    start_idx = end_idx;
    end_idx += batch_range;
  }

  // Delete all rows where key is even.
  ASSERT_OK(conn.Execute("DELETE from test_table where key % 2 = 0"));

  bool first_get_changes = true;
  GetChangesResponsePB change_resp;
  int insert_count = 0;
  int delete_count = 0;
  while (true) {
    if (first_get_changes) {
      change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));
      first_get_changes = false;

    } else {
      change_resp =
          ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets, &change_resp.cdc_sdk_checkpoint()));
    }

    if (change_resp.cdc_sdk_proto_records_size() == 0) {
      break;
    }
    ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    for (uint32_t i = 0; i < record_size; ++i) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
      if (record.row_message().op() == RowMessage::INSERT) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 2);
        ASSERT_EQ(record.row_message().old_tuple_size(), 2);
        // Old tuples validations
        ASSERT_EQ(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().old_tuple(1).datum_int32(), 0);

        // New tuples validations
        ASSERT_GT(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().new_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().new_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().new_tuple(1).datum_int32(), 4002);
        ASSERT_EQ(record.row_message().table(), kTableName);
        insert_count += 1;
      } else if (record.row_message().op() == RowMessage::DELETE) {
        ASSERT_EQ(record.row_message().new_tuple_size(), 2);
        ASSERT_EQ(record.row_message().old_tuple_size(), 2);
        // The old tuple key should match the new tuple key.
        ASSERT_GT(record.row_message().old_tuple(0).datum_int32(), 0);
        ASSERT_LE(record.row_message().old_tuple(0).datum_int32(), 4000);
        ASSERT_GT(record.row_message().old_tuple(1).datum_int32(), 1);
        ASSERT_LE(record.row_message().old_tuple(1).datum_int32(), 4002);

        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), 0);
        ASSERT_EQ(record.row_message().table(), kTableName);
        delete_count += 1;
      }
    }
  }
  LOG(INFO) << "Total insert count: " << insert_count << " update counts: " << delete_count;
  ASSERT_EQ(insert_count, 2 * delete_count);
}

}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
