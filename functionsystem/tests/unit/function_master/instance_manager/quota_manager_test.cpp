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

#define private public
#define protected public

#include "common/resource_view/resource_type.h"
#include "common/types/instance_state.h"
#include "function_master/instance_manager/quota_manager/quota_config.h"
#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"
#include "utils/future_test_helper.h"

namespace function_master::test {

using namespace functionsystem::resource_view;
using namespace functionsystem::test;

// ─── Helpers ────────────────────────────────────────────────────────────────

static InstanceInfo MakeInstance(const std::string &instanceID,
                                 const std::string &tenantID,
                                 double cpuMillicores,
                                 double memMb)
{
    InstanceInfo info;
    info.set_instanceid(instanceID);
    info.set_tenantid(tenantID);
    auto &res = (*info.mutable_resources()->mutable_resources());
    {
        auto &cpuVal = res[CPU_RESOURCE_NAME];
        cpuVal.mutable_scalar()->set_value(cpuMillicores);
    }
    {
        auto &memVal = res[MEMORY_RESOURCE_NAME];
        memVal.mutable_scalar()->set_value(memMb);
    }
    return info;
}

// ─── QuotaConfig Tests ──────────────────────────────────────────────────────

TEST(QuotaConfigTest, EmptyPathUsesDefaults)
{
    auto cfg = QuotaConfig::LoadFromFile("");
    EXPECT_FALSE(cfg.IsEnabled());
}

TEST(QuotaConfigTest, UpdateAndGetPerTenantQuota)
{
    auto cfg = QuotaConfig::LoadFromFile("");
    cfg.UpdateTenantQuota("tenant1", TenantQuota{1000, 2048, 5000});
    auto q = cfg.GetQuota("tenant1");
    EXPECT_EQ(q.cpuMillicores, 1000);
    EXPECT_EQ(q.memLimitMb, 2048);
    EXPECT_EQ(q.cooldownMs, 5000);
}

TEST(QuotaConfigTest, UnknownTenantFallsBackToDefault)
{
    auto cfg = QuotaConfig::LoadFromFile("");
    cfg.UpdateTenantQuota("tenant1", TenantQuota{1000, 2048, 5000});
    auto q = cfg.GetQuota("unknown");
    EXPECT_EQ(q.cpuMillicores, 32000);
}

TEST(QuotaManagerActorNoQuotaConfigTest, EmptyPathDoesNotEnforceQuota)
{
    auto actor = std::make_shared<QuotaManagerActor>(QuotaConfig::LoadFromFile(""));
    litebus::Spawn(actor);

    for (int i = 0; i < 3; i++) {
        actor->Send(actor->GetAID(),
                    "OnInstanceRunning",
                    MakeInstance("inst" + std::to_string(i), "t1", 20000.0, 40000.0).SerializeAsString());
    }

    ASSERT_AWAIT_TRUE([&actor]() {
        auto it = actor->tenantUsage_.find("t1");
        return it != actor->tenantUsage_.end() && it->second.sortedInstances.size() == 3u;
    });

    auto &usage = actor->tenantUsage_["t1"];
    EXPECT_EQ(usage.sortedInstances.size(), 3u);
    EXPECT_EQ(usage.cpuMillicores, 60000);
    EXPECT_EQ(usage.memMb, 120000);

    litebus::Terminate(actor->GetAID());
    litebus::Await(actor);
}

// ─── QuotaManagerActor Usage Tracking Tests ─────────────────────────────────

class QuotaManagerActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        QuotaConfig cfg = QuotaConfig::LoadFromFile("");
        // Use small quota to trigger enforcement: cpu=500, mem=1024
        cfg.UpdateTenantQuota("t1", TenantQuota{500, 1024, 5000});
        actor_ = std::make_shared<QuotaManagerActor>(std::move(cfg));
        litebus::Spawn(actor_);
    }

    void TearDown() override
    {
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_);
    }

    void SendInstanceRunning(const InstanceInfo &inst)
    {
        litebus::AID from;
        actor_->Send(actor_->GetAID(), "OnInstanceRunning", inst.SerializeAsString());
    }

    void SendInstanceExited(const InstanceInfo &inst)
    {
        litebus::AID from;
        actor_->Send(actor_->GetAID(), "OnInstanceExited", inst.SerializeAsString());
    }

