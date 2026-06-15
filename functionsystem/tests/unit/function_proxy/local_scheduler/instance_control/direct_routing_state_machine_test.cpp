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

#include "function_proxy/common/state_machine/instance_control_view.h"

#include <gtest/gtest.h>

#include "function_proxy/config/direct_routing_config.h"

namespace functionsystem::test {
namespace {

std::shared_ptr<messages::ScheduleRequest> MakeScheduleRequest(const std::string &requestID,
                                                               const std::string &instanceID)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid(requestID);
    req->set_traceid("trace-" + requestID);
    req->mutable_instance()->set_instanceid(instanceID);
    req->mutable_instance()->set_functionproxyid("node-a");
    req->mutable_instance()->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::SCHEDULING));
    return req;
}

class DirectRoutingStateMachineTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        function_proxy::DirectRoutingConfig::SetEnabled(true);
    }

    void TearDown() override
    {
        function_proxy::DirectRoutingConfig::SetEnabled(false);
    }
};

}  // namespace

TEST_F(DirectRoutingStateMachineTest, ScheduleFailureRollbackDeletesLocalMachine)
{
    InstanceControlView view("node-a", true);
    auto req = MakeScheduleRequest("req-1", "inst-1");

    auto gen = view.TryGenerateNewInstance(req);
    ASSERT_FALSE(gen.instanceID.empty());
    ASSERT_NE(nullptr, view.GetInstance("inst-1"));

    view.RollbackDirectRoutingScheduleFailure("inst-1", "req-1");

    EXPECT_EQ(nullptr, view.GetInstance("inst-1"));
    EXPECT_TRUE(view.TryGetInstanceIDByReq("req-1").empty());
}

TEST_F(DirectRoutingStateMachineTest, RollbackSkipsNewerRequest)
{
    InstanceControlView view("node-a", true);
    auto req = MakeScheduleRequest("req-new", "inst-1");

    auto gen = view.TryGenerateNewInstance(req);
    ASSERT_FALSE(gen.instanceID.empty());
    ASSERT_NE(nullptr, view.GetInstance("inst-1"));

    view.RollbackDirectRoutingScheduleFailure("inst-1", "req-old");

    EXPECT_NE(nullptr, view.GetInstance("inst-1"));
    EXPECT_EQ("inst-1", view.TryGetInstanceIDByReq("req-new"));
}

TEST_F(DirectRoutingStateMachineTest, StaleFailureDoesNotReleaseOrDeleteNewerRequest)
{
    InstanceControlView view("node-a", true);
    auto req = MakeScheduleRequest("req-new", "inst-1");

    auto gen = view.TryGenerateNewInstance(req);
    ASSERT_FALSE(gen.instanceID.empty());
    auto stateMachine = view.GetInstance("inst-1");
    ASSERT_NE(nullptr, stateMachine);
    ASSERT_EQ("req-new", stateMachine->GetRequestID());
    ASSERT_EQ("node-a", stateMachine->GetOwner());

    EXPECT_FALSE(view.ReleaseSchedulingOwnerIfRequestMatches("inst-1", "req-old"));
    view.RollbackDirectRoutingScheduleFailure("inst-1", "req-old");

    stateMachine = view.GetInstance("inst-1");
    ASSERT_NE(nullptr, stateMachine);
    EXPECT_EQ("req-new", stateMachine->GetRequestID());
    EXPECT_EQ("node-a", stateMachine->GetOwner());
}

}  // namespace functionsystem::test
