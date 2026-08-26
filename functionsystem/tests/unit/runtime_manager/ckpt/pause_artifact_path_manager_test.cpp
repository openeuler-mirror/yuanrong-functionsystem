/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "runtime_manager/ckpt/pause_artifact_path_manager.h"

namespace functionsystem::runtime_manager {
namespace {

namespace fs = std::filesystem;

class PauseArtifactPathManagerTest : public testing::Test {
protected:
    void SetUp() override
    {
        auto pattern = (fs::temp_directory_path() / "pause-artifact-test-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        root = mkdtemp(buffer.data());
        manager = std::make_unique<PauseArtifactPathManager>(root, "tenant-hash", "instance-1");
    }

    void TearDown() override
    {
        manager.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path root;
    std::unique_ptr<PauseArtifactPathManager> manager;
};

TEST_F(PauseArtifactPathManagerTest, ReturnsExactSourceAndAttemptPathsWithoutCreatingLocalState)
{
    const auto source = manager->PlanSourceArtifact("snapshot-1");
    const auto attempt = manager->PlanRestoreAttempt("snapshot-1", "attempt-9");

    ASSERT_TRUE(source.status.IsOk()) << source.status.ToString();
    EXPECT_EQ(source.path, root / "pause/tenant-hash/instance-1/snapshot-1/checkpoint.img");
    ASSERT_TRUE(attempt.status.IsOk()) << attempt.status.ToString();
    EXPECT_EQ(attempt.path,
              root / "restore/tenant-hash/instance-1/snapshot-1/attempts/attempt-9/checkpoint.img");
    EXPECT_FALSE(fs::exists(root / "pause"));
    EXPECT_FALSE(fs::exists(root / "restore"));
}

TEST_F(PauseArtifactPathManagerTest, DifferentSnapshotsDoNotUseNodeLocalReservation)
{
    const auto first = manager->PlanSourceArtifact("snapshot-1");
    const auto second = manager->PlanSourceArtifact("snapshot-2");

    ASSERT_TRUE(first.status.IsOk()) << first.status.ToString();
    ASSERT_TRUE(second.status.IsOk()) << second.status.ToString();
    EXPECT_NE(first.path, second.path);
    EXPECT_FALSE(fs::exists(root / "pause/tenant-hash/instance-1/.snapshot-owner"));
    EXPECT_FALSE(fs::exists(root / "pause"));
}

TEST_F(PauseArtifactPathManagerTest, PathPlanningRejectsTraversalWithoutConsultingLocalDisk)
{
    EXPECT_FALSE(manager->PlanSourceArtifact("../escape").status.IsOk());
    EXPECT_FALSE(PauseArtifactPathManager(root, "tenant/escape", "instance-1")
                     .PlanSourceArtifact("snapshot-1").status.IsOk());

    fs::create_directories(root / "pause/tenant-hash");
    fs::create_directories(root / "outside");
    fs::create_directory_symlink(root / "outside", root / "pause/tenant-hash/instance-1");
    const auto planned = manager->PlanSourceArtifact("snapshot-1");
    EXPECT_TRUE(planned.status.IsOk()) << planned.status.ToString();
    EXPECT_EQ(planned.path, root / "pause/tenant-hash/instance-1/snapshot-1/checkpoint.img");
}

TEST_F(PauseArtifactPathManagerTest, DeleteRestoreAttemptRejectsSymlinkedCacheTree)
{
    fs::create_directories(root / "outside/attempt-a");
    std::ofstream(root / "outside/attempt-a/checkpoint.img") << "outside";
    fs::create_directories(root / "restore/tenant-hash/instance-1/snapshot-1");
    fs::create_directory_symlink(
        root / "outside", root / "restore/tenant-hash/instance-1/snapshot-1/attempts");

    EXPECT_FALSE(manager->DeleteRestoreAttempt("snapshot-1", "attempt-a").Get().IsOk());
    EXPECT_TRUE(fs::is_regular_file(root / "outside/attempt-a/checkpoint.img"));
}

TEST_F(PauseArtifactPathManagerTest, RestoreAttemptPathRemainsStableAndScoped)
{
    EXPECT_EQ(PauseArtifactPathManager::StableTenantHash("tenant-r0"),
              "a0087d412d4000415c8dac253e46fbbc70074f34d3de5ef35599d58ca5273b82");
    const auto attemptA = manager->PlanRestoreAttempt("snapshot-1", "attempt-a");
    const auto attemptB = manager->PlanRestoreAttempt("snapshot-1", "attempt-b");
    ASSERT_TRUE(attemptA.status.IsOk());
    ASSERT_TRUE(attemptB.status.IsOk());
    EXPECT_NE(attemptA.path, attemptB.path);
    PauseArtifactPathManager other(root, "tenant-hash", "instance-2");
    EXPECT_NE(attemptA.path, other.PlanRestoreAttempt("snapshot-1", "attempt-a").path);
    EXPECT_TRUE(manager->PlanRestoreAttempt("snapshot-1", "../escape").status.IsError());
}

TEST_F(PauseArtifactPathManagerTest, DeleteRestoreAttemptOnlyRemovesExactReconstructableCache)
{
    const auto attemptA = manager->PlanRestoreAttempt("snapshot-1", "attempt-a");
    const auto attemptB = manager->PlanRestoreAttempt("snapshot-1", "attempt-b");
    ASSERT_TRUE(attemptA.status.IsOk());
    ASSERT_TRUE(attemptB.status.IsOk());
    fs::create_directories(attemptA.path.parent_path());
    fs::create_directories(attemptB.path.parent_path());
    std::ofstream(attemptA.path) << "attempt-a";
    std::ofstream(attemptB.path) << "attempt-b";

    ASSERT_TRUE(manager->DeleteRestoreAttempt("snapshot-1", "attempt-a").Get().IsOk());
    EXPECT_FALSE(fs::exists(attemptA.path.parent_path()));
    EXPECT_TRUE(fs::is_regular_file(attemptB.path));
    EXPECT_TRUE(manager->DeleteRestoreAttempt("snapshot-1", "attempt-a").Get().IsOk());
}

TEST_F(PauseArtifactPathManagerTest, DeleteRestoreAttemptPrunesEmptySnapshotHierarchy)
{
    const auto attempt = manager->PlanRestoreAttempt("snapshot-1", "attempt-a");
    ASSERT_TRUE(attempt.status.IsOk());
    fs::create_directories(attempt.path.parent_path());
    std::ofstream(attempt.path) << "checkpoint";

    const auto status = manager->DeleteRestoreAttempt("snapshot-1", "attempt-a").Get();

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_FALSE(fs::exists(root / "restore/tenant-hash/instance-1"));
    EXPECT_FALSE(fs::exists(root / "restore/tenant-hash"));
    EXPECT_FALSE(fs::exists(root / "restore"));
}

TEST_F(PauseArtifactPathManagerTest, PruneSourceArtifactParentsRemovesEmptyInstanceHierarchy)
{
    const auto source = manager->PlanSourceArtifact("snapshot-1");
    ASSERT_TRUE(source.status.IsOk());
    fs::create_directories(source.path.parent_path().parent_path());

    const auto status = manager->PruneSourceArtifactParents("snapshot-1").Get();

    ASSERT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_FALSE(fs::exists(root / "pause/tenant-hash/instance-1"));
    EXPECT_FALSE(fs::exists(root / "pause/tenant-hash"));
    EXPECT_FALSE(fs::exists(root / "pause"));
}

}  // namespace
}  // namespace functionsystem::runtime_manager
