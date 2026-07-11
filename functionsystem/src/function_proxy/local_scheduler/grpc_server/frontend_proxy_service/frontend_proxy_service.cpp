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

#include "frontend_proxy_service.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

#include "common/logs/logging.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"

namespace functionsystem::local_scheduler {
namespace {
constexpr const char *FRONTEND_CALLER_PREFIX = "frontend:";
constexpr const char *FRONTEND_PROXY_CONTROL_NOT_WIRED =
    "frontend proxy create/kill control path is not wired; use libruntime fallback until control semantics are reviewed";
constexpr const char *FRONTEND_PROXY_CREATE_READY_NOT_WIRED =
    "frontend proxy create requires ready create dispatcher; legacy stream create dispatcher is not allowed";
constexpr const char *FRONTEND_PROXY_KILL_READY_NOT_WIRED =
    "frontend proxy kill requires ready kill dispatcher; legacy stream kill dispatcher is not allowed";
constexpr const char *FRONTEND_CREATE_SOURCE_KEY = "source";
constexpr const char *FRONTEND_CREATE_SOURCE_VALUE = "frontend";
constexpr const char *FRONTEND_KILL_ROUTE_STALE_MESSAGE =
    "frontend proxy is not the owning proxy for this instance";
constexpr uint64_t FRONTEND_WAIT_POLL_MS = 20;

void LogLifecycleEvent(const char *operation, const char *dispatchPhase,
                       const ::frontend_proxy::FrontendRequestContext &context,
                       const std::string &endpointNodeID, const std::string &instanceID,
                       const std::string &outcome, bool retryable = false,
                       const std::string &retryReason = "", const std::string &cleanupOutcome = "",
                       const std::string &endpointAddress = "", const std::string &owningProxyID = "",
                       uint32_t attempt = 1,
                       const std::string &transport = FrontendProxyService::LIFECYCLE_TRANSPORT)
{
    // Payload-free lifecycle event. Only fields known at this boundary are
    // emitted; transport authentication is outside this service and is not
    // implied by frontendClientID or tenant consistency checks.
    YRLOG_INFO("frontend_lifecycle operation={} dispatchPhase={} frontendClientID={} tenantID={} requestID={} "
               "traceID={} endpointNodeID={} endpointAddress={} owningProxyID={} instanceID={} attempt={} "
               "transport={} outcome={} retryable={} retryReason={} cleanupOutcome={}",
               operation, dispatchPhase, context.frontendclientid(), context.tenantid(), context.requestid(),
               context.traceid(), endpointNodeID, endpointAddress, owningProxyID, instanceID, attempt, transport,
               outcome, retryable, retryReason, cleanupOutcome);
}

template <typename T>
litebus::Option<T> WaitFrontendResult(const litebus::Future<T> &future, ::grpc::ServerContext *context,
                                      uint64_t timeoutMs, bool &cancelled)
{
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        if (context != nullptr && context->IsCancelled()) {
            cancelled = true;
            return litebus::None();
        }
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsed >= timeoutMs) {
            return litebus::None();
        }
        auto result = future.Get(std::min(FRONTEND_WAIT_POLL_MS, timeoutMs - elapsed));
        if (result.IsSome()) {
            return result;
        }
    }
}

bool HasCreateTenantMismatch(const ::frontend_proxy::CreateInstanceRequest &request)
{
    const auto &options = request.create().createoptions();
    for (const auto *key : { "tenantID", "tenantId", "tenant" }) {
        auto iter = options.find(key);
        if (iter != options.end() && !iter->second.empty() && iter->second != request.context().tenantid()) {
            return true;
        }
    }
    return false;
}

bool HasOperationRequestIDMismatch(const ::frontend_proxy::FrontendRequestContext &context,
                                   const std::string &operationRequestID)
{
    return !operationRequestID.empty() && operationRequestID != context.requestid();
}

