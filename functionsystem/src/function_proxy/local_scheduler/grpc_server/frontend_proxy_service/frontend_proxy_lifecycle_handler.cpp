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

#include "frontend_proxy_lifecycle_handler.h"

#include <utility>

#include "common/logs/logging.h"
#include "common/utils/struct_transfer.h"

namespace functionsystem::local_scheduler {
namespace {
constexpr const char *FRONTEND_CALLER_PREFIX = "frontend:";
constexpr const char *FRONTEND_SYSTEM_KILL_CALLER = "";
constexpr int CREATE_READY_CALL_RESULT_FIELD_NUMBER = 4;

std::string BuildFrontendSystemCallerID(const ::frontend_proxy::FrontendRequestContext &context)
{
    const auto &frontendClientID = context.frontendclientid();
    if (frontendClientID.rfind(FRONTEND_CALLER_PREFIX, 0) == 0) {
        return frontendClientID;
    }
    return std::string(FRONTEND_CALLER_PREFIX) + frontendClientID;
}

::frontend_proxy::CreateInstanceResponse BuildCreateResponse(common::ErrorCode code, const std::string &message)
{
    ::frontend_proxy::CreateInstanceResponse response;
    response.mutable_create()->set_code(code);
    response.mutable_create()->set_message(message);
    return response;
}

::frontend_proxy::CreateInstanceResponse BuildCreateResponse(const messages::ScheduleResponse &scheduleResponse)
{
    ::frontend_proxy::CreateInstanceResponse response;
    response.mutable_create()->CopyFrom(TransFromScheduleRspToCreateRsp(scheduleResponse));
    return response;
}

void AttachCreateReadyCallResult(::frontend_proxy::CreateInstanceResponse &response,
                                 const core_service::CallResult &callResult)
{
    // The source proto reserves CreateInstanceResponse.callResult as field 4.
    // Some checked-in generated C++ protobuf files may lag the proto refresh,
    // so write the serialized message as an unknown field to preserve wire
    // compatibility. Regenerated clients parse it as the typed field.
    response.mutable_unknown_fields()->AddLengthDelimited(CREATE_READY_CALL_RESULT_FIELD_NUMBER,
                                                          callResult.SerializeAsString());
}

std::shared_ptr<messages::ScheduleRequest> BuildFrontendScheduleRequest(
    const ::frontend_proxy::CreateInstanceRequest &request)
{
    CreateRequest createReq;
    createReq.CopyFrom(request.create());
    if (createReq.requestid().empty()) {
        createReq.set_requestid(request.context().requestid());
    }
    if (createReq.traceid().empty()) {
        createReq.set_traceid(request.context().traceid());
    }
    return TransFromCreateReqToScheduleReq(std::move(createReq), BuildFrontendSystemCallerID(request.context()));
}
}

FrontendProxyServiceParam::CreateReadyDispatcher BuildFrontendProxyCreateReadyDispatcher(
    const FrontendProxyCreateScheduler &scheduler, const FrontendProxyReadyRegistrar &readyRegistrar)
{
    return [scheduler, readyRegistrar](const ::frontend_proxy::CreateInstanceRequest &request) {
        if (!scheduler) {
            return litebus::Future<::frontend_proxy::CreateInstanceResponse>(
                BuildCreateResponse(common::ERR_INNER_SYSTEM_ERROR, "frontend proxy create scheduler is not configured"));
        }
        if (!readyRegistrar) {
            return litebus::Future<::frontend_proxy::CreateInstanceResponse>(
                BuildCreateResponse(common::ERR_INNER_SYSTEM_ERROR, "frontend proxy ready registrar is not configured"));
        }

        auto scheduleReq = BuildFrontendScheduleRequest(request);
        auto runtimePromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
        YRLOG_INFO("{}|frontend system create function({}) from frontendClientID({}), tenantID({})",
                   scheduleReq->requestid(), scheduleReq->instance().function(), request.context().frontendclientid(),
                   request.context().tenantid());
        return scheduler(scheduleReq, runtimePromise)
            .Then([scheduleReq, readyRegistrar](const messages::ScheduleResponse &scheduleResponse) {
                auto response = BuildCreateResponse(scheduleResponse);
                if (response.create().code() != common::ERR_NONE || response.create().instanceid().empty()) {
                    return litebus::Future<::frontend_proxy::CreateInstanceResponse>(response);
                }

                auto readyPromise = std::make_shared<litebus::Promise<Status>>();
                readyRegistrar(response.create().instanceid(), scheduleReq,
                               [readyPromise](const Status &readyStatus) -> litebus::Future<Status> {
                                   readyPromise->SetValue(readyStatus);
                                   return Status::OK();
                               });
                return readyPromise->GetFuture().Then(
                    [response, scheduleReq](const Status &readyStatus) mutable
                        -> litebus::Future<::frontend_proxy::CreateInstanceResponse> {
                        core_service::CallResult readyResult;
                        readyResult.set_requestid(scheduleReq->requestid());
                        readyResult.set_instanceid(response.create().instanceid());
                        readyResult.set_code(Status::GetPosixErrorCode(readyStatus.StatusCode()));
                        if (readyStatus.IsError()) {
                            readyResult.set_message(readyStatus.ToString());
                        }
                        AttachCreateReadyCallResult(response, readyResult);
                        if (readyStatus.IsError()) {
                            response.mutable_create()->set_code(readyResult.code());
                            response.mutable_create()->set_message(readyResult.message());
                        }
                        return response;
                    });
            });
    };
}

FrontendProxyServiceParam::KillReadyDispatcher BuildFrontendProxyKillReadyDispatcher(
    const FrontendProxyKillInvoker &killInvoker)
{
    return [killInvoker](const ::frontend_proxy::KillInstanceRequest &request) {
        ::frontend_proxy::KillInstanceResponse response;
        if (!killInvoker) {
            response.mutable_kill()->set_code(common::ERR_INNER_SYSTEM_ERROR);
            response.mutable_kill()->set_message("frontend proxy kill invoker is not configured");
            return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
        }

        auto killReq = std::make_shared<KillRequest>();
        killReq->CopyFrom(request.kill());
        if (killReq->requestid().empty()) {
            killReq->set_requestid(request.context().requestid());
        }
        YRLOG_INFO("{}|frontend system kill instance({}) from frontendClientID({}), tenantID({})",
                   killReq->requestid(), killReq->instanceid(), request.context().frontendclientid(),
                   request.context().tenantid());
        return killInvoker(FRONTEND_SYSTEM_KILL_CALLER, killReq)
            .Then([](const KillResponse &killResponse) {
                ::frontend_proxy::KillInstanceResponse response;
                response.mutable_kill()->CopyFrom(killResponse);
                return response;
            });
    };
}

FrontendProxyServiceParam BuildFrontendProxyServiceParam(
    const std::string &nodeID, bool enableCreateDispatch, const FrontendProxyCreateScheduler &scheduler,
    const FrontendProxyReadyRegistrar &readyRegistrar, bool enableKillDispatch,
    const FrontendProxyKillInvoker &killInvoker)
{
    FrontendProxyServiceParam param;
    param.nodeID = nodeID;
    param.enableCreateDispatch = enableCreateDispatch;
    if (enableCreateDispatch) {
        param.createReadyDispatcher = BuildFrontendProxyCreateReadyDispatcher(scheduler, readyRegistrar);
    }
    param.enableKillDispatch = enableKillDispatch;
    if (enableKillDispatch) {
        param.killReadyDispatcher = BuildFrontendProxyKillReadyDispatcher(killInvoker);
    }
    return param;
}

FrontendProxyServiceParam BuildFrontendProxyServiceParam(const std::string &nodeID, bool enableKillDispatch,
                                                         const FrontendProxyKillInvoker &killInvoker)
{
    return BuildFrontendProxyServiceParam(nodeID, false, nullptr, nullptr, enableKillDispatch, killInvoker);
}

}  // namespace functionsystem::local_scheduler
