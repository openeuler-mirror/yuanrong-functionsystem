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

#ifndef TEST_INTEGRATION_MOCKS_MOCK_DOMAIN_SCHED_SRV_ACTOR_H
#define TEST_INTEGRATION_MOCKS_MOCK_DOMAIN_SCHED_SRV_ACTOR_H

#include <gmock/gmock.h>

#include "actor/actor.hpp"

namespace functionsystem::test {
class MockDomainSchedSrvActor : public litebus::ActorBase {
public:
    explicit MockDomainSchedSrvActor(const std::string &name) : litebus::ActorBase(name)
    {
    }

    ~MockDomainSchedSrvActor() = default;

    MOCK_METHOD(std::string, GetResponseSchedule, ());

    MOCK_METHOD(std::string, GetResources, ());

protected:
    void Init() override
    {
        Receive("PullResources", &MockDomainSchedSrvActor::PullResources);
        Receive("Schedule", &MockDomainSchedSrvActor::Schedule);
    }

    void PullResources(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        Send(from, "UpdateResources", GetResources());
    }

    void Schedule(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        Send(from, "ResponseSchedule", GetResponseSchedule());
    }
};
}  // namespace functionsystem::test

#endif  // TEST_INTEGRATION_MOCKS_MOCK_DOMAIN_SCHED_SRV_ACTOR_H
