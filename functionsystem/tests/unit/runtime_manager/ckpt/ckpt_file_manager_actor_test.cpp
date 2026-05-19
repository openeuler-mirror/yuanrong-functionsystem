/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "async/async.hpp"
#include "runtime_manager/ckpt/ckpt_file_manager_actor.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using functionsystem::runtime_manager::CkptFileManagerActor;

class CkptFileManagerActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        // Use a temp path unique per test run; actor's Init() will create it.
        tmpDir_ = std::string("/tmp/ckpt_test_") +
                  litebus::uuid_generator::UUID::GetRandomUUID().ToString();

        std::string name = "CkptFileMgr_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        actor_ = std::make_shared<CkptFileManagerActor>(name, tmpDir_);

        // Disable the periodic cleanup timer so it does not interfere with tests.
        actor_->cleanupEnabled_ = false;

        litebus::Spawn(actor_);

        // Sync with the actor to guarantee Init() has finished before the test body runs.
        litebus::Async(actor_->GetAID(), &CkptFileManagerActor::CleanupExpiredFiles).Get();
    }

    void TearDown() override
    {
        if (actor_) {
            litebus::Terminate(actor_->GetAID());
            litebus::Await(actor_->GetAID());
        }
        actor_ = nullptr;
        std::filesystem::remove_all(tmpDir_);
    }

protected:
    std::string tmpDir_;
    std::shared_ptr<CkptFileManagerActor> actor_{ nullptr };
};

// Verify that Init() creates the configured checkpoint directory even when
// it does not exist prior to spawning the actor.
TEST_F(CkptFileManagerActorTest, CheckpointDirCreatedByInit)
{
    EXPECT_TRUE(std::filesystem::exists(tmpDir_));
    EXPECT_TRUE(std::filesystem::is_directory(tmpDir_));
}

// Verify that checkpoint entries already present on disk when the actor
// starts are loaded into memory and become addressable by their directory names.
TEST_F(CkptFileManagerActorTest, CheckpointRestoredFromPrePopulatedDir)
{
    const std::string ckptId = "ckpt_restore_test";
    std::filesystem::create_directories(tmpDir_ + "/" + ckptId);

    // Re-trigger the restore scan so it picks up the newly created directory.
    // RestoreCheckpointsFromLocal returns void; fire-and-forget then rely on FIFO
    // actor ordering: AddReference will only execute after the restore completes.
    litebus::Async(actor_->GetAID(), &CkptFileManagerActor::RestoreCheckpointsFromLocal);

    // AddReference succeeds only when the checkpoint is known to the actor.
    auto status = litebus::Async(actor_->GetAID(), &CkptFileManagerActor::AddReference, ckptId).Get();
    EXPECT_TRUE(status.IsOk());
}

// Verify that AddReference returns an error for an unknown checkpoint ID.
TEST_F(CkptFileManagerActorTest, AddReferenceFailsForUnknownCheckpoint)
{
    auto status = litebus::Async(
        actor_->GetAID(), &CkptFileManagerActor::AddReference, std::string("no_such_ckpt")).Get();
    EXPECT_FALSE(status.IsOk());
}

// Verify that RemoveReference is a no-op (returns OK) for an unknown checkpoint,
// which can happen when the checkpoint was already deleted.
TEST_F(CkptFileManagerActorTest, RemoveReferenceSucceedsForUnknownCheckpoint)
{
    auto status = litebus::Async(
        actor_->GetAID(), &CkptFileManagerActor::RemoveReference, std::string("no_such_ckpt")).Get();
    EXPECT_TRUE(status.IsOk());
}

}  // namespace functionsystem::test