bool HasContextTenantMismatch(const ::frontend_proxy::FrontendRequestContext &context)
{
    for (const auto *key : { "tenantID", "tenantId", "tenant" }) {
        const auto iter = context.labels().find(key);
        if (iter != context.labels().end() && !iter->second.empty() && iter->second != context.tenantid()) {
            return true;
        }
    }
    return false;
}

std::string BuildPendingKey(const std::string &, const std::string &requestID)
{
    // Frontend unary calls are system-service requests, not runtime-to-runtime calls.
    // The runtime CallResult may carry an empty/legacy sender instance id, so the
    // synchronous frontend waiter is keyed only by the unique requestID.
    return requestID;
}

std::string BuildFrontendCallerID(const ::frontend_proxy::FrontendRequestContext &context)
{
    const auto &frontendClientID = context.frontendclientid();
    if (frontendClientID.rfind(FRONTEND_CALLER_PREFIX, 0) == 0) {
        return frontendClientID;
    }
    return std::string(FRONTEND_CALLER_PREFIX) + frontendClientID;
}

SharedStreamMsg CreateCallResultAck(common::ErrorCode code, const std::string &message, const std::string &messageID)
{
    auto response = std::make_shared<runtime_rpc::StreamingMessage>();
    response->set_messageid(messageID);
    auto ack = response->mutable_callresultack();
    ack->set_code(code);
    ack->set_message(message);
    return response;
}

SharedStreamMsg CreateInvokeRequest(const ::frontend_proxy::InvokeInstanceRequest &request)
{
    auto invoke = std::make_shared<runtime_rpc::StreamingMessage>();
    auto invokeReq = invoke->mutable_invokereq();
    invokeReq->CopyFrom(request.invoke());
    if (invokeReq->requestid().empty()) {
        invokeReq->set_requestid(request.context().requestid());
    }
    if (invokeReq->traceid().empty()) {
        invokeReq->set_traceid(request.context().traceid());
    }
    return invoke;
}

::frontend_proxy::CreateInstanceRequest CreateReadyRequest(const ::frontend_proxy::CreateInstanceRequest &request)
{
    ::frontend_proxy::CreateInstanceRequest readyRequest;
    readyRequest.CopyFrom(request);
    if (readyRequest.create().requestid().empty()) {
        readyRequest.mutable_create()->set_requestid(request.context().requestid());
    }
    if (readyRequest.create().traceid().empty()) {
        readyRequest.mutable_create()->set_traceid(request.context().traceid());
    }
    return readyRequest;
}

::frontend_proxy::KillInstanceRequest CreateReadyKillRequest(const ::frontend_proxy::KillInstanceRequest &request)
{
    ::frontend_proxy::KillInstanceRequest readyRequest;
    readyRequest.CopyFrom(request);
    if (readyRequest.kill().requestid().empty()) {
        readyRequest.mutable_kill()->set_requestid(request.context().requestid());
    }
    return readyRequest;
}

class FrontendCallResultRegistry {
public:
    struct WatchResult {
        bool registered;
        litebus::Future<SharedStreamMsg> future;
    };

    static WatchResult Watch(const std::string &frontendCallerID, const std::string &requestID)
    {
        auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
        std::lock_guard<std::mutex> lock(Mutex());
        const bool inserted = Pending().emplace(BuildPendingKey(frontendCallerID, requestID), promise).second;
        return { inserted, promise->GetFuture() };
    }

    static void Cancel(const std::string &frontendCallerID, const std::string &requestID)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        (void)Pending().erase(BuildPendingKey(frontendCallerID, requestID));
    }

    static std::pair<bool, SharedStreamMsg> Receive(const std::string &, const SharedStreamMsg &request)
    {
        if (request == nullptr || !request->has_callresultreq()) {
            return { false, nullptr };
        }
        const auto &callResult = request->callresultreq();
        auto key = BuildPendingKey(callResult.instanceid(), callResult.requestid());
        std::shared_ptr<litebus::Promise<SharedStreamMsg>> promise;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            auto iter = Pending().find(key);
            if (iter == Pending().end()) {
                return { false, nullptr };
            }
            promise = iter->second;
            (void)Pending().erase(iter);
        }
        promise->SetValue(request);
        return { true, CreateCallResultAck(common::ERR_NONE, "success", request->messageid()) };
    }

