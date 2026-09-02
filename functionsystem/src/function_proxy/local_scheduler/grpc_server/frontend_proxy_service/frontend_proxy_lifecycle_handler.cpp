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
constexpr const char *FRONTEND_SYSTEM_CREATE_CALLER = "";
constexpr const char *FRONTEND_SYSTEM_KILL_CALLER = "";
// Reason recorded when unregistering a frontend ready ticket on timeout.
constexpr const char *FRONTEND_CREATE_READY_UNREGISTER_REASON = "frontend create ready call result timed out";
// Generic message attached to the timeout CallResult when no deploy-failure
// snapshot is available.
constexpr const char *FRONTEND_CREATE_READY_TIMEOUT_MSG = "frontend proxy create ready call result timed out";

::frontend_proxy::CreateInstanceResponse BuildCreateResponse(common::ErrorCode code, const std::string &message)
{
    ::frontend_proxy::CreateInstanceResponse response;
    response.mutable_create()->set_code(code);
    response.mutable_create()->set_message(message);
    return response;
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
    // Frontend unary create is a system-service request, not a runtime child
    // create. Keep the old runtime parent/sender identity empty so
    // InstanceCtrl does not try to authorize frontendClientID as a runtime
    // instance parent. The reviewed system-caller marker is carried by both
    // createOptions["source"]="frontend" and InstanceInfo.extensions so
    // function master can recognize frontend-created instances without a
    // runtime parent.
    auto scheduleReq = TransFromCreateReqToScheduleReq(std::move(createReq), FRONTEND_SYSTEM_CREATE_CALLER);
    (*scheduleReq->mutable_instance()->mutable_extensions())[CREATE_SOURCE] = FRONTEND_STR;
    return scheduleReq;
}

// Builds the CallResult emitted when the frontend ready ticket times out. It
// first consults the last deploy-failure snapshot (if a lookup is wired) so the
// real supervisor error surfaces instead of the generic timeout string, then
// unregisters the ticket. Snapshot lookup must run before unregister because
// UnregisterFrontendReadyWait erases the snapshot. The lookup runs on the
// InstanceCtrl actor strand; unregister and result building run on that
// strand's completion, so the snapshot continuation is chained asynchronously.
litebus::Future<std::shared_ptr<functionsystem::CallResult>> BuildFrontendCreateReadyTimeoutResult(
    const FrontendCreateFailureLookup &createFailureLookup, const FrontendProxyReadyUnregister &readyUnregister,
    const std::string &requestID)
{
    if (createFailureLookup) {
        return createFailureLookup(requestID).Then(
            [requestID, readyUnregister](const FrontendCreateFailureSnapshot &snapshot)
                -> std::shared_ptr<functionsystem::CallResult> {
                if (readyUnregister) {
                    readyUnregister(requestID, FRONTEND_CREATE_READY_UNREGISTER_REASON);
                }
                auto result = std::make_shared<functionsystem::CallResult>();
                result->set_requestid(requestID);
                if (snapshot.present) {
                    result->set_code(static_cast<common::ErrorCode>(snapshot.code));
                    result->set_message(snapshot.message);
                    result->set_instanceid(snapshot.instanceID);
                } else {
                    result->set_code(common::ERR_INNER_SYSTEM_ERROR);
                    result->set_message(FRONTEND_CREATE_READY_TIMEOUT_MSG);
                }
                return result;
            });
    }
    if (readyUnregister) {
        readyUnregister(requestID, FRONTEND_CREATE_READY_UNREGISTER_REASON);
    }
    auto result = std::make_shared<functionsystem::CallResult>();
    result->set_requestid(requestID);
    result->set_code(common::ERR_INNER_SYSTEM_ERROR);
    result->set_message(FRONTEND_CREATE_READY_TIMEOUT_MSG);
    return result;
}
}

