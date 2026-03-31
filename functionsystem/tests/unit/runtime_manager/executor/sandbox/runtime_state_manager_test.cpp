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

#include "runtime_manager/executor/sandbox/runtime_state_manager.h"

#include <gtest/gtest.h>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── T10: RuntimeStateManager tests ───────────────────────────────────────────

class RuntimeStateManagerTest : public ::testing::Test {
public:
    void SetUp() override {}
    void TearDown() override {}

    RuntimeStateManager mgr_;
};

// T10-1: Register then Find → SandboxInfo retrieved correctly
TEST_F(RuntimeStateManagerTest, RegisterThenFindReturnsInfo)
{
    const std::string runtimeID = "rt-001";
    SandboxInfo info;
    info.runtimeID = runtimeID;
    info.sandboxID = "sandbox-001";

    mgr_.Register(info);
    auto found = mgr_.Find(runtimeID);

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->runtimeID, runtimeID);
    EXPECT_EQ(found->sandboxID, "sandbox-001");
}

// T10-2: Unregister → Find returns nullopt
TEST_F(RuntimeStateManagerTest, UnregisterThenFindReturnsNullopt)
{
    const std::string runtimeID = "rt-002";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    mgr_.Unregister(runtimeID);
    auto found = mgr_.Find(runtimeID);

    EXPECT_FALSE(found.has_value());
}

// T10-3: Register same runtimeID twice → second replaces first (no duplicate)
TEST_F(RuntimeStateManagerTest, RegisterSameIdTwiceReplacesFirst)
{
    const std::string runtimeID = "rt-003";
    SandboxInfo info1;
    info1.runtimeID = runtimeID;
    info1.sandboxID = "sandbox-A";
    SandboxInfo info2;
    info2.runtimeID = runtimeID;
    info2.sandboxID = "sandbox-B";

    mgr_.Register(info1);
    mgr_.Register(info2);

    auto found = mgr_.Find(runtimeID);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->sandboxID, "sandbox-B");

    // Only one entry should exist (no duplicate)
    auto all = mgr_.GetAllSandboxes();
    EXPECT_EQ(all.count(runtimeID), 1u);
}

// T10-4: UpdateSandboxID after Register → GetSandboxID returns new value
TEST_F(RuntimeStateManagerTest, UpdateSandboxIDReturnsNewValue)
{
    const std::string runtimeID = "rt-004";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    mgr_.UpdateSandboxID(runtimeID, "new-sandbox-id");

    EXPECT_EQ(mgr_.GetSandboxID(runtimeID), "new-sandbox-id");
}

// T10-5: HasSandbox false before UpdateSandboxID (sandboxID empty), true after
TEST_F(RuntimeStateManagerTest, HasSandboxFalseBeforeUpdateTrueAfter)
{
    const std::string runtimeID = "rt-005";
    // Register with empty sandboxID
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    // GetSandboxID returns "" → sandboxID is not set yet
    EXPECT_TRUE(mgr_.GetSandboxID(runtimeID).empty());

    mgr_.UpdateSandboxID(runtimeID, "real-sandbox");
    EXPECT_EQ(mgr_.GetSandboxID(runtimeID), "real-sandbox");
}

// T10-6: IsActive: true when registered (sandboxes_ entry present)
TEST_F(RuntimeStateManagerTest, IsActiveTrueWhenRegistered)
{
    const std::string runtimeID = "rt-006";
    EXPECT_FALSE(mgr_.IsActive(runtimeID));

    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});
    EXPECT_TRUE(mgr_.IsActive(runtimeID));

    mgr_.Unregister(runtimeID);
    EXPECT_FALSE(mgr_.IsActive(runtimeID));
}

// T10-7: GetAllSandboxes: returns all registered entries
TEST_F(RuntimeStateManagerTest, GetAllSandboxesReturnsAllEntries)
{
    mgr_.Register(SandboxInfo{"rt-A", {}, {}, {}, {}});
    mgr_.Register(SandboxInfo{"rt-B", {}, {}, {}, {}});
    mgr_.Register(SandboxInfo{"rt-C", {}, {}, {}, {}});

    auto all = mgr_.GetAllSandboxes();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_TRUE(all.count("rt-A") > 0);
    EXPECT_TRUE(all.count("rt-B") > 0);
    EXPECT_TRUE(all.count("rt-C") > 0);
}

