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

#include "state_actor.h"

#include "async/asyncafter.hpp"
#include "common/utils/generate_message.h"

namespace functionsystem::function_proxy {

StateActor::StateActor(const std::shared_ptr<StateClient> &stateClient)
    : litebus::ActorBase("state_actor"), stateClient_(stateClient)
{
}

litebus::Future<StateSaveResponse> StateActor::SaveState(const std::string &instanceId,
                                                         const std::shared_ptr<StateSaveRequest> &request)
{
    if (instanceId.empty() || !stateClient_) {
        YRLOG_ERROR("failed to save state: empty instance id {} or stateClient null", instanceId);
        return GenStateSaveResponse(common::ErrorCode::ERR_PARAM_INVALID, "save state failed: empty instance id");
    }

    // use instance id as checkpoint id
    auto status = stateClient_->Set(instanceId, request->state());
    if (status.IsError()) {
        YRLOG_ERROR("failed to save state, status: {}", status.ToString());
        return GenStateSaveResponse(common::ErrorCode::ERR_INNER_SYSTEM_ERROR,
                                    "save state failed: " + status.ToString());
    }
    YRLOG_INFO("succeed to save instance({}) state", instanceId);
    return GenStateSaveResponse(common::ErrorCode::ERR_NONE, "", instanceId);
}

litebus::Future<StateLoadResponse> StateActor::LoadState(const std::shared_ptr<StateLoadRequest> &request)
{
    if (request->checkpointid().empty() || !stateClient_) {
        YRLOG_ERROR("failed to load state: empty checkpoint id");
        return GenStateLoadResponse(common::ErrorCode::ERR_PARAM_INVALID, "load state failed: empty checkpoint id");
    }

    std::string state;
    auto status = stateClient_->Get(request->checkpointid(), state);
    if (status.IsError()) {
        YRLOG_ERROR("failed to load state: {}", status.ToString());
        return GenStateLoadResponse(common::ErrorCode::ERR_INNER_SYSTEM_ERROR,
                                    "load state failed: " + status.ToString());
    }
    YRLOG_INFO("succeed to load checkpoint({}) state", request->checkpointid());
    return GenStateLoadResponse(common::ErrorCode::ERR_NONE, "", state);
}

litebus::Future<Status> StateActor::DeleteState(const std::string &instanceId)
{
    return stateClient_->Del(instanceId);
}

void StateActor::InitStateClient()
{
    RETURN_IF_NULL(stateClient_);
    if (auto status = stateClient_->Init(); status.IsOk()) {
        YRLOG_INFO("succeed to init state client");
        return;
    }
    YRLOG_WARN("failed to init state client, try to reconnect");
    const uint32_t stateClientInitRetryPeriod = 1000;  // ms
    litebus::AsyncAfter(stateClientInitRetryPeriod, GetAID(), &StateActor::InitStateClient);
}

}  // namespace functionsystem::function_proxy