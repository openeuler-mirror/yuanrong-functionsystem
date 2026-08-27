/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 * See the LICENSE file in this repository for the complete license text.
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "common/proto/pb/message_pb.h"
#include "function_agent/snapshot/local_snapshot_store.h"

namespace functionsystem::function_agent {
namespace {

namespace fs = std::filesystem;

TEST(LocalSnapshotProtoTest, UsesStableInternalFieldNumbers)
{
    const auto *anonymous = messages::SnapshotRuntimeRequest::descriptor()->FindFieldByName("anonymous");
    ASSERT_NE(anonymous, nullptr);
    EXPECT_EQ(anonymous->number(), 13);

    const auto *localSnapshot = messages::SnapshotRuntimeResponse::descriptor()->FindFieldByName("localSnapshot");
    ASSERT_NE(localSnapshot, nullptr);
    EXPECT_EQ(localSnapshot->number(), 10);

    const auto *snapshots = messages::ListLocalSnapshotsResponse::descriptor()->FindFieldByName("snapshots");
    ASSERT_NE(snapshots, nullptr);
    EXPECT_EQ(snapshots->number(), 4);
}

TEST(LocalRestoreProtoTest, UsesStableRestoreSnapshotFields)
{
    const auto *deploy = messages::DeployInstanceRequest::descriptor()->FindFieldByName("restoreSnapshotID");
    ASSERT_NE(deploy, nullptr);
    EXPECT_EQ(deploy->number(), 42);

    const auto *runtime = messages::RuntimeInstanceInfo::descriptor()->FindFieldByName("restoreSnapshotID");
    ASSERT_NE(runtime, nullptr);
    EXPECT_EQ(runtime->number(), 14);
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

    static void WriteCheckpoint(const fs::path &path, const std::string &contents)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output << contents;
        output.close();
        ASSERT_TRUE(output.good());
    }

