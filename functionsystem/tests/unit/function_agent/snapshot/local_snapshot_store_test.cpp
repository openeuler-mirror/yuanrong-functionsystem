/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "common/proto/pb/message_pb.h"
#include "function_agent/snapshot/local_snapshot_store.h"

namespace functionsystem::function_agent {
namespace {
namespace fs = std::filesystem;

TEST(LocalSnapshotProtoTest, UsesStableInternalFieldNumbers)
{
    const auto *candidate = messages::SnapshotRuntimeRequest::descriptor()
        ->FindFieldByName("localRecoveryCandidate");
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->number(), 13);
    const auto *localSnapshot = messages::SnapshotRuntimeResponse::descriptor()
        ->FindFieldByName("localSnapshot");
    ASSERT_NE(localSnapshot, nullptr);
    EXPECT_EQ(localSnapshot->number(), 10);
}

class LocalSnapshotStoreTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto pattern = (fs::temp_directory_path() / "local-snapshot-store-test-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        root_ = mkdtemp(buffer.data());
        store_ = std::make_unique<LocalSnapshotStore>(root_);
    }

    void TearDown() override
    {
        store_.reset();
        std::error_code error;
        fs::remove_all(root_, error);
    }

    static LocalSnapshotCommitRequest Request(const std::string &snapshotID)
    {
        LocalSnapshotCommitRequest request;
        request.snapshotID = snapshotID;
        request.recoveryCandidate = true;
        request.instanceID = "instance-a";
        request.tenantHash = "tenant-hash";
        request.sourceRuntimeID = "runtime-a";
        request.sourceSandboxID = "sandbox-a";
        request.sourceInstanceVersion = 12;
        request.createdAtUnixSeconds = 1787670000;
        return request;
    }

    static void Write(const fs::path &path, const std::string &contents)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << contents;
        output.close();
        ASSERT_TRUE(output.good());
    }

    LocalSnapshotDescriptor Commit(const std::string &snapshotID, const std::string &contents)
    {
        const auto request = Request(snapshotID);
        const auto prepared = store_->Prepare(request);
        EXPECT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
        Write(prepared.directory / "checkpoint.img", contents);
        const auto committed = store_->Commit(request);
        EXPECT_TRUE(committed.status.IsOk()) << committed.status.ToString();
        return committed.descriptor;
    }

    fs::path root_;
    std::unique_ptr<LocalSnapshotStore> store_;
};

TEST_F(LocalSnapshotStoreTest, CommitsOpaqueCheckpointWithoutSidecarManifest)
{
    const auto descriptor = Commit("checkpoint-a", "payload");
    EXPECT_TRUE(descriptor.recoveryCandidate);
    EXPECT_EQ(descriptor.size, 7U);
    EXPECT_EQ(store_->List().size(), 1U);
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-a" / "checkpoint.img"));
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a" / "snapshot.meta"));
}

TEST_F(LocalSnapshotStoreTest, ExistingRegularCheckpointIsAnIdempotentReplay)
{
    Commit("checkpoint-a", "payload");
    const auto replay = store_->Prepare(Request("checkpoint-a"));
    ASSERT_TRUE(replay.status.IsOk()) << replay.status.ToString();
    EXPECT_TRUE(replay.replayed);
}

TEST_F(LocalSnapshotStoreTest, RejectsSymlinkedCheckpoint)
{
    const auto request = Request("checkpoint-link");
    const auto prepared = store_->Prepare(request);
    ASSERT_TRUE(prepared.status.IsOk());
    Write(root_ / "outside.img", "payload");
    fs::create_symlink(root_ / "outside.img", prepared.directory / "checkpoint.img");
    EXPECT_TRUE(store_->Commit(request).status.IsError());
}

TEST_F(LocalSnapshotStoreTest, DeletesByExactSnapshotIDAndIsIdempotent)
{
    Commit("checkpoint-a", "payload");
    EXPECT_TRUE(store_->Delete({"checkpoint-a"}).IsOk());
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a"));
    EXPECT_TRUE(store_->Delete({"checkpoint-a"}).IsOk());
}

