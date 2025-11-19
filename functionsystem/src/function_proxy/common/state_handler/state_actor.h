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

#ifndef BUSPROXY_BUSINESS_STATE_ACTOR_STATE_ACTOR_H
#define BUSPROXY_BUSINESS_STATE_ACTOR_STATE_ACTOR_H

#include <memory>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix_pb.h"
#include "state_client.h"

namespace functionsystem::function_proxy {

class StateActor : public litebus::ActorBase {
public:
    explicit StateActor(const std::shared_ptr<StateClient> &stateClient);
    ~StateActor() override = default;

    litebus::Future<StateSaveResponse> SaveState(const std::string &instanceId,
                                                 const std::shared_ptr<StateSaveRequest> &request);

    litebus::Future<StateLoadResponse> LoadState(const std::shared_ptr<StateLoadRequest> &request);

    litebus::Future<Status> DeleteState(const std::string &instanceId);

    void InitStateClient();

private:
    std::shared_ptr<StateClient> stateClient_;
};

}  // namespace functionsystem::function_proxy

#endif  // BUSPROXY_BUSINESS_STATE_ACTOR_STATE_ACTOR_H
