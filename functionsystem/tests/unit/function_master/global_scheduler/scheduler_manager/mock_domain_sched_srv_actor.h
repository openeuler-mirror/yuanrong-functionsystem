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

#ifndef FUNCTIONSYSTEM_TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H
#define FUNCTIONSYSTEM_TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "actor/actor.hpp"
#include "async/async.hpp"

namespace functionsystem::test {

class MockDomainSchedSrvActor : public litebus::ActorBase {
public:
    explicit MockDomainSchedSrvActor(const std::string &name) : ActorBase(name){};
    ~MockDomainSchedSrvActor() = default;

    void Init() override
    {
        Receive("UpdateSchedTopoView", &MockDomainSchedSrvActor::UpdateSchedTopoView);
        Receive("Registered", &MockDomainSchedSrvActor::Registered);
        Receive("Schedule", &MockDomainSchedSrvActor::Schedule);
        Receive("ResponseNotifySchedAbnormal", &MockDomainSchedSrvActor::ResponseNotifySchedAbnormal);
        Receive("ResponseNotifyWorkerStatus", &MockDomainSchedSrvActor::ResponseNotifyWorkerStatus);
        Receive("QueryAgentInfo", &MockDomainSchedSrvActor::QueryAgentInfo);
        Receive("QueryResourcesInfo", &MockDomainSchedSrvActor::QueryResourcesInfo);
        Receive("GetSchedulingQueue", &MockDomainSchedSrvActor::GetSchedulingQueue);
    }

    void UpdateSchedTopoView(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockUpdateSchedTopoView(from, name, msg);
    }
    MOCK_METHOD3(MockUpdateSchedTopoView, void(const litebus::AID from, std::string name, std::string msg));

    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockRegistered(from, name, msg);
    }
    MOCK_METHOD3(MockRegistered, void(const litebus::AID from, std::string name, std::string msg));

    void RegisterToGlobalScheduler(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "Register", std::string(msg));
    }

    void NotifySchedAbnormal(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "NotifySchedAbnormal", std::string(msg));
    }

    void NotifyWorkerStatus(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "NotifyWorkerStatus", std::string(msg));
    }

    void ResponseQueryAgentInfo(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "ResponseQueryAgentInfo", std::string(msg));
    }

    void ResponseQueryResourcesInfo(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "ResponseQueryResourcesInfo", std::string(msg));
    }

    void ResponseGetSchedulingQueue(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "ResponseGetSchedulingQueue", std::string(msg));
    }

    void Schedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockSchedule(from, name, msg);
    }

    MOCK_METHOD3(MockSchedule, void(const litebus::AID from, std::string name, std::string msg));

    void QueryAgentInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockQueryAgentInfo(from, name, msg);
    }

    MOCK_METHOD3(MockQueryAgentInfo, void(const litebus::AID from, std::string name, std::string msg));

    void QueryResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockQueryResourcesInfo(from, name, msg);
    }

    MOCK_METHOD3(MockQueryResourcesInfo, void(const litebus::AID from, std::string name, std::string msg));

    void GetSchedulingQueue(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockGetSchedulingQueue(from, name, msg);
    }
    MOCK_METHOD3(MockGetSchedulingQueue, void(const litebus::AID from, std::string name, std::string msg));

    void ResponseNotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseNotifySchedAbnormal(from, name, msg);
    }

    MOCK_METHOD3(MockResponseNotifySchedAbnormal, void(const litebus::AID &, std::string, std::string));

    void ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        MockResponseNotifyWorkerStatus(from, name, msg);
    }

    MOCK_METHOD3(MockResponseNotifyWorkerStatus, void(const litebus::AID &, std::string, std::string));

    void ResponseScheduleToGlobalScheduler(const litebus::AID &to, const std::string &msg)
    {
        Send(to, "ResponseSchedule", std::string(msg));
    }
};

}  // namespace functionsystem::test

#endif  // FUNCTIONSYSTEM_TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H