private:
    static std::mutex &Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static std::unordered_map<std::string, std::shared_ptr<litebus::Promise<SharedStreamMsg>>> &Pending()
    {
        static std::unordered_map<std::string, std::shared_ptr<litebus::Promise<SharedStreamMsg>>> pending;
        return pending;
    }
};

litebus::Future<SharedStreamMsg> DispatchInvoke(const FrontendProxyServiceParam &param,
                                                const std::string &frontendCallerID,
                                                const SharedStreamMsg &invokeRequest)
{
    if (param.invokeDispatcher) {
        return param.invokeDispatcher(frontendCallerID, invokeRequest);
    }
    return InvocationHandler::Invoke(frontendCallerID, invokeRequest);
}

std::optional<litebus::Future<::frontend_proxy::CreateInstanceResponse>> DispatchReadyCreate(
    const FrontendProxyServiceParam &param, const ::frontend_proxy::CreateInstanceRequest &request)
{
    if (!param.enableCreateDispatch || !param.createReadyDispatcher) {
        return std::nullopt;
    }
    return param.createReadyDispatcher(CreateReadyRequest(request));
}

std::optional<litebus::Future<::frontend_proxy::KillInstanceResponse>> DispatchReadyKill(
    const FrontendProxyServiceParam &param, const ::frontend_proxy::KillInstanceRequest &request)
{
    if (!param.enableKillDispatch || !param.killReadyDispatcher) {
        return std::nullopt;
    }
    return param.killReadyDispatcher(CreateReadyKillRequest(request));
}

}  // namespace

FrontendProxyService::FrontendProxyService(FrontendProxyServiceParam &&param) : param_(std::move(param))
{
    InvocationHandler::RegisterFrontendCallResultReceiver(FrontendCallResultRegistry::Receive);
}

