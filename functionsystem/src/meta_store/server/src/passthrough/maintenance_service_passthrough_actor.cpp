/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "maintenance_service_passthrough_actor.h"

#include <async/defer.hpp>

namespace functionsystem::meta_store {
void MaintenanceServicePassthroughActor::HealthCheck(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse HealthCheck request");
        return;
    }

    if (healthyStatus_.IsError()) {
        YRLOG_ERROR("{}|failed to health check, fallbreak", req.requestid());
        StatusResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call Health Check: " + healthyStatus_.GetMessage());
        return OnHealthCheck(response, req.requestid(), from);
    }

    YRLOG_DEBUG("{}|receive Passthrough HealthCheck request", req.requestid());
    etcdClient_->HealthCheck().OnComplete(litebus::Defer(GetAID(), &MaintenanceServicePassthroughActor::OnHealthCheck,
                                                         std::placeholders::_1, req.requestid(), from));
}

void MaintenanceServicePassthroughActor::OnHealthCheck(const litebus::Future<StatusResponse> &response,
                                                       const std::string &id, const litebus::AID &from)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    etcdserverpb::StatusResponse etcdResponse;
    if (response.IsError()) {
        int32_t code = response.GetErrorCode();
        etcdResponse.add_errors(std::to_string(code));
    } else if (response.Get().status.IsError()) {
        int32_t code = response.Get().status.StatusCode();
        etcdResponse.add_errors(std::to_string(code));
    }
    res.set_responsemsg(etcdResponse.SerializeAsString());

    YRLOG_DEBUG("{}|send StatusResponse to {}", id, std::string(from));
    Send(from, "OnHealthCheck", res.SerializeAsString());
}
}  // namespace functionsystem::meta_store