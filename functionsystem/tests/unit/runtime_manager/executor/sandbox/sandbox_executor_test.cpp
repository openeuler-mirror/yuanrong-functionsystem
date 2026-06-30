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
#include "runtime_manager/port/port_manager.h"

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

TEST_F(SandboxExecutorTest, MakeSuccessStartResponseSetsContainerExecutorType)
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    auto *info = request->mutable_runtimeinstanceinfo();
    info->set_instanceid("instance-container");
    info->set_runtimeid("runtime-container");
    info->set_requestid("request-container");

    SandboxExecutor executor("sandbox-executor-response-test", litebus::AID(),
                             "/tmp/sandbox-executor-response-test-ckpt");
    const auto response = executor.MakeSuccessStartResponse(request, "sandbox-container-id");

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::SUCCESS));
    EXPECT_EQ(response.requestid(), "request-container");
    EXPECT_EQ(response.startruntimeinstanceresponse().runtimeid(), "runtime-container");
    EXPECT_EQ(response.startruntimeinstanceresponse().containerid(), "sandbox-container-id");
    EXPECT_EQ(response.startruntimeinstanceresponse().executortype(),
              static_cast<int32_t>(EXECUTOR_TYPE::CONTAINER));
}

TEST_F(SandboxExecutorTest, CleanupLocalRuntimeStateForOrphanUnregistersKnownSandbox)
{
    SandboxExecutor executor("sandbox-executor-orphan-test", litebus::AID(), "/tmp/sandbox-executor-orphan-test-ckpt");
    const std::string runtimeID = "rt-orphan-local";
    const std::string sandboxID = "sandbox-orphan-local";

    auto &portManager = PortManager::GetInstance();
    portManager.Clear();
    portManager.InitPortResource(35000, 8);
    auto ports = portManager.RequestPorts(runtimeID, 2);
    ASSERT_EQ(ports.size(), 2U);
    ASSERT_FALSE(portManager.GetPort(runtimeID).empty());

    messages::RuntimeInstanceInfo instanceInfo;
    instanceInfo.set_runtimeid(runtimeID);
    instanceInfo.set_instanceid("inst-orphan-local");
    executor.stateManager_.Register(SandboxInfo{runtimeID, sandboxID, "", "", instanceInfo});
    executor.sandboxStatsSnapshots_[runtimeID] = SandboxExecutor::SandboxStatsSnapshot{};
    executor.sandboxStatsPollingRuntimes_.insert(runtimeID);
    executor.sandboxLifecycleStates_[runtimeID] = SandboxExecutor::SandboxLifecycleStatus::RUNNING;
    executor.userInitiatedTerminateRuntimes_.insert(runtimeID);
    executor.sandboxRunningStartTimes_[runtimeID] = std::chrono::steady_clock::now();

    executor.CleanupLocalRuntimeStateForOrphan("req-orphan-local", sandboxID);

    EXPECT_FALSE(executor.stateManager_.IsActive(runtimeID));
    EXPECT_TRUE(executor.stateManager_.FindRuntimeIDBySandboxID(sandboxID).empty());
    EXPECT_TRUE(executor.sandboxStatsSnapshots_.count(runtimeID) == 0);
    EXPECT_TRUE(executor.sandboxStatsPollingRuntimes_.count(runtimeID) == 0);
    EXPECT_TRUE(executor.sandboxLifecycleStates_.count(runtimeID) == 0);
    EXPECT_TRUE(executor.userInitiatedTerminateRuntimes_.count(runtimeID) == 0);
    EXPECT_TRUE(executor.sandboxRunningStartTimes_.count(runtimeID) == 0);
    EXPECT_TRUE(portManager.GetPort(runtimeID).empty());

    portManager.Clear();
    portManager.InitPortResource(500, 2000);
}

TEST_F(SandboxExecutorTest, ReconcileRuntimesRejectsNullRequest)
{
    SandboxExecutor executor("sandbox-executor-test", litebus::AID(), "/tmp/sandbox-executor-test-ckpt");

    const auto response = executor.ReconcileRuntimes(nullptr).Get();

    EXPECT_EQ(response.code(), static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
    EXPECT_EQ(response.message(), "request is null");
}

}  // namespace functionsystem::test