::grpc::Status FrontendProxyService::InvokeInstance(::grpc::ServerContext *context,
                                                    const ::frontend_proxy::InvokeInstanceRequest *request,
                                                    ::frontend_proxy::InvokeInstanceResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy invoke args" };
    }
    if (!HasRequiredLifecycleContext(request->context()) || request->invoke().instanceid().empty()) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke requires context.frontendClientID, context.requestID, context.tenantID and invoke.instanceID");
        return ::grpc::Status::OK;
    }
    // This is a consistency check only. Authentication of the frontend service
    // remains a transport/interceptor concern and is not inferred from tenantID.
    if (HasContextTenantMismatch(request->context())) {
        SetStatus(response->mutable_status(), common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy invoke tenant does not match context labels");
        return ::grpc::Status::OK;
    }
    if (HasOperationRequestIDMismatch(request->context(), request->invoke().requestid())) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke request id does not match context request id");
        return ::grpc::Status::OK;
    }

    const auto requestID = request->invoke().requestid().empty() ? request->context().requestid()
                                                                 : request->invoke().requestid();
    const auto traceID = request->invoke().traceid().empty() ? request->context().traceid()
                                                             : request->invoke().traceid();
    const auto frontendCallerID = BuildFrontendCallerID(request->context());
    YRLOG_INFO("{}|frontend proxy invoke received by proxy({}), frontendClientID({}), instance({}), traceID({})",
               requestID, param_.nodeID, request->context().frontendclientid(), request->invoke().instanceid(), traceID);
    LogLifecycleEvent("invoke", "received", request->context(), param_.nodeID,
                      request->invoke().instanceid(), "accepted", false, "", "", param_.endpointAddress,
                      param_.nodeID);

    auto watchResult = FrontendCallResultRegistry::Watch(frontendCallerID, requestID);
    if (!watchResult.registered) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke requires a globally unique request id");
        return ::grpc::Status::OK;
    }
    // Do not encode frontendClientID as the old runtime caller/from identity.
    // frontendClientID is a system-service management identity; runtime senderid
    // remains empty for public FaaS frontend invokes.
    auto invokeFuture = DispatchInvoke(param_, "", CreateInvokeRequest(*request));
    bool invokeCancelled = false;
    auto invokeResponse = WaitFrontendResult(invokeFuture, context, param_.invokeResultTimeoutMs, invokeCancelled);
    if (!invokeResponse.IsSome()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  invokeCancelled ? "frontend proxy invoke cancelled with unknown dispatch outcome"
                                  : "frontend proxy invoke timed out with unknown dispatch outcome",
                  false, "post-dispatch-unknown");
        LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                          request->invoke().instanceid(), invokeCancelled ? "cancelled-unknown" : "timeout-unknown",
                          false, "post-dispatch-unknown", "result-waiter-unregistered", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }
    if (!invokeResponse.Get()->has_invokersp()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call response");
        LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                          request->invoke().instanceid(), "invalid-response", false, "", "result-waiter-unregistered",
                          param_.endpointAddress, param_.nodeID);
        return ::grpc::Status::OK;
    }

    response->mutable_invoke()->CopyFrom(invokeResponse.Get()->invokersp());
    if (invokeResponse.Get()->invokersp().code() != common::ERR_NONE) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), invokeResponse.Get()->invokersp().code(),
                  invokeResponse.Get()->invokersp().message(), true, "call-response-error");
        LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                          request->invoke().instanceid(), "dispatch-failed", true, "call-response-error",
                          "result-waiter-unregistered", param_.endpointAddress, param_.nodeID);
        return ::grpc::Status::OK;
    }

    bool resultCancelled = false;
    auto callResult = WaitFrontendResult(watchResult.future, context, param_.invokeResultTimeoutMs, resultCancelled);
    if (!callResult.IsSome()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  resultCancelled ? "frontend proxy invoke cancelled while result outcome is unknown"
                                  : "frontend proxy invoke timed out while result outcome is unknown",
                  false, "post-dispatch-unknown");
        LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                          request->invoke().instanceid(), resultCancelled ? "cancelled-unknown" : "timeout-unknown",
                          false, "post-dispatch-unknown", "result-waiter-unregistered", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }
    if (!callResult.Get()->has_callresultreq()) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call result");
        LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                          request->invoke().instanceid(), "invalid-result", false, "", "", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }

    response->mutable_callresult()->CopyFrom(callResult.Get()->callresultreq());
    SetStatus(response->mutable_status(), callResult.Get()->callresultreq().code(),
              callResult.Get()->callresultreq().message());
    LogLifecycleEvent("invoke", "terminal", request->context(), param_.nodeID,
                      request->invoke().instanceid(),
                      callResult.Get()->callresultreq().code() == common::ERR_NONE ? "success" : "failed", false,
                      "", "", param_.endpointAddress, param_.nodeID);
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::CreateInstance(::grpc::ServerContext *context,
                                                    const ::frontend_proxy::CreateInstanceRequest *request,
                                                    ::frontend_proxy::CreateInstanceResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy create args" };
    }
    if (!HasRequiredLifecycleContext(request->context()) || request->create().function().empty()) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create requires context.frontendClientID, context.requestID, context.tenantID and create.function");
        return ::grpc::Status::OK;
    }
    const auto &createOptions = request->create().createoptions();
    const auto sourceIter = createOptions.find(FRONTEND_CREATE_SOURCE_KEY);
    if (sourceIter == createOptions.end() || sourceIter->second != FRONTEND_CREATE_SOURCE_VALUE) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create requires create.createOptions source=frontend");
        return ::grpc::Status::OK;
    }
    if (HasCreateTenantMismatch(*request)) {
        SetStatus(response->mutable_status(), common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy create tenant does not match create options");
        return ::grpc::Status::OK;
    }
    if (HasOperationRequestIDMismatch(request->context(), request->create().requestid())) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create request id does not match context request id");
        return ::grpc::Status::OK;
    }

    YRLOG_INFO("{}|frontend proxy create received by proxy({}), frontendClientID({}), function({})",
               request->context().requestid(), param_.nodeID, request->context().frontendclientid(),
               request->create().function());
    LogLifecycleEvent("create", "received", request->context(), param_.nodeID, "", "accepted", false, "", "",
                      param_.endpointAddress, param_.nodeID);
    auto createFuture = DispatchReadyCreate(param_, *request);
    if (createFuture.has_value()) {
        bool cancelled = false;
        auto createResponse = WaitFrontendResult(createFuture.value(), context, param_.invokeResultTimeoutMs,
                                                 cancelled);
        if (!createResponse.IsSome() || !createResponse.Get().has_create()) {
            if (param_.createWaitCanceller) {
                param_.createWaitCanceller(request->context().requestid(),
                                           cancelled ? "grpc client cancelled" : "frontend create timed out");
            }
            SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      cancelled ? "frontend proxy create cancelled while schedule outcome is unknown"
                                : "frontend proxy create timed out while schedule outcome is unknown",
                      false, "post-dispatch-unknown");
            LogLifecycleEvent("create", "terminal", request->context(), param_.nodeID, "",
                              cancelled ? "cancelled-unknown" : "timeout-unknown", false,
                              "post-dispatch-unknown", "ready-waiter-unregistered", param_.endpointAddress,
                              param_.nodeID);
            return ::grpc::Status::OK;
        }
        response->CopyFrom(createResponse.Get());
        const auto &createRsp = response->create();
        if (createRsp.code() == common::ERR_NONE && createRsp.instanceid().empty()) {
            SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      "frontend proxy create response missing instance id");
            return ::grpc::Status::OK;
        }
        if (createRsp.code() == common::ERR_NONE) {
            if (response->routeaddress().empty()) {
                response->set_routeaddress(param_.nodeID);
            }
        }
        SetStatus(response->mutable_status(), createRsp.code(), createRsp.message());
        const auto owningProxyID = response->has_callresult() && response->callresult().has_runtimeinfo()
                                       ? response->callresult().runtimeinfo().proxyid()
                                       : param_.nodeID;
        LogLifecycleEvent("create", "terminal", request->context(), param_.nodeID, createRsp.instanceid(),
                          createRsp.code() == common::ERR_NONE ? "success" : "failed", false, "", "",
                          response->routeaddress().empty() ? param_.endpointAddress : response->routeaddress(),
                          owningProxyID);
        if (response->has_callresult()) {
            LogLifecycleEvent(FrontendProxyService::READY_OPERATION, "terminal", request->context(), param_.nodeID,
                              createRsp.instanceid(),
                              response->callresult().code() == common::ERR_NONE ? "success" : "failed", false, "",
                              "", response->routeaddress().empty() ? param_.endpointAddress : response->routeaddress(),
                              owningProxyID);
        }
        return ::grpc::Status::OK;
    }
    if (param_.enableCreateDispatch) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  FRONTEND_PROXY_CREATE_READY_NOT_WIRED, false, "control-path-not-wired");
        return ::grpc::Status::OK;
    }
    // Frontend unary create must not reuse the old Posix create boundary yet:
    // the raw libruntime create contract returns the final NotifyRequest, while
    // Posix create only returns the immediate scheduler CreateResponse and its
    // caller argument is also interpreted as parent/sender runtime identity.
    // Keep the proto/API shape for future control-plane work, but force callers
    // to use the reviewed libruntime fallback until ready-notify semantics and
    // frontend caller identity are designed end-to-end.
    SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
              FRONTEND_PROXY_CONTROL_NOT_WIRED, false, "control-path-not-wired");
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::KillInstance(::grpc::ServerContext *context,
                                                  const ::frontend_proxy::KillInstanceRequest *request,
                                                  ::frontend_proxy::KillInstanceResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy kill args" };
    }
    if (!HasRequiredLifecycleContext(request->context()) || request->kill().instanceid().empty()) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy kill requires context.frontendClientID, context.requestID, context.tenantID and kill.instanceID");
        return ::grpc::Status::OK;
    }
    if (HasOperationRequestIDMismatch(request->context(), request->kill().requestid())) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy kill request id does not match context request id");
        return ::grpc::Status::OK;
    }

    YRLOG_INFO("{}|frontend proxy kill received by proxy({}), frontendClientID({}), instance({})",
               request->context().requestid(), param_.nodeID, request->context().frontendclientid(),
               request->kill().instanceid());
    LogLifecycleEvent("kill", "received", request->context(), param_.nodeID, request->kill().instanceid(),
                      "accepted", false, "", "", param_.endpointAddress, param_.nodeID);
    auto killFuture = DispatchReadyKill(param_, *request);
    if (killFuture.has_value()) {
        bool cancelled = false;
        auto killResponse = WaitFrontendResult(killFuture.value(), context, param_.invokeResultTimeoutMs,
                                               cancelled);
        if (!killResponse.IsSome() || !killResponse.Get().has_kill()) {
            SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      cancelled ? "frontend proxy kill cancelled with unknown dispatch outcome"
                                : "frontend proxy kill timed out with unknown dispatch outcome",
                      false, "post-dispatch-unknown");
            LogLifecycleEvent("kill", "terminal", request->context(), param_.nodeID,
                              request->kill().instanceid(),
                              cancelled ? "cancelled-unknown" : "timeout-unknown", false,
                              "post-dispatch-unknown", "", param_.endpointAddress, param_.nodeID);
            return ::grpc::Status::OK;
        }
        response->CopyFrom(killResponse.Get());
        const bool routeStale = response->kill().code() == common::ERR_INSTANCE_NOT_FOUND
                                && response->kill().message() == FRONTEND_KILL_ROUTE_STALE_MESSAGE;
        const bool ownerUnknown = response->kill().code() == common::ERR_INSTANCE_NOT_FOUND && !routeStale;
        SetStatus(response->mutable_status(), response->kill().code(), response->kill().message(), routeStale,
                  routeStale ? "route-stale" : (ownerUnknown ? "owner-unknown" : ""));
        LogLifecycleEvent("kill", "terminal", request->context(), param_.nodeID,
                          request->kill().instanceid(),
                          response->kill().code() == common::ERR_NONE ? "success" : "failed", routeStale,
                          routeStale ? "route-stale" : (ownerUnknown ? "owner-unknown" : ""), "",
                          param_.endpointAddress, param_.nodeID);
        return ::grpc::Status::OK;
    }
    if (param_.enableKillDispatch) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  FRONTEND_PROXY_KILL_READY_NOT_WIRED, false, "control-path-not-wired");
        return ::grpc::Status::OK;
    }
    SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
              FRONTEND_PROXY_CONTROL_NOT_WIRED, false, "control-path-not-wired");
    return ::grpc::Status::OK;
}

bool FrontendProxyService::HasRequiredContext(const ::frontend_proxy::FrontendRequestContext &context)
{
    return !context.frontendclientid().empty() && !context.requestid().empty();
}

bool FrontendProxyService::HasRequiredLifecycleContext(const ::frontend_proxy::FrontendRequestContext &context)
{
    return HasRequiredContext(context) && !context.tenantid().empty();
}

void FrontendProxyService::SetStatus(::frontend_proxy::FrontendProxyStatus *status, common::ErrorCode code,
                                     const std::string &message, bool retryable, const std::string &retryReason)
{
    if (status == nullptr) {
        return;
    }
    status->set_code(code);
    status->set_message(message);
    status->set_retryable(retryable);
    status->set_retryreason(retryReason);
}

}  // namespace functionsystem::local_scheduler