protected:
    std::shared_ptr<QuotaManagerActor> actor_;
};

TEST_F(QuotaManagerActorTest, OnInstanceRunningAddsUsage)
{
    auto inst = MakeInstance("inst1", "t1", 100.0, 200.0);
    SendInstanceRunning(inst);

    ASSERT_AWAIT_TRUE([this]() {
        auto it = actor_->tenantUsage_.find("t1");
        return it != actor_->tenantUsage_.end() && it->second.cpuMillicores == 100;
    });

    auto &usage = actor_->tenantUsage_["t1"];
    EXPECT_EQ(usage.cpuMillicores, 100);
    EXPECT_EQ(usage.memMb, 200);
    EXPECT_EQ(usage.sortedInstances.size(), 1u);
}

TEST_F(QuotaManagerActorTest, OnInstanceExitedReducesUsage)
{
    auto inst = MakeInstance("inst1", "t1", 100.0, 200.0);
    SendInstanceRunning(inst);
    ASSERT_AWAIT_TRUE([this]() {
        auto it = actor_->tenantUsage_.find("t1");
        return it != actor_->tenantUsage_.end() && it->second.cpuMillicores == 100;
    });

    SendInstanceExited(inst);
    ASSERT_AWAIT_TRUE([this]() {
        auto it = actor_->tenantUsage_.find("t1");
        return it != actor_->tenantUsage_.end() && it->second.cpuMillicores == 0;
    });

    auto &usage = actor_->tenantUsage_["t1"];
    EXPECT_EQ(usage.cpuMillicores, 0);
    EXPECT_EQ(usage.memMb, 0);
    EXPECT_EQ(usage.sortedInstances.size(), 0u);
}

TEST_F(QuotaManagerActorTest, SystemTenantIsIgnored)
{
    auto inst = MakeInstance("inst1", "", 100.0, 200.0);  // empty tenantID = system
    SendInstanceRunning(inst);

    // Give the actor a moment and verify nothing was added
    usleep(50000);
    EXPECT_TRUE(actor_->tenantUsage_.empty());
}

TEST_F(QuotaManagerActorTest, TenantIdZeroIsIgnored)
{
    auto inst = MakeInstance("inst1", "0", 100.0, 200.0);
    SendInstanceRunning(inst);

    usleep(50000);
    EXPECT_TRUE(actor_->tenantUsage_.empty());
}

TEST_F(QuotaManagerActorTest, CheckAndEnforceEvictsWhenOverCpuQuota)
{
    // Quota for t1: cpu=500. Add 3 instances of 200 each → 600 > 500.
    // instanceMgrAID not set → kill Send is silently dropped.
    // After enforcement, sorted instances should shrink.
    for (int i = 0; i < 3; i++) {
        SendInstanceRunning(MakeInstance("inst" + std::to_string(i), "t1", 200.0, 100.0));
    }

    // Wait until all 3 are processed and enforcement runs (evicts 1)
    ASSERT_AWAIT_TRUE([this]() {
        auto it = actor_->tenantUsage_.find("t1");
        // After enforcement: 3*200=600, evict 1 → 400 <= 500 → 2 remain
        return it != actor_->tenantUsage_.end() && it->second.sortedInstances.size() == 2u;
    });

    auto &usage = actor_->tenantUsage_["t1"];
    EXPECT_EQ(usage.sortedInstances.size(), 2u);
    EXPECT_LE(usage.cpuMillicores, 500);
}

TEST_F(QuotaManagerActorTest, MultipleInstancesTrackedPerTenant)
{
    for (int i = 0; i < 3; i++) {
        SendInstanceRunning(MakeInstance("inst" + std::to_string(i), "t2", 10.0, 20.0));
    }

    ASSERT_AWAIT_TRUE([this]() {
        auto it = actor_->tenantUsage_.find("t2");
        return it != actor_->tenantUsage_.end() && it->second.sortedInstances.size() == 3u;
    });

    auto &usage = actor_->tenantUsage_["t2"];
    EXPECT_EQ(usage.cpuMillicores, 30);
    EXPECT_EQ(usage.memMb, 60);
}

}  // namespace function_master::test
