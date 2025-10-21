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

#ifndef TEST_UNIT_MOCKS_MOCK_SCALER_ACTOR_H
#define TEST_UNIT_MOCKS_MOCK_SCALER_ACTOR_H

#include <gmock/gmock.h>

#include "actor/actor.hpp"
#include "common/constants/actor_name.h"

namespace functionsystem::test {

class MockScalerActor : public litebus::ActorBase {
public:
    MockScalerActor() : litebus::ActorBase(SCALER_ACTOR)
    {
    }

    MOCK_METHOD(std::string, GetCreateAgentResponse, ());

    void CreateAgent(const litebus::AID &from, std::string &&, std::string &&msg)
    {
        Send(from, "CreateAgentResponse", GetCreateAgentResponse());
    }

    void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        auto deletePodRequest = std::make_shared<messages::DeletePodRequest>();
        if (!deletePodRequest->ParseFromString(msg)) {
            return;
        }
        auto resp = std::make_shared<messages::DeletePodResponse>();
        resp->set_code(MockDeletePodResponse());
        resp->set_requestid(deletePodRequest->requestid());
        Send(from, "DeletePodResponse", resp->SerializeAsString());
    }
    MOCK_METHOD0(MockDeletePodResponse, int32_t());

protected:
    void Init() override
    {
        Receive("CreateAgent", &MockScalerActor::CreateAgent);
        Receive("DeletePod", &MockScalerActor::DeletePod);
    }
};
}  // namespace functionsystem::test
#endif  // TEST_UNIT_MOCKS_MOCK_SCALER_ACTOR_H
