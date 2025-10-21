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

#include "maintenance_service_actor.h"

#include <async/defer.hpp>

namespace functionsystem::meta_store {
const uint32_t META_STORE_HEARTBEAT_TIMEOUT = 2000;
MaintenanceServiceActor::MaintenanceServiceActor() : ActorBase("MaintenanceServiceActor")
{
    pingPongDriver_ =
        std::make_shared<PingPongDriver>("meta-store", META_STORE_HEARTBEAT_TIMEOUT,
                                         [aid(GetAID())](const litebus::AID &from, HeartbeatConnection type) {
                                             YRLOG_WARN("client({}) heartbeat timeout", from.HashString());
                                         });
}

void MaintenanceServiceActor::Init()
{
    YRLOG_INFO("Init maintenance service actor");
    Receive("HealthCheck", &MaintenanceServiceActor::HealthCheck);
}

// reserved for circuit breaker
void MaintenanceServiceActor::HealthCheck(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_DEBUG("failed to parse HealthCheck request");
        return;
    }
    YRLOG_DEBUG("{}|receive HealthCheck request", req.requestid());

    messages::MetaStoreResponse res;
    res.set_responseid(req.requestid());

    etcdserverpb::StatusResponse etcdResponse;
    res.set_responsemsg(etcdResponse.SerializeAsString());
    Send(from, "OnHealthCheck", res.SerializeAsString());
}

void MaintenanceServiceActor::OnHealthyStatus(const Status &status)
{
    YRLOG_DEBUG("MaintenanceServiceActor health status changes to healthy({})", status.IsOk());
    healthyStatus_ = status;
}
}  // namespace functionsystem::meta_store
