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

#ifndef TEST_UNIT_DOMAIN_SCHEDULER_DOMAIN_SCHEDULER_SERVICE_UPLAYER_STUB_H
#define TEST_UNIT_DOMAIN_SCHEDULER_DOMAIN_SCHEDULER_SERVICE_UPLAYER_STUB_H

#include <gmock/gmock.h>

#include <litebus.hpp>

#include "common/constants/actor_name.h"
#include "domain_scheduler/domain_scheduler_service/domain_sched_srv.h"
#include "domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.h"
#include "mocks/mock_domain_instance_ctrl.h"
#include "mocks/mock_domain_underlayer_sched_mgr.h"

namespace functionsystem::domain_scheduler::test {
class UplayerActor : public litebus::ActorBase {
public:
    UplayerActor(const std::string &name) : ActorBase(name)
    {
    }
    ~UplayerActor() = default;

    void SetResponseLeader(const std::string &name, const std::string &address, bool hasLeader = true)
    {
        hasLeader_ = hasLeader;
        leaderName_ = name;
        leaderAddress_ = address;
    }

    void Register(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::Register req;
        ASSERT_EQ(req.ParseFromString(msg), true);
        registeredName_ = req.name();
        registeredAddress_ = req.address();

        messages::Registered rsp;
        rsp.set_code(0);
        if (GetAID().Name() == DOMAIN_SCHED_MGR_ACTOR_NAME) {
            if (hasLeader_) {
                auto leader = rsp.mutable_topo()->mutable_leader();
                leader->set_name(leaderName_);
                leader->set_address(leaderAddress_);
            }
        }
        Send(from, "Registered", rsp.SerializeAsString());
    }
    void UpdateResource(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockUpdateResource(from, name, msg);
    }
    MOCK_METHOD3(MockUpdateResource, void(const litebus::AID &from, std::string &name, std::string &msg));

    void ForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto rsp = MockForwardSchedule(from, name, msg);
        Send(from, "ResponseForwardSchedule", std::move(rsp));
    }
    MOCK_METHOD3(MockForwardSchedule, std::string(const litebus::AID &from, std::string &name, std::string &msg));

    void NotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::NotifySchedAbnormalRequest req;
        ASSERT_EQ(req.ParseFromString(msg), true);
        abnormalName_ = req.schedname();
        messages::NotifySchedAbnormalResponse rsp;
        rsp.set_schedname(abnormalName_);
        Send(from, "ResponseNotifySchedAbnormal", rsp.SerializeAsString());
    }

    void ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::NotifyWorkerStatusRequest req;
        ASSERT_EQ(req.ParseFromString(msg), true);
        messages::NotifyWorkerStatusResponse rsp;
        rsp.set_workerip(req.workerip());
        rsp.set_healthy(req.healthy());
        Send(from, "ResponseNotifyWorkerStatus", rsp.SerializeAsString());
    }

    void ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseSchedule(from, name, msg);
    }
    MOCK_METHOD3(MockResponseSchedule, void(const litebus::AID &from, std::string &name, std::string &msg));

    void ResponseQueryAgentInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseQueryAgentInfo(from, name, msg);
    }
    MOCK_METHOD3(MockResponseQueryAgentInfo, void(const litebus::AID &from, std::string &name, std::string &msg));

    void ResponseQueryResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseQueryResourcesInfo(from, name, msg);
    }
    MOCK_METHOD3(MockResponseQueryResourcesInfo, void(const litebus::AID &from, std::string &name, std::string &msg));

    void ResponseGetSchedulingQueue(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseGetSchedulingQueue(from, name, msg);
    }
    MOCK_METHOD3(MockResponseGetSchedulingQueue, void(const litebus::AID &from, std::string &name, std::string &msg));

    void SendRequest(const litebus::AID &from, std::string name, std::string msg)
    {
        Send(from, std::move(name), std::move(msg));
    }

    void TryCancelResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockTryCancelResponse(from, name, msg);
    }
    MOCK_METHOD(void, MockTryCancelResponse, (const litebus::AID &from, std::string &name, std::string &msg));

    void Init() override
    {
        Receive("Register", &UplayerActor::Register);
        Receive("NotifySchedAbnormal", &UplayerActor::NotifySchedAbnormal);
        Receive("UpdateResources", &UplayerActor::UpdateResource);
        Receive("ForwardSchedule", &UplayerActor::ForwardSchedule);
        Receive("ResponseSchedule", &UplayerActor::ResponseSchedule);
        Receive("ResponseQueryAgentInfo", &UplayerActor::ResponseQueryAgentInfo);
        Receive("ResponseQueryResourcesInfo", &UplayerActor::ResponseQueryResourcesInfo);
        Receive("NotifyWorkerStatus", &UplayerActor::ResponseNotifyWorkerStatus);
        Receive("TryCancelResponse", &UplayerActor::TryCancelResponse);
        Receive("ResponseGetSchedulingQueue", &UplayerActor::ResponseGetSchedulingQueue);
    }

    const std::string GetRegisteredName()
    {
        return registeredName_;
    }
    const std::string GetRegisteredAddress()
    {
        return registeredAddress_;
    }
    const std::string GetAbnormalName()
    {
        return abnormalName_;
    }

private:
    std::string registeredName_;
    std::string registeredAddress_;
    std::string abnormalName_;
    std::string leaderName_;
    std::string leaderAddress_;
    bool hasLeader_{ false };
};
}  // namespace functionsystem::domain_scheduler::test
#endif  // TEST_UNIT_DOMAIN_SCHEDULER_DOMAIN_SCHEDULER_SERVICE_UPLAYER_STUB_H
