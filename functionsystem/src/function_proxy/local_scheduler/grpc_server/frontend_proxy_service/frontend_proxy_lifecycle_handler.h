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

#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_LIFECYCLE_HANDLER_H
#define FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_LIFECYCLE_HANDLER_H

#include <functional>
#include <memory>
#include <string>

#include "common/status/status.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/frontend_proxy_service.pb.h"
#include "common/proto/pb/posix_pb.h"
#include "grpc_server/frontend_proxy_service/frontend_proxy_service.h"

namespace functionsystem::local_scheduler {

using FrontendProxyReadyCallback = std::function<litebus::Future<CallResultAck>(
    const std::shared_ptr<functionsystem::CallResult> &)>;
using FrontendProxyReadyRegistrar =
    std::function<void(const std::string &, const std::shared_ptr<messages::ScheduleRequest> &,
                       FrontendProxyReadyCallback)>;
using FrontendProxyCreateReadyScheduler =
    std::function<litebus::Future<messages::ScheduleResponse>(
        const std::shared_ptr<messages::ScheduleRequest> &,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
        FrontendProxyReadyCallback)>;
using FrontendProxyReadyUnregister = std::function<void(const std::string &, const std::string &)>;
using FrontendProxyCreateScheduler =
    std::function<litebus::Future<messages::ScheduleResponse>(
        const std::shared_ptr<messages::ScheduleRequest> &,
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &)>;
using FrontendProxyKillInvoker =
    std::function<litebus::Future<KillResponse>(const std::string &, const std::string &,
                                                const std::shared_ptr<KillRequest> &)>;
using FrontendProxyKillCleanupProbe = FrontendProxyServiceParam::KillCleanupProbe;

FrontendProxyServiceParam::CreateReadyDispatcher BuildFrontendProxyCreateReadyDispatcher(
    const FrontendProxyCreateReadyScheduler &scheduler);

FrontendProxyServiceParam::KillReadyDispatcher BuildFrontendProxyKillReadyDispatcher(
    const FrontendProxyKillInvoker &killInvoker);

FrontendProxyServiceParam BuildFrontendProxyServiceParam(
    const std::string &nodeID, bool enableCreateDispatch, const FrontendProxyCreateReadyScheduler &scheduler,
    const FrontendProxyReadyUnregister &readyUnregister, bool enableKillDispatch,
    const FrontendProxyKillInvoker &killInvoker,
    const FrontendProxyKillCleanupProbe &killCleanupProbe = nullptr);

FrontendProxyServiceParam BuildFrontendProxyServiceParam(const std::string &nodeID, bool enableKillDispatch,
                                                         const FrontendProxyKillInvoker &killInvoker);

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_LIFECYCLE_HANDLER_H