TEST_F(LocalSnapshotStoreTest, ByteBudgetEvictsLeastRecentlyUsedArtifact)
{
    store_ = std::make_unique<LocalSnapshotStore>(root_, 8);
    Commit("checkpoint-a", "12345");
    Commit("checkpoint-b", "67890");
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a"));
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-b" / "checkpoint.img"));
}

TEST_F(LocalSnapshotStoreTest, RestorePinProtectsArtifactUntilLastRelease)
{
    store_ = std::make_unique<LocalSnapshotStore>(root_, 8);
    Commit("checkpoint-a", "12345");
    ASSERT_TRUE(store_->PinForRestore("checkpoint-a").IsOk());
    Commit("checkpoint-b", "67890");

    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-a" / "checkpoint.img"));
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-b" / "checkpoint.img"));

    ASSERT_TRUE(store_->UnpinAfterRestore("checkpoint-a", false).IsOk());
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a"));
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-b" / "checkpoint.img"));
}

TEST_F(LocalSnapshotStoreTest, DistributedOnlyEvictionWaitsForAllRestorePins)
{
    Commit("checkpoint-a", "payload");
    ASSERT_TRUE(store_->PinForRestore("checkpoint-a").IsOk());
    ASSERT_TRUE(store_->PinForRestore("checkpoint-a").IsOk());
    ASSERT_TRUE(store_->EvictLocalArtifact("checkpoint-a").IsOk());
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-a" / "checkpoint.img"));

    ASSERT_TRUE(store_->UnpinAfterRestore("checkpoint-a", true).IsOk());
    EXPECT_TRUE(fs::is_regular_file(root_ / "checkpoint-a" / "checkpoint.img"));
    ASSERT_TRUE(store_->UnpinAfterRestore("checkpoint-a", true).IsOk());
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a"));
    EXPECT_TRUE(store_->List().empty());
}

TEST_F(LocalSnapshotStoreTest, ExplicitDeleteReturnsBusyWhileRestoreIsPinned)
{
    Commit("checkpoint-a", "payload");
    ASSERT_TRUE(store_->PinForRestore("checkpoint-a").IsOk());
    EXPECT_EQ(store_->Delete({"checkpoint-a"}).StatusCode(), StatusCode::ERR_INSTANCE_BUSY);
    ASSERT_TRUE(store_->UnpinAfterRestore("checkpoint-a", false).IsOk());
    EXPECT_TRUE(store_->Delete({"checkpoint-a"}).IsOk());
}

TEST_F(LocalSnapshotStoreTest, InstanceCleanupDeletesOnlyOwnedRecoveryCandidates)
{
    Commit("checkpoint-a", "payload-a");

    auto other = Request("checkpoint-b");
    other.instanceID = "instance-b";
    auto prepared = store_->Prepare(other);
    ASSERT_TRUE(prepared.status.IsOk());
    Write(prepared.directory / "checkpoint.img", "payload-b");
    ASSERT_TRUE(store_->Commit(other).status.IsOk());

    auto reusable = Request("reusable");
    reusable.recoveryCandidate = false;
    prepared = store_->Prepare(reusable);
    ASSERT_TRUE(prepared.status.IsOk());
    Write(prepared.directory / "checkpoint.img", "reusable");
    ASSERT_TRUE(store_->Commit(reusable).status.IsOk());

    ASSERT_TRUE(store_->DeleteRecoveryCandidatesForInstance("instance-a").IsOk());
    EXPECT_FALSE(fs::exists(root_ / "checkpoint-a"));
    EXPECT_TRUE(fs::exists(root_ / "checkpoint-b"));
    EXPECT_TRUE(fs::exists(root_ / "reusable"));
}

TEST_F(LocalSnapshotStoreTest, LruDoesNotKeepPhantomRemoteRecord)
{
    store_ = std::make_unique<LocalSnapshotStore>(root_, 8);
    Commit("checkpoint-a", "12345");
    ASSERT_TRUE(store_->SetStorageLocation(
        "checkpoint-a", "datasystem", "snapshots/checkpoint-a").IsOk());
    Commit("checkpoint-b", "67890");

    ASSERT_EQ(store_->List().size(), 1U);
    EXPECT_EQ(store_->List().front().snapshotID, "checkpoint-b");
}

}  // namespace
}  // namespace functionsystem::function_agent
