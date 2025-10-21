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

#include "function_proxy/local_scheduler/resource_group_controller/resource_group_ctrl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "function_proxy/local_scheduler/resource_group_controller/resource_group_ctrl_actor.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {
using namespace testing;
using namespace local_scheduler;
class MockResourceGroupMananger : public litebus::ActorBase {
public:
    MockResourceGroupMananger() : litebus::ActorBase(RESOURCE_GROUP_MANAGER)
    {
    }
    ~MockResourceGroupMananger() = default;

    void ForwardCreateResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        auto rsp = MockForwardCreateResourceGroup();
        Send(from, "OnForwardCreateResourceGroup", rsp.SerializeAsString());
    }
    MOCK_METHOD(CreateResourceGroupResponse, MockForwardCreateResourceGroup, ());

    void ForwardDeleteResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        auto rsp = MockForwardDeleteResourceGroup();
        Send(from, "OnForwardDeleteResourceGroup", rsp.SerializeAsString());
    }
    MOCK_METHOD(inner_service::ForwardKillResponse, MockForwardDeleteResourceGroup, ());

    void Init() override
    {
        Receive("ForwardDeleteResourceGroup", &MockResourceGroupMananger::ForwardDeleteResourceGroup);
        Receive("ForwardCreateResourceGroup", &MockResourceGroupMananger::ForwardCreateResourceGroup);
    }
};

class ResourceGroupCtrlTest : public ::testing::Test {
public:
    void SetUp() override
    {
        rGroupCtrl_ = ResourceGroupCtrl::Init();
        mockResourceGroupMananger_ = std::make_shared<MockResourceGroupMananger>();
        litebus::Spawn(mockResourceGroupMananger_);
        auto leader = explorer::LeaderInfo();
        leader.address = mockResourceGroupMananger_->GetAID().UnfixUrl();
        auto actor = std::dynamic_pointer_cast<ResourceGroupCtrlActor>(rGroupCtrl_->actor_);
        actor->UpdateMasterInfo(leader);
    }

    void TearDown() override
    {
        litebus::Terminate(mockResourceGroupMananger_->GetAID());
        litebus::Terminate(rGroupCtrl_->actor_->GetAID());
        litebus::Await(mockResourceGroupMananger_->GetAID());
        litebus::Await(rGroupCtrl_->actor_->GetAID());
        rGroupCtrl_ = nullptr;
        mockResourceGroupMananger_ = nullptr;
    }

protected:
    std::shared_ptr<MockResourceGroupMananger> mockResourceGroupMananger_;
    std::shared_ptr<ResourceGroupCtrl> rGroupCtrl_;
};

TEST_F(ResourceGroupCtrlTest, Create)
{
    auto req = std::make_shared<CreateResourceGroupRequest>();
    std::string from = "srcInstance";
    req->set_requestid("requestID");
    req->set_traceid("traceID");
    req->mutable_rgroupspec()->set_name("rg");
    req->mutable_rgroupspec()->add_bundles();
    auto readyToReturn = std::make_shared<litebus::Promise<bool>>();
    auto received = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*mockResourceGroupMananger_, MockForwardCreateResourceGroup)
        .WillOnce(Invoke([req, readyToReturn, received]() {
            received->SetValue(true);
            CreateResourceGroupResponse rsp;
            rsp.set_requestid(req->requestid());
            readyToReturn->GetFuture().Wait();
            return rsp;
        }));

    auto future = rGroupCtrl_->Create(from, req);
    auto duplicateFuture = rGroupCtrl_->Create(from, req);
    ASSERT_AWAIT_READY(received->GetFuture());
    readyToReturn->SetValue(true);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get()->code(), common::ERR_NONE);
    ASSERT_AWAIT_READY(duplicateFuture);
    EXPECT_EQ(duplicateFuture.Get()->code(), common::ERR_NONE);
}

TEST_F(ResourceGroupCtrlTest, InvalidOnForwardCreateResourceGroup)
{
    auto actor = std::dynamic_pointer_cast<ResourceGroupCtrlActor>(rGroupCtrl_->actor_);
    std::string method = "OnForwardCreateResourceGroup";
    std::string msg = "*-asdcqw";
    actor->OnForwardCreateResourceGroup(litebus::AID(), std::move(method), std::move(msg));
    EXPECT_EQ(actor->rgMgrAid_->UnfixUrl(), mockResourceGroupMananger_->GetAID().UnfixUrl());
}

TEST_F(ResourceGroupCtrlTest, InvalidOnForwardDeleteResourceGroup)
{
    auto actor = std::dynamic_pointer_cast<ResourceGroupCtrlActor>(rGroupCtrl_->actor_);
    std::string method = "OnForwardDeleteResourceGroup";
    std::string msg = "*-a.;]";
    actor->OnForwardCreateResourceGroup(litebus::AID(), std::move(method), std::move(msg));
    EXPECT_EQ(actor->rgMgrAid_->UnfixUrl(), mockResourceGroupMananger_->GetAID().UnfixUrl());
}

TEST_F(ResourceGroupCtrlTest, Kill)
{
    auto req = std::make_shared<KillRequest>();
    std::string from = "srcInstance";
    req->set_instanceid("rg");
    req->set_signal(8);
    auto readyToReturn = std::make_shared<litebus::Promise<bool>>();
    auto received = std::make_shared<litebus::Promise<bool>>();
    EXPECT_CALL(*mockResourceGroupMananger_, MockForwardDeleteResourceGroup)
        .WillOnce(Invoke([req, readyToReturn, received]() {
            received->SetValue(true);
            inner_service::ForwardKillResponse rsp;
            rsp.set_requestid("rg");
            readyToReturn->GetFuture().Wait();
            return rsp;
        }));

    auto future = rGroupCtrl_->Kill(from, "tenant", req);
    auto duplicateFuture = rGroupCtrl_->Kill(from, "tenant", req);
    ASSERT_AWAIT_READY(received->GetFuture());
    readyToReturn->SetValue(true);
    ASSERT_AWAIT_READY(future);
    EXPECT_EQ(future.Get().code(), common::ERR_NONE);
    ASSERT_AWAIT_READY(duplicateFuture);
    EXPECT_EQ(duplicateFuture.Get().code(), common::ERR_NONE);
}

}  // namespace functionsystem::test