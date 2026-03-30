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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/utils/tenant_cooldown_manager.h"

namespace functionsystem::test {

class TenantCooldownManagerTest : public ::testing::Test {
protected:
    TenantCooldownManager manager;
};

TEST_F(TenantCooldownManagerTest, ApplyBlocksTenant)
{
    bool timerCalled = false;
    manager.Apply("tenant1", [&](uint64_t gen) {
        timerCalled = true;
        return litebus::Timer{};
    });

    EXPECT_TRUE(timerCalled);
    EXPECT_TRUE(manager.IsBlocked("tenant1"));
}

TEST_F(TenantCooldownManagerTest, OnExpiredUnblocks)
{
    uint64_t capturedGen = 0;
    manager.Apply("tenant1", [&](uint64_t gen) {
        capturedGen = gen;
        return litebus::Timer{};
    });

    EXPECT_TRUE(manager.IsBlocked("tenant1"));
    manager.OnExpired("tenant1", capturedGen);
    EXPECT_FALSE(manager.IsBlocked("tenant1"));
}

TEST_F(TenantCooldownManagerTest, OnExpiredStaleGenerationIgnored)
{
    manager.Apply("tenant1", [&](uint64_t gen) {
        return litebus::Timer{};
    });

    EXPECT_TRUE(manager.IsBlocked("tenant1"));

    manager.OnExpired("tenant1", 999);

    EXPECT_TRUE(manager.IsBlocked("tenant1"));
}

TEST_F(TenantCooldownManagerTest, ApplyEmptyTenantIsNoop)
{
    bool timerCalled = false;
    manager.Apply("", [&](uint64_t gen) {
        timerCalled = true;
        return litebus::Timer{};
    });

    EXPECT_FALSE(timerCalled);
    EXPECT_FALSE(manager.IsBlocked(""));
}

TEST_F(TenantCooldownManagerTest, ReapplyResetsGeneration)
{
    uint64_t gen1 = 0;
    uint64_t gen2 = 0;

    manager.Apply("tenant1", [&](uint64_t gen) {
        gen1 = gen;
        return litebus::Timer{};
    });

    EXPECT_TRUE(manager.IsBlocked("tenant1"));

    manager.Apply("tenant1", [&](uint64_t gen) {
        gen2 = gen;
        return litebus::Timer{};
    });

    EXPECT_TRUE(manager.IsBlocked("tenant1"));
    EXPECT_EQ(gen2, gen1 + 1);

    manager.OnExpired("tenant1", gen1);
    EXPECT_TRUE(manager.IsBlocked("tenant1"));

    manager.OnExpired("tenant1", gen2);
    EXPECT_FALSE(manager.IsBlocked("tenant1"));
}

TEST_F(TenantCooldownManagerTest, CancelAllClearsAllEntries)
{
    manager.Apply("tenant1", [&](uint64_t gen) { return litebus::Timer{}; });
    manager.Apply("tenant2", [&](uint64_t gen) { return litebus::Timer{}; });
    manager.Apply("tenant3", [&](uint64_t gen) { return litebus::Timer{}; });

    EXPECT_TRUE(manager.IsBlocked("tenant1"));
    EXPECT_TRUE(manager.IsBlocked("tenant2"));
    EXPECT_TRUE(manager.IsBlocked("tenant3"));

    manager.CancelAll();

    EXPECT_FALSE(manager.IsBlocked("tenant1"));
    EXPECT_FALSE(manager.IsBlocked("tenant2"));
    EXPECT_FALSE(manager.IsBlocked("tenant3"));
}

}  // namespace functionsystem::test