// T10-8: GetPortMappingsJson: returns empty string when not set, JSON after UpdatePortMappings
TEST_F(RuntimeStateManagerTest, GetPortMappingsJsonEmptyThenSetJson)
{
    const std::string runtimeID = "rt-008";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    EXPECT_TRUE(mgr_.GetPortMappingsJson(runtimeID).empty());

    const std::string json = R"([{"port":8080,"protocol":"tcp"}])";
    mgr_.UpdatePortMappings(runtimeID, json);
    EXPECT_EQ(mgr_.GetPortMappingsJson(runtimeID), json);
}

// T10-9 (corner case): Unregister non-existent ID → no crash, no state corruption
TEST_F(RuntimeStateManagerTest, UnregisterNonExistentIdNocrash)
{
    mgr_.Register(SandboxInfo{"rt-existing", "sb-1", {}, {}, {}});

    EXPECT_NO_THROW(mgr_.Unregister("rt-nonexistent"));

    // Existing entry must not be affected
    ASSERT_TRUE(mgr_.Find("rt-existing").has_value());
}

// T10-10 (corner case): UpdateSandboxID for unregistered ID → no crash
TEST_F(RuntimeStateManagerTest, UpdateSandboxIdForUnregisteredIdNocrash)
{
    EXPECT_NO_THROW(mgr_.UpdateSandboxID("rt-ghost", "sb-ghost"));
}

// T10-11: MarkPendingDelete + IsPendingDelete flow
TEST_F(RuntimeStateManagerTest, MarkPendingDeleteAndIsPendingDelete)
{
    const std::string runtimeID = "rt-011";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});

    EXPECT_FALSE(mgr_.IsPendingDelete(runtimeID));
    mgr_.MarkPendingDelete(runtimeID);
    EXPECT_TRUE(mgr_.IsPendingDelete(runtimeID));
}

// T10-12: ClearPendingDelete clears the flag
TEST_F(RuntimeStateManagerTest, ClearPendingDeleteClearsFlag)
{
    const std::string runtimeID = "rt-012";
    mgr_.Register(SandboxInfo{runtimeID, {}, {}, {}, {}});
    mgr_.MarkPendingDelete(runtimeID);
    ASSERT_TRUE(mgr_.IsPendingDelete(runtimeID));

    mgr_.ClearPendingDelete(runtimeID);
    EXPECT_FALSE(mgr_.IsPendingDelete(runtimeID));
}

// T10-13: IsWarmUp: false before RegisterWarmUp, true after; false after UnregisterWarmUp
TEST_F(RuntimeStateManagerTest, IsWarmUpLifecycle)
{
    const std::string runtimeID = "rt-013";

    EXPECT_FALSE(mgr_.IsWarmUp(runtimeID));

    runtime::v1::FunctionRuntime proto;
    proto.set_id(runtimeID);
    mgr_.RegisterWarmUp(runtimeID, std::move(proto));
    EXPECT_TRUE(mgr_.IsWarmUp(runtimeID));

    mgr_.UnregisterWarmUp(runtimeID);
    EXPECT_FALSE(mgr_.IsWarmUp(runtimeID));
}

// T10-14 (corner case): GetAllInstanceInfos: empty map initially; correct count after registers
TEST_F(RuntimeStateManagerTest, GetAllInstanceInfosEmptyThenCorrectCount)
{
    // Initially empty
    EXPECT_TRUE(mgr_.GetAllInstanceInfos().empty());

    // Register two entries
    SandboxInfo info1;
    info1.runtimeID = "rt-i1";
    info1.instanceInfo.set_instanceid("ins-1");
    mgr_.Register(info1);

    SandboxInfo info2;
    info2.runtimeID = "rt-i2";
    info2.instanceInfo.set_instanceid("ins-2");
    mgr_.Register(info2);

    auto infos = mgr_.GetAllInstanceInfos();
    EXPECT_EQ(infos.size(), 2u);
}

}  // namespace functionsystem::test