    static std::string ReadFile(const fs::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        EXPECT_TRUE(input.is_open());
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    static LocalSnapshotCommitRequest MakeRequest(const std::string &snapshotID,
                                                  const std::string &instanceID = "sandbox-a")
    {
        LocalSnapshotCommitRequest request;
        request.snapshotID = snapshotID;
        request.anonymous = true;
        request.instanceID = instanceID;
        request.tenantHash = "tenant-hash";
        request.sourceRuntimeID = "runtime-a";
        request.sourceSandboxID = "sbox-runtime-a";
        request.sourceInstanceVersion = 12;
        request.runtimeClass = "runsc";
        request.architecture = "x86_64";
        request.createdAtUnixSeconds = 1787670000;
        return request;
    }

    LocalSnapshotDescriptor CommitSnapshot(const std::string &snapshotID,
                                           const std::string &contents)
    {
        const auto request = MakeRequest(snapshotID);
        const auto prepared = store_->Prepare(request);
        EXPECT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
        WriteCheckpoint(prepared.directory / "checkpoint.img", contents);
        const auto committed = store_->Commit(request);
        EXPECT_TRUE(committed.status.IsOk()) << committed.status.ToString();
        return committed.descriptor;
    }

    fs::path root_;
    std::unique_ptr<LocalSnapshotStore> store_;
};

TEST_F(LocalSnapshotStoreTest, CommitsAndListsFlatSnapshot)
{
    const auto request = MakeRequest("anon-1");
    const auto prepared = store_->Prepare(request);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    EXPECT_EQ(prepared.directory, root_ / "anon-1");
    WriteCheckpoint(prepared.directory / "checkpoint.img", "checkpoint-state");

    const auto committed = store_->Commit(request);

    ASSERT_TRUE(committed.status.IsOk()) << committed.status.ToString();
    EXPECT_EQ(committed.descriptor.snapshotID, "anon-1");
    EXPECT_TRUE(committed.descriptor.anonymous);
    EXPECT_EQ(committed.descriptor.generation, 1U);
    EXPECT_EQ(committed.descriptor.size, std::string("checkpoint-state").size());
    EXPECT_EQ(committed.descriptor.sha256.size(), 64U);
    ASSERT_EQ(store_->List().size(), 1U);
    EXPECT_TRUE(fs::is_regular_file(root_ / "anon-1" / "snapshot.meta"));
}

TEST_F(LocalSnapshotStoreTest, IgnoresDirectoryWithoutCommittedMeta)
{
    const auto request = MakeRequest("partial");
    const auto prepared = store_->Prepare(request);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    WriteCheckpoint(prepared.directory / "checkpoint.img", "partial");

    EXPECT_TRUE(store_->List().empty());
}

TEST_F(LocalSnapshotStoreTest, ExactRetryCanReplaceIncompleteArtifact)
{
    const auto request = MakeRequest("partial-retry");
    const auto prepared = store_->Prepare(request);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    WriteCheckpoint(prepared.directory / "checkpoint.img", "partial");

    const auto retry = store_->Prepare(request);

    EXPECT_TRUE(retry.status.IsOk()) << retry.status.ToString();
    EXPECT_FALSE(retry.replayed);
    EXPECT_EQ(retry.directory, prepared.directory);
}

TEST_F(LocalSnapshotStoreTest, RefusesDeleteWhenDigestDoesNotMatch)
{
    const auto descriptor = CommitSnapshot("anon-2", "payload");
    LocalSnapshotDeleteIdentity identity;
    identity.snapshotID = descriptor.snapshotID;
    identity.expectedGeneration = descriptor.generation;
    identity.expectedSize = descriptor.size;
    identity.expectedSha256 = std::string(64, '0');

    const auto status = store_->Delete(identity);

    EXPECT_EQ(status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_TRUE(fs::is_regular_file(root_ / "anon-2" / "checkpoint.img"));
    EXPECT_TRUE(fs::is_regular_file(root_ / "anon-2" / "snapshot.meta"));
}

TEST_F(LocalSnapshotStoreTest, SelectsNextAnonymousGenerationAfterRestart)
{
    EXPECT_EQ(CommitSnapshot("anon-1", "first").generation, 1U);
    store_ = std::make_unique<LocalSnapshotStore>(root_);

    EXPECT_EQ(CommitSnapshot("anon-2", "second").generation, 2U);
}

TEST_F(LocalSnapshotStoreTest, ReplaysMatchingCommittedSnapshot)
{
    const auto request = MakeRequest("anon-replay");
    const auto descriptor = CommitSnapshot(request.snapshotID, "payload");

    const auto prepared = store_->Prepare(request);
    const auto replayed = store_->Commit(request);

    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    EXPECT_TRUE(prepared.replayed);
    ASSERT_TRUE(replayed.status.IsOk()) << replayed.status.ToString();
    EXPECT_EQ(replayed.descriptor.generation, descriptor.generation);
    EXPECT_EQ(replayed.descriptor.sha256, descriptor.sha256);
}

TEST_F(LocalSnapshotStoreTest, ReusesNonAnonymousSnapshotAcrossTargetInstances)
{
    auto source = MakeRequest("reusable-local", "source-instance");
    source.anonymous = false;
    const auto prepared = store_->Prepare(source);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    WriteCheckpoint(prepared.directory / "checkpoint.img", "payload");
    ASSERT_TRUE(store_->Commit(source).status.IsOk());

    auto target = MakeRequest("reusable-local", "target-instance");
    target.anonymous = false;
    const auto replay = store_->Prepare(target);

    ASSERT_TRUE(replay.status.IsOk()) << replay.status.ToString();
    EXPECT_TRUE(replay.replayed);
    const auto committed = store_->Commit(target);
    ASSERT_TRUE(committed.status.IsOk()) << committed.status.ToString();
    EXPECT_EQ(committed.descriptor.instanceID, "source-instance");
    EXPECT_EQ(committed.descriptor.generation, 0U);
}

TEST_F(LocalSnapshotStoreTest, RejectsCorruptedCommittedSnapshotReplay)
{
    const auto request = MakeRequest("anon-corrupt-replay");
    CommitSnapshot(request.snapshotID, "payload");
    WriteCheckpoint(root_ / request.snapshotID / "checkpoint.img", "PAYLOAD");

    EXPECT_EQ(store_->Prepare(request).status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_EQ(store_->Commit(request).status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
}

TEST_F(LocalSnapshotStoreTest, RejectsConflictingCommittedSnapshot)
{
    CommitSnapshot("anon-conflict", "payload");
    auto conflicting = MakeRequest("anon-conflict");
    conflicting.sourceRuntimeID = "runtime-b";

    EXPECT_EQ(store_->Prepare(conflicting).status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_EQ(store_->Commit(conflicting).status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
}

TEST_F(LocalSnapshotStoreTest, RejectsSymlinkedImageAndMeta)
{
    const auto imageRequest = MakeRequest("linked-image");
    const auto prepared = store_->Prepare(imageRequest);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    WriteCheckpoint(root_ / "outside.img", "payload");
    fs::create_symlink(root_ / "outside.img", prepared.directory / "checkpoint.img");
    EXPECT_TRUE(store_->Commit(imageRequest).status.IsError());

    CommitSnapshot("linked-meta", "payload");
    const auto metadata = ReadFile(root_ / "linked-meta" / "snapshot.meta");
    WriteCheckpoint(root_ / "outside.meta", metadata);
    ASSERT_TRUE(fs::remove(root_ / "linked-meta" / "snapshot.meta"));
    fs::create_symlink(root_ / "outside.meta", root_ / "linked-meta" / "snapshot.meta");
    EXPECT_TRUE(store_->List().empty());
    LocalSnapshotDescriptor descriptor;
    EXPECT_TRUE(store_->ValidateForRestore("linked-meta", descriptor).IsError());
}

TEST_F(LocalSnapshotStoreTest, ValidateForRestoreChecksShaAndCompatibility)
{
    const auto digestDescriptor = CommitSnapshot("digest", "payload-a");
    WriteCheckpoint(root_ / digestDescriptor.snapshotID / "checkpoint.img", "payload-b");
    LocalSnapshotDescriptor restored;
    EXPECT_EQ(store_->ValidateForRestore(digestDescriptor.snapshotID, restored).StatusCode(),
              StatusCode::SCHEDULE_CONFLICTED);

    auto incompatible = MakeRequest("incompatible");
    incompatible.artifactFormat = "unknown-format";
    const auto prepared = store_->Prepare(incompatible);
    ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
    WriteCheckpoint(prepared.directory / "checkpoint.img", "payload");
    ASSERT_TRUE(store_->Commit(incompatible).status.IsOk());
    EXPECT_EQ(store_->ValidateForRestore(incompatible.snapshotID, restored).StatusCode(),
              StatusCode::SCHEDULE_CONFLICTED);
}

TEST_F(LocalSnapshotStoreTest, DeleteMissingSnapshotIsIdempotent)
{
    LocalSnapshotDeleteIdentity identity;
    identity.snapshotID = "missing";
    identity.expectedGeneration = 7;
    identity.expectedSize = 42;
    identity.expectedSha256 = std::string(64, 'a');

    EXPECT_TRUE(store_->Delete(identity).IsOk());
}

}  // namespace
}  // namespace functionsystem::function_agent
