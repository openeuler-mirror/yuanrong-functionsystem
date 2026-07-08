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

std::string BuildPendingKey(const std::string &frontendCallerID, const std::string &requestID)
{
    return frontendCallerID + "|" + requestID;
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
    static litebus::Future<SharedStreamMsg> Watch(const std::string &frontendCallerID, const std::string &requestID)
    {
        auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
        std::lock_guard<std::mutex> lock(Mutex());
        Pending()[BuildPendingKey(frontendCallerID, requestID)] = promise;
        return promise->GetFuture();
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

::grpc::Status FrontendProxyService::InvokeInstance(::grpc::ServerContext *,
                                                    const ::frontend_proxy::InvokeInstanceRequest *request,
                                                    ::frontend_proxy::InvokeInstanceResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy invoke args" };
    }
    if (!HasRequiredContext(request->context()) || request->invoke().instanceid().empty()) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke requires context.frontendClientID, context.requestID and invoke.instanceID");
        return ::grpc::Status::OK;
    }

    const auto requestID = request->invoke().requestid().empty() ? request->context().requestid()
                                                                 : request->invoke().requestid();
    const auto traceID = request->invoke().traceid().empty() ? request->context().traceid()
                                                             : request->invoke().traceid();
    const auto frontendCallerID = BuildFrontendCallerID(request->context());
    YRLOG_INFO("{}|frontend proxy invoke received by proxy({}), frontendClientID({}), instance({}), traceID({})",
               requestID, param_.nodeID, request->context().frontendclientid(), request->invoke().instanceid(), traceID);

    auto resultFuture = FrontendCallResultRegistry::Watch(frontendCallerID, requestID);
    auto invokeFuture = DispatchInvoke(param_, frontendCallerID, CreateInvokeRequest(*request));
    auto invokeResponse = invokeFuture.Get(param_.invokeResultTimeoutMs);
    if (!invokeResponse.IsSome()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_REQUEST_BETWEEN_RUNTIME_BUS,
                  "frontend proxy invoke timed out waiting for call response", true, "call-response-timeout");
        return ::grpc::Status::OK;
    }
    if (!invokeResponse.Get()->has_invokersp()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call response");
        return ::grpc::Status::OK;
    }

    response->mutable_invoke()->CopyFrom(invokeResponse.Get()->invokersp());
    if (invokeResponse.Get()->invokersp().code() != common::ERR_NONE) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), invokeResponse.Get()->invokersp().code(),
                  invokeResponse.Get()->invokersp().message(), true, "call-response-error");
        return ::grpc::Status::OK;
    }

    auto callResult = resultFuture.Get(param_.invokeResultTimeoutMs);
    if (!callResult.IsSome()) {
        FrontendCallResultRegistry::Cancel(frontendCallerID, requestID);
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke timed out waiting for call result");
        return ::grpc::Status::OK;
    }
    if (!callResult.Get()->has_callresultreq()) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call result");
        return ::grpc::Status::OK;
    }

    response->mutable_callresult()->CopyFrom(callResult.Get()->callresultreq());
    SetStatus(response->mutable_status(), callResult.Get()->callresultreq().code(),
              callResult.Get()->callresultreq().message());
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::CreateInstance(::grpc::ServerContext *,
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

    YRLOG_INFO("{}|frontend proxy create received by proxy({}), frontendClientID({}), function({})",
               request->context().requestid(), param_.nodeID, request->context().frontendclientid(),
               request->create().function());
    auto createFuture = DispatchReadyCreate(param_, *request);
    if (createFuture.has_value()) {
        auto createResponse = createFuture.value().Get(param_.invokeResultTimeoutMs);
        if (!createResponse.IsSome() || !createResponse.Get().has_create()) {
            SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      "frontend proxy create received invalid create response");
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

::grpc::Status FrontendProxyService::KillInstance(::grpc::ServerContext *,
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

    YRLOG_INFO("{}|frontend proxy kill received by proxy({}), frontendClientID({}), instance({})",
               request->context().requestid(), param_.nodeID, request->context().frontendclientid(),
               request->kill().instanceid());
    auto killFuture = DispatchReadyKill(param_, *request);
    if (killFuture.has_value()) {
        auto killResponse = killFuture.value().Get(param_.invokeResultTimeoutMs);
        if (!killResponse.IsSome() || !killResponse.Get().has_kill()) {
            SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      "frontend proxy kill received invalid kill response");
            return ::grpc::Status::OK;
        }
        response->CopyFrom(killResponse.Get());
        SetStatus(response->mutable_status(), response->kill().code(), response->kill().message());
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
