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

#ifndef BUSPROXY_BUSINESS_STATE_ACTOR_STATE_HANDLER_H
#define BUSPROXY_BUSINESS_STATE_ACTOR_STATE_HANDLER_H

#include "state_actor.h"

namespace functionsystem::function_proxy {

class StateHandler {
public:
    virtual ~StateHandler();

    static void BindStateActor(const std::shared_ptr<StateActor> &stateActor);
    static litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>> SaveState(
        const std::string &instanceId, const std::shared_ptr<runtime_rpc::StreamingMessage> &request);

    static litebus::Future<std::shared_ptr<runtime_rpc::StreamingMessage>> LoadState(
        const std::string &instanceId, const std::shared_ptr<runtime_rpc::StreamingMessage> &request);

    static litebus::Future<Status> DeleteState(const std::string &instanceId);

protected:
    [[maybe_unused]] static void ClearStateActor();

private:
    inline static litebus::AID stateActorAid_{};
};

}  // namespace functionsystem::function_proxy

#endif  // BUSPROXY_BUSINESS_STATE_ACTOR_STATE_HANDLER_H
