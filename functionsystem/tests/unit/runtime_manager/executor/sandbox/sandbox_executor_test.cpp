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

#include "runtime_manager/executor/sandbox/sandbox_executor.h"
#include "runtime_manager/executor/sandbox/runtime_state_manager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "async/future.hpp"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── T12: SandboxStartGuard RAII + RuntimeStateManager corner-case tests ───────

class SandboxExecutorTest : public ::testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}

    // Helper: register a runtimeID and return a Future for use by SandboxStartGuard
    litebus::Future<messages::StartInstanceResponse> RegisterAndGetFuture(
        RuntimeStateManager &mgr, const std::string &runtimeID)
    {
        mgr.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});
        litebus::Promise<messages::StartInstanceResponse> promise;
        return promise.GetFuture();
    }

    RuntimeStateManager mgr_;
};

// T12-1 (corner case): SandboxStartGuard destroyed without Commit()
//   → runtimeID is unregistered from mgr (rollback on failure)
TEST_F(SandboxExecutorTest, GuardWithoutCommitUnregistersOnDestruct)
{
    const std::string runtimeID = "rt-guard-1";
    {
        auto future = RegisterAndGetFuture(mgr_, runtimeID);
        ASSERT_TRUE(mgr_.IsActive(runtimeID));

        SandboxStartGuard guard(mgr_, runtimeID, std::move(future));
        // Intentionally do NOT call Commit()
    }  // guard destroyed here → destructor calls Unregister

    EXPECT_FALSE(mgr_.IsActive(runtimeID));
    EXPECT_FALSE(mgr_.Find(runtimeID).has_value());
}

// T12-2 (corner case): SandboxStartGuard destroyed after Commit()
//   → runtimeID remains in mgr (no rollback when committed)
TEST_F(SandboxExecutorTest, GuardAfterCommitKeepsEntryInMgr)
{
    const std::string runtimeID = "rt-guard-2";
    {
        auto future = RegisterAndGetFuture(mgr_, runtimeID);
        ASSERT_TRUE(mgr_.IsActive(runtimeID));

        SandboxStartGuard guard(mgr_, runtimeID, std::move(future));
        guard.Commit();
    }  // guard destroyed here, but committed_ = true → no Unregister called

    EXPECT_TRUE(mgr_.IsActive(runtimeID));
    EXPECT_TRUE(mgr_.Find(runtimeID).has_value());
}

// T12-3 (corner case): Multiple concurrent SandboxStartGuards for different runtimeIDs
//   → each is independent (no cross-interference)
TEST_F(SandboxExecutorTest, MultipleGuardsAreIndependent)
{
    const std::string runtimeID_A = "rt-guard-A";
    const std::string runtimeID_B = "rt-guard-B";

    auto futureA = RegisterAndGetFuture(mgr_, runtimeID_A);
    auto futureB = RegisterAndGetFuture(mgr_, runtimeID_B);

    {
        SandboxStartGuard guardA(mgr_, runtimeID_A, std::move(futureA));
        SandboxStartGuard guardB(mgr_, runtimeID_B, std::move(futureB));
        // Commit only B
        guardB.Commit();
        // guardA is NOT committed → will unregister on destruct
    }

    EXPECT_FALSE(mgr_.IsActive(runtimeID_A));  // rolled back
    EXPECT_TRUE(mgr_.IsActive(runtimeID_B));   // committed, stays registered
}

// T12-4: Port mappings JSON: empty string → GetPortMappingsJson returns empty
TEST_F(SandboxExecutorTest, EmptyPortMappingsJsonReturnsEmpty)
{
    const std::string runtimeID = "rt-pm-4";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    EXPECT_TRUE(mgr_.GetPortMappingsJson(runtimeID).empty());
}

// T12-5: UpdatePortMappings with valid JSON → GetPortMappingsJson returns that JSON
TEST_F(SandboxExecutorTest, ValidPortMappingsJsonRoundTrip)
{
    const std::string runtimeID = "rt-pm-5";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    const std::string json = R"([{"containerPort":8080,"protocol":"tcp"}])";
    mgr_.UpdatePortMappings(runtimeID, json);

    EXPECT_EQ(mgr_.GetPortMappingsJson(runtimeID), json);
}

// T12-6: UpdatePortMappings with arbitrary string (invalid JSON) → stored as-is, no exception
TEST_F(SandboxExecutorTest, InvalidPortMappingsJsonStoredWithoutException)
{
    const std::string runtimeID = "rt-pm-6";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    const std::string badJson = "not-valid-json";
    EXPECT_NO_THROW(mgr_.UpdatePortMappings(runtimeID, badJson));
    // The manager stores the string as-is (parsing is done by the executor, not the manager)
    EXPECT_EQ(mgr_.GetPortMappingsJson(runtimeID), badJson);
}

// T12-7: GetPortMappingsJson for unregistered runtimeID → returns empty string (no crash)
TEST_F(SandboxExecutorTest, GetPortMappingsJsonForUnregisteredIdReturnsEmpty)
{
    EXPECT_NO_THROW({
        auto result = mgr_.GetPortMappingsJson("rt-not-registered");
        EXPECT_TRUE(result.empty());
    });
}

// T12-8 (corner case): SandboxStartGuard Commit() then guard out of scope
//   → mgr.IsActive returns true (entry stays registered)
TEST_F(SandboxExecutorTest, CommitThenOutOfScopeMgrIsActiveTrue)
{
    const std::string runtimeID = "rt-guard-8";
    {
        auto future = RegisterAndGetFuture(mgr_, runtimeID);
        SandboxStartGuard guard(mgr_, runtimeID, std::move(future));
        guard.Commit();
        // Verify IsActive while guard is still in scope
        EXPECT_TRUE(mgr_.IsActive(runtimeID));
    }  // guard goes out of scope here

    // After scope: still active (committed, not rolled back)
    EXPECT_TRUE(mgr_.IsActive(runtimeID));
}

}  // namespace functionsystem::test