FrontendProxyServiceParam::CreateReadyDispatcher BuildFrontendProxyCreateReadyDispatcher(
    const FrontendProxyCreateReadyScheduler &scheduler, const FrontendProxyReadyUnregister &readyUnregister,
    uint64_t readyTimeoutMs, const FrontendCreateFailureLookup &createFailureLookup)
{
    return [scheduler, readyUnregister, readyTimeoutMs,
            createFailureLookup](const ::frontend_proxy::CreateInstanceRequest &request) {
        if (!scheduler) {
            return litebus::Future<::frontend_proxy::CreateInstanceResponse>(BuildCreateResponse(
                common::ERR_INNER_SYSTEM_ERROR, "frontend proxy create scheduler is not configured"));
        }
        auto scheduleReq = BuildFrontendScheduleRequest(request);
        auto runtimePromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
        auto readyPromise = std::make_shared<litebus::Promise<std::shared_ptr<functionsystem::CallResult>>>();
        // Still wired so the ready ticket and its timeout cleanup drive state-machine
        // cleanup in the background, even though the create response no longer waits.
        [[maybe_unused]] auto readyResolved = readyPromise->GetFuture().After(
            readyTimeoutMs,
            [scheduleReq, readyUnregister, createFailureLookup](
                const litebus::Future<std::shared_ptr<functionsystem::CallResult>> &)
                -> litebus::Future<std::shared_ptr<functionsystem::CallResult>> {
                return BuildFrontendCreateReadyTimeoutResult(
                    createFailureLookup, readyUnregister, scheduleReq->requestid());
            });
        YRLOG_INFO("{}|frontend system create function({}) from frontendClientID({}), tenantID({})",
                   scheduleReq->requestid(), scheduleReq->instance().function(), request.context().frontendclientid(),
                   request.context().tenantid());
        return scheduler(scheduleReq, runtimePromise,
                         [readyPromise](const std::shared_ptr<functionsystem::CallResult> &readyResult)
                             -> litebus::Future<CallResultAck> {
            readyPromise->SetValue(readyResult);
            CallResultAck ack;
            ack.set_code(common::ERR_NONE);
            ack.set_message("success");
            return ack;
                         })
            .Then([](const messages::ScheduleResponse &scheduleResponse) {
                // Return the IID from the ScheduleResponse on schedule success without
                // waiting for ready; callresult is left unset.
                ::frontend_proxy::CreateInstanceResponse response;
                response.mutable_create()->CopyFrom(TransFromScheduleRspToCreateRsp(scheduleResponse));
                return litebus::Future<::frontend_proxy::CreateInstanceResponse>(response);
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
        return killInvoker(FRONTEND_SYSTEM_KILL_CALLER, request.context().tenantid(), killReq)
            .Then([](const KillResponse &killResponse) {
                ::frontend_proxy::KillInstanceResponse response;
                response.mutable_kill()->CopyFrom(killResponse);
                return response;
            });
    };
}

FrontendProxyServiceParam BuildFrontendProxyServiceParam(const std::string &nodeID,
                                                         const FrontendProxyServiceBindings &bindings)
{
    FrontendProxyServiceParam param;
    param.nodeID = nodeID;
    param.enableCreateDispatch = bindings.enableCreateDispatch;
    if (bindings.enableCreateDispatch) {
        param.createReadyDispatcher =
            BuildFrontendProxyCreateReadyDispatcher(bindings.scheduler, bindings.readyUnregister,
                                                    FRONTEND_CREATE_READY_TIMEOUT_MS, bindings.createFailureLookup);
        param.createWaitCanceller = bindings.readyUnregister;
    }
    param.enableKillDispatch = bindings.enableKillDispatch;
    if (bindings.enableKillDispatch) {
        param.killReadyDispatcher = BuildFrontendProxyKillReadyDispatcher(bindings.killInvoker);
        param.killCleanupProbe = bindings.killCleanupProbe;
    }
    return param;
}

FrontendProxyServiceParam BuildFrontendProxyServiceParam(const std::string &nodeID, bool enableKillDispatch,
                                                         const FrontendProxyKillInvoker &killInvoker)
{
    return BuildFrontendProxyServiceParam(
        nodeID, { false, nullptr, nullptr, nullptr, enableKillDispatch, killInvoker, nullptr });
}

}  // namespace functionsystem::local_scheduler
