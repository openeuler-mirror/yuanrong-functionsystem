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

#ifndef UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_INSTANCE_MANAGER_STUB_H
#define UNIT_FUNCTION_PROXY_LOCAL_SCHEDULER_LOCAL_SCHEDULER_SERVICE_INSTANCE_MANAGER_STUB_H

#include <gmock/gmock.h>

#include <actor/actor.hpp>
#include <async/future.hpp>

namespace functionsystem::test {
class MockInstanceManagerActor : public litebus::ActorBase {
public:
    MockInstanceManagerActor() : litebus::ActorBase(INSTANCE_MANAGER_ACTOR_NAME)
    {
    }
    void Init()
    {
        Receive("ForwardKill", &MockInstanceManagerActor::ForwardKill);
    }

    void ForwardKill(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::ForwardKillRequest req;
        if (!req.ParseFromString(msg)) {
            YRLOG_ERROR("failed to parse ForwardKillRequest");
            return;
        }

        messages::ForwardKillResponse rsp;
        rsp.set_requestid(req.requestid());
        Send(from, "ResponseForwardKill", rsp.SerializeAsString());
    }
};

}  // namespace functionsystem::test
#endif