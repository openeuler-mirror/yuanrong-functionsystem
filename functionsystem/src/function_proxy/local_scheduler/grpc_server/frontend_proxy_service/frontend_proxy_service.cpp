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
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/logs/logging.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"

namespace functionsystem::local_scheduler {
namespace {
constexpr const char *FRONTEND_PROXY_CONTROL_NOT_WIRED =
    "frontend proxy create/kill control path is not wired";
constexpr const char *FRONTEND_PROXY_CREATE_READY_NOT_WIRED =
    "frontend proxy create requires ready create dispatcher; legacy stream create dispatcher is not allowed";
constexpr const char *FRONTEND_PROXY_KILL_READY_NOT_WIRED =
    "frontend proxy kill requires ready kill dispatcher; legacy stream kill dispatcher is not allowed";
constexpr const char *FRONTEND_CREATE_SOURCE_KEY = "source";
constexpr const char *FRONTEND_CREATE_SOURCE_VALUE = "frontend";
constexpr const char *FRONTEND_KILL_ROUTE_STALE_MESSAGE =
    "frontend proxy is not the owning proxy for this instance";
constexpr uint64_t FRONTEND_WAIT_POLL_MS = 20;
// Keep this aligned with meta_service's maximum function timeout (100 days).
constexpr int64_t MAX_FUNCTION_INVOKE_TIMEOUT_MS = 100LL * 24 * 60 * 60 * 1000;
constexpr const char *FUNCTION_PROXY_STREAM_CALLER_ID = "function-proxy";
constexpr size_t FRONTEND_EVENT_QUEUE_CAPACITY = 100;
constexpr size_t FRONTEND_EVENT_METADATA_SIZE = 16;
constexpr const char *FRONTEND_EVENT_EOF = "yuanrong_event_EOF";

// --- Workspace RPC 常量(方案 A) ---
// 默认 workspace 存储根目录;生产环境由 --workspace_storage_root flag 覆盖。
constexpr const char *DEFAULT_WORKSPACE_STORAGE_ROOT = "/home/agentos/.users";
// 默认 workspace 目录权限位(0700)。
constexpr mode_t DEFAULT_WORKSPACE_DIR_MODE = 0700;
// 默认 workspace 目录的 (uid, gid);生产环境由 flag 覆盖。
constexpr uid_t DEFAULT_WORKSPACE_UID = 1000;
constexpr gid_t DEFAULT_WORKSPACE_GID = 1000;
// workspace 权限位上限(rwxrwxrwx),超出视为非法 mode。
constexpr int WORKSPACE_MODE_MAX = 0777;
// nftw 递归删除时同时使用的文件描述符数量上限。
constexpr int WORKSPACE_NFTW_MAX_FDS = 16;
// nftw 失败路径记录缓冲区长度:thread_local 变量必须由 noexcept 构造初始化,
// 用定长字符数组替代 std::string,超长路径截断不影响排障定位。
constexpr int WORKSPACE_NFTW_PATH_MAX = 4096;
// workspace 父目录权限(rwxr-xr-x),仅叶子目录使用请求指定的 mode。
constexpr mode_t WORKSPACE_PARENT_DIR_MODE = 0755;
struct LifecycleEvent {
    const char *operation;
    const char *dispatchPhase;
    const ::frontend_proxy::FrontendRequestContext &context;
    std::string endpointNodeID;
    std::string instanceID;
    std::string outcome;
    bool retryable { false };
    std::string retryReason;
    std::string cleanupOutcome;
    std::string endpointAddress;
    std::string owningProxyID;
    uint32_t attempt { 1 };
    std::string transport { FrontendProxyService::lifecycleTransport };
    const FrontendKillCleanupSnapshot *cleanup { nullptr };
};

void LogLifecycleEventImpl(const LifecycleEvent &event)
{
    // Payload-free lifecycle event. Only fields known at this boundary are
    // emitted; transport authentication is outside this service and is not
    // implied by frontendClientID or tenant consistency checks.
    const auto requestTicket = event.cleanup == nullptr || !event.cleanup->requestTicketKnown
                                   ? "unknown"
                                   : (event.cleanup->requestTicketCleared ? "cleared" : "pending");
    const auto instanceTicket = event.cleanup == nullptr || !event.cleanup->instanceTicketKnown
                                    ? "unknown"
                                    : (event.cleanup->instanceTicketCleared ? "cleared" : "pending");
    const auto runtimeState = event.cleanup == nullptr ? "unknown" : event.cleanup->runtimeState;
    const auto instanceState = event.cleanup == nullptr ? "unknown" : event.cleanup->instanceState;
    const auto pendingInvokeCount = event.cleanup == nullptr ? -1 : event.cleanup->pendingInvokeCount;
    YRLOG_INFO("frontend_lifecycle operation={} dispatchPhase={} frontendClientID={} tenantID={} requestID={} "
               "traceID={} endpointNodeID={} endpointAddress={} owningProxyID={} instanceID={} attempt={} "
               "transport={} outcome={} retryable={} retryReason={} cleanupOutcome={} cleanupRequestTicket={} "
               "cleanupInstanceTicket={} cleanupRuntimeState={} cleanupInstanceState={} pendingInvokeCount={}",
               event.operation, event.dispatchPhase, event.context.frontendclientid(), event.context.tenantid(),
               event.context.requestid(), event.context.traceid(), event.endpointNodeID, event.endpointAddress,
               event.owningProxyID, event.instanceID, event.attempt, event.transport, event.outcome, event.retryable,
               event.retryReason, event.cleanupOutcome, requestTicket, instanceTicket, runtimeState, instanceState,
               pendingInvokeCount);
}

#define LogLifecycleEvent(...) LogLifecycleEventImpl(LifecycleEvent{ __VA_ARGS__ })

template <typename T>
litebus::Option<T> WaitFrontendResult(const litebus::Future<T> &future, ::grpc::ServerContext *context,
                                      uint64_t timeoutMs, bool &cancelled)
{
    const auto started = std::chrono::steady_clock::now();
    uint64_t elapsed = 0;
    while (elapsed < timeoutMs) {
        if (context != nullptr && context->IsCancelled()) {
            cancelled = true;
            return litebus::None();
        }
        auto result = future.Get(std::min(FRONTEND_WAIT_POLL_MS, timeoutMs - elapsed));
        if (result.IsSome()) {
            return result;
        }
        elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    }
    return litebus::None();
}

template <typename T>
litebus::Option<T> WaitFrontendResult(const litebus::Future<T> &future, ::grpc::ServerContext *context,
                                      std::chrono::milliseconds timeout, bool &cancelled)
{
    return WaitFrontendResult(future, context, static_cast<uint64_t>(timeout.count()), cancelled);
}

std::chrono::steady_clock::time_point DeadlineAfter(std::chrono::milliseconds timeout)
{
    const auto now = std::chrono::steady_clock::now();
    const auto maxTimeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - now);
    return now + std::min(timeout, maxTimeout);
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

std::string BuildPendingKey(const std::string &instanceID, const std::string &requestID)
{
    return instanceID + "\x1f" + requestID;
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

namespace frontend_call_result_registry {
    struct PendingCall {
        std::shared_ptr<litebus::Promise<SharedStreamMsg>> promise;
        std::string instanceID;
    };

    struct WatchResult {
        bool registered;
        litebus::Future<SharedStreamMsg> future;
    };

    std::mutex &Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, PendingCall> &Pending()
    {
        static std::unordered_map<std::string, PendingCall> pending;
        return pending;
    }

    WatchResult Watch(const std::string &requestID, const std::string &instanceID)
    {
        auto promise = std::make_shared<litebus::Promise<SharedStreamMsg>>();
        std::lock_guard<std::mutex> lock(Mutex());
        const bool inserted = Pending().emplace(BuildPendingKey(instanceID, requestID),
                                                PendingCall{ promise, instanceID }).second;
        return { inserted, promise->GetFuture() };
    }

    void Cancel(const std::string &requestID, const std::string &instanceID)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        (void)Pending().erase(BuildPendingKey(instanceID, requestID));
    }

    std::pair<bool, SharedStreamMsg> Receive(const std::string &from, const SharedStreamMsg &request)
    {
        if (request == nullptr || !request->has_callresultreq()) {
            return { false, nullptr };
        }
        const auto &callResult = request->callresultreq();
        std::shared_ptr<litebus::Promise<SharedStreamMsg>> promise;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            // `from` is the authenticated runtime stream identity. For an SSE
            // invoke the logical caller is function-proxy, so CallResult's
            // instanceid may name that destination instead of the runtime that
            // produced the result.
            auto iter = Pending().find(BuildPendingKey(from, callResult.requestid()));
            if (iter == Pending().end()) {
                return { false, nullptr };
            }
            promise = iter->second.promise;
            (void)Pending().erase(iter);
        }
        promise->SetValue(request);
        return { true, CreateCallResultAck(common::ERR_NONE, "success", request->messageid()) };
    }

    int64_t PendingCount(const std::string &instanceID)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return static_cast<int64_t>(std::count_if(Pending().begin(), Pending().end(),
            [&instanceID](const auto &entry) { return entry.second.instanceID == instanceID; }));
    }

}  // namespace frontend_call_result_registry

namespace frontend_event_registry {
    struct PendingStream {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::string> events;
        bool eventComplete { false };
        bool closed { false };
    };

    std::mutex &Mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unordered_map<std::string, std::shared_ptr<PendingStream>> &Pending()
    {
        static std::unordered_map<std::string, std::shared_ptr<PendingStream>> pending;
        return pending;
    }

    std::shared_ptr<PendingStream> Watch(const std::string &requestID)
    {
        auto stream = std::make_shared<PendingStream>();
        std::lock_guard<std::mutex> lock(Mutex());
        if (!Pending().emplace(requestID, stream).second) {
            return nullptr;
        }
        return stream;
    }

    void Cancel(const std::string &requestID)
    {
        std::shared_ptr<PendingStream> stream;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            auto iter = Pending().find(requestID);
            if (iter == Pending().end()) {
                return;
            }
            stream = iter->second;
            (void)Pending().erase(iter);
        }
        {
            std::lock_guard<std::mutex> lock(stream->mutex);
            stream->closed = true;
        }
        stream->changed.notify_all();
    }

    bool Receive(const std::string &, const SharedStreamMsg &request)
    {
        if (request == nullptr || !request->has_eventreq()) {
            return false;
        }
        std::shared_ptr<PendingStream> stream;
        {
            std::lock_guard<std::mutex> lock(Mutex());
            auto iter = Pending().find(request->eventreq().requestid());
            if (iter == Pending().end()) {
                return false;
            }
            stream = iter->second;
        }
        {
            std::unique_lock<std::mutex> lock(stream->mutex);
            if (stream->closed) {
                return false;
            }
            const auto &framedMessage = request->eventreq().message();
            if (framedMessage.size() < FRONTEND_EVENT_METADATA_SIZE) {
                return false;
            }
            auto event = framedMessage.substr(FRONTEND_EVENT_METADATA_SIZE);
            if (event == FRONTEND_EVENT_EOF) {
                stream->eventComplete = true;
            } else {
                stream->changed.wait(lock, [&]() {
                    return stream->events.size() < FRONTEND_EVENT_QUEUE_CAPACITY || stream->closed;
                });
                if (stream->closed) {
                    return false;
                }
                stream->events.emplace_back(std::move(event));
            }
        }
        stream->changed.notify_all();
        return true;
    }
}  // namespace frontend_event_registry

bool ForwardFrontendEvents(
    ::grpc::ServerContext *context,
    ::grpc::ServerWriter<::frontend_proxy::InvokeInstanceStreamResponse> *writer,
    const std::shared_ptr<frontend_event_registry::PendingStream> &pending,
    const std::atomic<bool> &dispatchComplete,
    const ::frontend_proxy::InvokeInstanceResponse &finalResponse)
{
    bool writeOK = true;
    bool observedEventComplete = false;
    while (writeOK && !context->IsCancelled()) {
        std::deque<std::string> events;
        bool eventComplete = false;
        {
            std::unique_lock<std::mutex> lock(pending->mutex);
            pending->changed.wait_for(lock, std::chrono::milliseconds(FRONTEND_WAIT_POLL_MS), [&]() {
                return !pending->events.empty() || pending->eventComplete != observedEventComplete
                       || context->IsCancelled();
            });
            events.swap(pending->events);
            eventComplete = pending->eventComplete;
            observedEventComplete = eventComplete;
        }
        pending->changed.notify_all();
        for (auto &event : events) {
            ::frontend_proxy::InvokeInstanceStreamResponse frame;
            frame.set_event(std::move(event));
            if (!writer->Write(frame)) {
                writeOK = false;
                break;
            }
        }
        if (dispatchComplete.load(std::memory_order_acquire)
            && (eventComplete || finalResponse.status().code() != common::ERR_NONE)) {
            break;
        }
    }
    return writeOK;
}

struct KillCleanupObservation {
    FrontendKillCleanupSnapshot snapshot;
    std::string outcome { "probe-not-configured" };
};

KillCleanupObservation ObserveKillCleanup(const FrontendProxyServiceParam &param, ::grpc::ServerContext *context,
                                          const std::string &requestID, const std::string &instanceID,
                                          bool killSucceeded)
{
    KillCleanupObservation observation;
    observation.snapshot.pendingInvokeCount = frontend_call_result_registry::PendingCount(instanceID);
    if (!param.killCleanupProbe) {
        return observation;
    }

    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started
           < std::chrono::milliseconds(param.killCleanupTimeoutMs)) {
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsed >= param.killCleanupTimeoutMs) {
            observation.outcome = "incomplete-timeout";
            return observation;
        }
        bool cancelled = false;
        auto snapshot = WaitFrontendResult(param.killCleanupProbe(requestID, instanceID), context,
                                           param.killCleanupTimeoutMs - elapsed, cancelled);
        if (!snapshot.IsSome()) {
            observation.outcome = cancelled ? "cancelled" : "probe-timeout";
            return observation;
        }
        observation.snapshot = snapshot.Get();
        observation.snapshot.pendingInvokeCount = frontend_call_result_registry::PendingCount(instanceID);
        if (!killSucceeded) {
            observation.outcome = "kill-failed";
            return observation;
        }
        if (observation.snapshot.IsComplete()) {
            observation.outcome = "complete";
            return observation;
        }
        const auto elapsedAfterProbe = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        if (elapsedAfterProbe >= param.killCleanupTimeoutMs) {
            observation.outcome = "incomplete-timeout";
            return observation;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::min(FRONTEND_WAIT_POLL_MS, param.killCleanupTimeoutMs - elapsedAfterProbe)));
    }
    observation.outcome = "incomplete-timeout";
    return observation;
}

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
    InvocationHandler::RegisterFrontendCallResultReceiver(frontend_call_result_registry::Receive);
    InvocationHandler::RegisterFrontendEventReceiver(frontend_event_registry::Receive);
}

bool FrontendProxyService::HasAuthenticatedPeer(const ::grpc::ServerContext *context) const
{
    if (!param_.requireAuthenticatedPeer) {
        return true;
    }
    if (context == nullptr) {
        return false;
    }
    const auto authContext = context->auth_context();
    return authContext != nullptr && authContext->IsPeerAuthenticated();
}

::grpc::Status FrontendProxyService::InvokeInstance(::grpc::ServerContext *context,
                                                    const ::frontend_proxy::InvokeInstanceRequest *request,
                                                    ::frontend_proxy::InvokeInstanceResponse *response)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy invoke requires an authenticated component certificate" };
    }
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy invoke args" };
    }
    if (!ValidateInvokeRequest(*request, *response)) {
        return ::grpc::Status::OK;
    }
    return DispatchInvokeRequest(context, *request, *response);
}

::grpc::Status FrontendProxyService::InvokeInstanceStream(
    ::grpc::ServerContext *context, const ::frontend_proxy::InvokeInstanceRequest *request,
    ::grpc::ServerWriter<::frontend_proxy::InvokeInstanceStreamResponse> *writer)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy invoke stream requires an authenticated component certificate" };
    }
    if (request == nullptr || writer == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy invoke stream args" };
    }

    ::frontend_proxy::InvokeInstanceResponse finalResponse;
    if (!ValidateInvokeRequest(*request, finalResponse)) {
        ::frontend_proxy::InvokeInstanceStreamResponse frame;
        frame.mutable_final()->CopyFrom(finalResponse);
        (void)writer->Write(frame);
        return ::grpc::Status::OK;
    }

    const auto requestID = request->invoke().requestid().empty() ? request->context().requestid()
                                                                : request->invoke().requestid();
    auto pending = frontend_event_registry::Watch(requestID);
    if (pending == nullptr) {
        SetStatus(finalResponse.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke stream requires a globally unique request id");
        ::frontend_proxy::InvokeInstanceStreamResponse frame;
        frame.mutable_final()->CopyFrom(finalResponse);
        (void)writer->Write(frame);
        return ::grpc::Status::OK;
    }

    std::atomic<bool> dispatchComplete { false };
    std::thread dispatchThread([this, context, request, &finalResponse, &dispatchComplete, pending]() {
        (void)DispatchInvokeRequest(context, *request, finalResponse);
        dispatchComplete.store(true, std::memory_order_release);
        pending->changed.notify_all();
    });

    const bool writeOK = ForwardFrontendEvents(context, writer, pending, dispatchComplete, finalResponse);

    frontend_event_registry::Cancel(requestID);
    if (dispatchThread.joinable()) {
        dispatchThread.join();
    }
    if (writeOK && !context->IsCancelled()) {
        ::frontend_proxy::InvokeInstanceStreamResponse frame;
        frame.mutable_final()->CopyFrom(finalResponse);
        (void)writer->Write(frame);
    }
    return ::grpc::Status::OK;
}

bool FrontendProxyService::ValidateInvokeRequest(const ::frontend_proxy::InvokeInstanceRequest &request,
                                                 ::frontend_proxy::InvokeInstanceResponse &response) const
{
    if (!HasRequiredLifecycleContext(request.context()) || request.invoke().instanceid().empty()) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke requires context.frontendClientID, context.requestID, context.tenantID and "
                  "invoke.instanceID");
        return false;
    }
    if (HasContextTenantMismatch(request.context())) {
        SetStatus(response.mutable_status(), common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy invoke tenant does not match context labels");
        return false;
    }
    if (param_.invokeTenantAuthorizer
        && !param_.invokeTenantAuthorizer(request.context().tenantid(), request.invoke().instanceid())) {
        SetStatus(response.mutable_status(), common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy invoke tenant does not own target instance");
        return false;
    }
    if (HasOperationRequestIDMismatch(request.context(), request.invoke().requestid())) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke request id does not match context request id");
        return false;
    }
    return true;
}

::grpc::Status FrontendProxyService::DispatchInvokeRequest(
    ::grpc::ServerContext *context, const ::frontend_proxy::InvokeInstanceRequest &request,
    ::frontend_proxy::InvokeInstanceResponse &response)
{
    const auto totalTimeout = ResolveInvokeResultTimeout(request, param_);
    const auto waitDeadline = DeadlineAfter(totalTimeout);
    const auto requestID = request.invoke().requestid().empty() ? request.context().requestid()
                                                               : request.invoke().requestid();
    const auto traceID = request.invoke().traceid().empty() ? request.context().traceid() : request.invoke().traceid();
    YRLOG_INFO("{}|frontend proxy invoke received by proxy({}), frontendClientID({}), instance({}), traceID({})",
               requestID, param_.nodeID, request.context().frontendclientid(), request.invoke().instanceid(), traceID);
    LogLifecycleEvent("invoke", "received", request.context(), param_.nodeID, request.invoke().instanceid(),
                      "accepted", false, "", "", param_.endpointAddress, param_.nodeID);
    auto watchResult = frontend_call_result_registry::Watch(requestID, request.invoke().instanceid());
    if (!watchResult.registered) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy invoke requires a globally unique request id");
        return ::grpc::Status::OK;
    }
    // The Call sender is also the destination used by runtime EventAsync.
    // Direct frontend invokes therefore use the local FunctionProxy's stream
    // identity so SSE data and the legacy EOF event return to this service.
    auto invokeFuture = DispatchInvoke(param_, FUNCTION_PROXY_STREAM_CALLER_ID, CreateInvokeRequest(request));
    bool invokeCancelled = false;
    auto invokeResponse = WaitFrontendResult(invokeFuture, context, totalTimeout, invokeCancelled);
    if (!invokeResponse.IsSome()) {
        frontend_call_result_registry::Cancel(requestID, request.invoke().instanceid());
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  invokeCancelled ? "frontend proxy invoke cancelled with unknown dispatch outcome"
                                  : "frontend proxy invoke timed out with unknown dispatch outcome",
                  false, "post-dispatch-unknown");
        LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                          invokeCancelled ? "cancelled-unknown" : "timeout-unknown",
                          false, "post-dispatch-unknown", "result-waiter-unregistered", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }
    if (!invokeResponse.Get()->has_invokersp()) {
        frontend_call_result_registry::Cancel(requestID, request.invoke().instanceid());
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call response");
        LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                          "invalid-response", false, "", "result-waiter-unregistered",
                          param_.endpointAddress, param_.nodeID);
        return ::grpc::Status::OK;
    }
    response.mutable_invoke()->CopyFrom(invokeResponse.Get()->invokersp());
    if (invokeResponse.Get()->invokersp().code() != common::ERR_NONE) {
        frontend_call_result_registry::Cancel(requestID, request.invoke().instanceid());
        SetStatus(response.mutable_status(), invokeResponse.Get()->invokersp().code(),
                  invokeResponse.Get()->invokersp().message(), true, "call-response-error");
        LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                          "dispatch-failed", true, "call-response-error",
                          "result-waiter-unregistered", param_.endpointAddress, param_.nodeID);
        return ::grpc::Status::OK;
    }
    return AwaitInvokeResult(context, request, response, watchResult.future, waitDeadline);
}

::grpc::Status FrontendProxyService::AwaitInvokeResult(
    ::grpc::ServerContext *context, const ::frontend_proxy::InvokeInstanceRequest &request,
    ::frontend_proxy::InvokeInstanceResponse &response, const litebus::Future<SharedStreamMsg> &resultFuture,
    const std::chrono::steady_clock::time_point &waitDeadline)
{
    const auto requestID = request.invoke().requestid().empty() ? request.context().requestid()
                                                               : request.invoke().requestid();
    const auto remainingMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(waitDeadline - std::chrono::steady_clock::now()).count();
    const auto remainingTimeoutMs = remainingMs > 0 ? static_cast<uint64_t>(remainingMs) : 0;
    bool resultCancelled = false;
    auto callResult = WaitFrontendResult(resultFuture, context, remainingTimeoutMs, resultCancelled);
    if (!callResult.IsSome()) {
        frontend_call_result_registry::Cancel(requestID, request.invoke().instanceid());
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  resultCancelled ? "frontend proxy invoke cancelled while result outcome is unknown"
                                  : "frontend proxy invoke timed out while result outcome is unknown",
                  false, "post-dispatch-unknown");
        LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                          resultCancelled ? "cancelled-unknown" : "timeout-unknown",
                          false, "post-dispatch-unknown", "result-waiter-unregistered", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }
    if (!callResult.Get()->has_callresultreq()) {
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy invoke received invalid call result");
        LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                          "invalid-result", false, "", "", param_.endpointAddress,
                          param_.nodeID);
        return ::grpc::Status::OK;
    }
    response.mutable_callresult()->CopyFrom(callResult.Get()->callresultreq());
    // A valid CallResult means the frontend-to-proxy operation completed. Its
    // code belongs to runtime/function execution and must remain in callResult.
    SetStatus(response.mutable_status(), common::ERR_NONE, "");
    LogLifecycleEvent("invoke", "terminal", request.context(), param_.nodeID, request.invoke().instanceid(),
                      callResult.Get()->callresultreq().code() == common::ERR_NONE ? "success" : "failed", false,
                      "", "", param_.endpointAddress, param_.nodeID);
    return ::grpc::Status::OK;
}

std::chrono::milliseconds FrontendProxyService::ResolveInvokeResultTimeout(
    const ::frontend_proxy::InvokeInstanceRequest &request, const FrontendProxyServiceParam &param)
{
    const auto maxFunctionTimeoutMs = static_cast<uint64_t>(MAX_FUNCTION_INVOKE_TIMEOUT_MS);
    const auto functionTimeoutMs = request.invoketimeoutms() <= 0
                                       ? std::min(param.invokeResultTimeoutMs, maxFunctionTimeoutMs)
                                       : static_cast<uint64_t>(
                                             std::min(request.invoketimeoutms(), MAX_FUNCTION_INVOKE_TIMEOUT_MS));
    const auto maxBufferMs = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - functionTimeoutMs;
    const auto bufferMs = std::min(param.invokeResultTimeoutBufferMs, maxBufferMs);
    return std::chrono::milliseconds(static_cast<int64_t>(functionTimeoutMs + bufferMs));
}

::grpc::Status FrontendProxyService::CreateInstance(::grpc::ServerContext *context,
                                                    const ::frontend_proxy::CreateInstanceRequest *request,
                                                    ::frontend_proxy::CreateInstanceResponse *response)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy create requires an authenticated component certificate" };
    }
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy create args" };
    }
    if (!ValidateCreateRequest(*request, *response)) {
        return ::grpc::Status::OK;
    }
    return DispatchCreateRequest(context, *request, *response);
}

bool FrontendProxyService::ValidateCreateRequest(const ::frontend_proxy::CreateInstanceRequest &request,
                                                 ::frontend_proxy::CreateInstanceResponse &response) const
{
    if (!HasRequiredLifecycleContext(request.context()) || request.create().function().empty()) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create requires context.frontendClientID, context.requestID, context.tenantID and "
                  "create.function");
        return false;
    }
    const auto &createOptions = request.create().createoptions();
    const auto sourceIter = createOptions.find(FRONTEND_CREATE_SOURCE_KEY);
    if (sourceIter == createOptions.end() || sourceIter->second != FRONTEND_CREATE_SOURCE_VALUE) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create requires create.createOptions source=frontend");
        return false;
    }
    if (HasCreateTenantMismatch(request)) {
        SetStatus(response.mutable_status(), common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy create tenant does not match create options");
        return false;
    }
    if (HasOperationRequestIDMismatch(request.context(), request.create().requestid())) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy create request id does not match context request id");
        return false;
    }
    return true;
}

::grpc::Status FrontendProxyService::DispatchCreateRequest(
    ::grpc::ServerContext *context, const ::frontend_proxy::CreateInstanceRequest &request,
    ::frontend_proxy::CreateInstanceResponse &response)
{
    YRLOG_INFO("{}|frontend proxy create received by proxy({}), frontendClientID({}), function({})",
               request.context().requestid(), param_.nodeID, request.context().frontendclientid(),
               request.create().function());
    LogLifecycleEvent("create", "received", request.context(), param_.nodeID, "", "accepted", false, "", "",
                      param_.endpointAddress, param_.nodeID);
    auto createFuture = DispatchReadyCreate(param_, request);
    if (createFuture.has_value()) {
        bool cancelled = false;
        auto createResponse = WaitFrontendResult(createFuture.value(), context, param_.invokeResultTimeoutMs,
                                                 cancelled);
        if (!createResponse.IsSome() || !createResponse.Get().has_create()) {
            if (param_.createWaitCanceller) {
                param_.createWaitCanceller(request.context().requestid(),
                                           cancelled ? "grpc client cancelled" : "frontend create timed out");
            }
            SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      cancelled ? "frontend proxy create cancelled while schedule outcome is unknown"
                                : "frontend proxy create timed out while schedule outcome is unknown",
                      false, "post-dispatch-unknown");
            LogLifecycleEvent("create", "terminal", request.context(), param_.nodeID, "",
                              cancelled ? "cancelled-unknown" : "timeout-unknown", false,
                              "post-dispatch-unknown", "ready-waiter-unregistered", param_.endpointAddress,
                              param_.nodeID);
            return ::grpc::Status::OK;
        }
        response.CopyFrom(createResponse.Get());
        return FinalizeCreateResponse(request, response);
    }
    if (param_.enableCreateDispatch) {
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  FRONTEND_PROXY_CREATE_READY_NOT_WIRED, false, "control-path-not-wired");
        return ::grpc::Status::OK;
    }
    // Frontend unary create must not reuse the old Posix create boundary yet:
    // the raw libruntime create contract returns the final NotifyRequest, while
    // Posix create only returns the immediate scheduler CreateResponse and its
    // caller argument is also interpreted as parent/sender runtime identity.
    // Keep this explicit failure for an incompletely wired proxy deployment.
    // Frontend must not fall back to libruntime.
    SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
              FRONTEND_PROXY_CONTROL_NOT_WIRED, false, "control-path-not-wired");
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::FinalizeCreateResponse(
    const ::frontend_proxy::CreateInstanceRequest &request, ::frontend_proxy::CreateInstanceResponse &response)
{
    const auto &createRsp = response.create();
    if (createRsp.code() == common::ERR_NONE && createRsp.instanceid().empty()) {
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "frontend proxy create response missing instance id");
        return ::grpc::Status::OK;
    }
    const auto owningProxyID = response.has_callresult() && response.callresult().has_runtimeinfo()
                                   ? response.callresult().runtimeinfo().proxyid()
                                   : "";
    if (createRsp.code() == common::ERR_NONE) {
        // async create returns before ready: owningProxyID is empty, fall back
        // to param_.nodeID so frontend records routeOnlyInstance for DELETE/kill.
        if (owningProxyID.empty()) {
            YRLOG_WARN("{}|frontend create response missing owning proxyID, routeaddress falls back to local node({})",
                       request.context().requestid(), param_.nodeID);
        }
        response.set_routeaddress(owningProxyID.empty() ? param_.nodeID : owningProxyID);
    }
    SetStatus(response.mutable_status(), createRsp.code(), createRsp.message());
    const auto routeAddress = response.routeaddress();
    LogLifecycleEvent("create", "terminal", request.context(), param_.nodeID, createRsp.instanceid(),
                      createRsp.code() == common::ERR_NONE ? "success" : "failed", false, "", "", routeAddress,
                      owningProxyID);
    if (response.has_callresult()) {
        LogLifecycleEvent(FrontendProxyService::readyOperation, "terminal", request.context(), param_.nodeID,
                          createRsp.instanceid(),
                          response.callresult().code() == common::ERR_NONE ? "success" : "failed", false, "", "",
                          routeAddress, owningProxyID);
    }
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::KillInstance(::grpc::ServerContext *context,
                                                  const ::frontend_proxy::KillInstanceRequest *request,
                                                  ::frontend_proxy::KillInstanceResponse *response)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy kill requires an authenticated component certificate" };
    }
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy kill args" };
    }
    if (!ValidateKillRequest(*request, *response)) {
        return ::grpc::Status::OK;
    }
    return DispatchKillRequest(context, *request, *response);
}

bool FrontendProxyService::ValidateKillRequest(const ::frontend_proxy::KillInstanceRequest &request,
                                               ::frontend_proxy::KillInstanceResponse &response) const
{
    if (!HasRequiredLifecycleContext(request.context()) || request.kill().instanceid().empty()) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy kill requires context.frontendClientID, context.requestID, context.tenantID and "
                  "kill.instanceID");
        return false;
    }
    if (HasOperationRequestIDMismatch(request.context(), request.kill().requestid())) {
        SetStatus(response.mutable_status(), common::ERR_PARAM_INVALID,
                  "frontend proxy kill request id does not match context request id");
        return false;
    }
    return true;
}

::grpc::Status FrontendProxyService::DispatchKillRequest(
    ::grpc::ServerContext *context, const ::frontend_proxy::KillInstanceRequest &request,
    ::frontend_proxy::KillInstanceResponse &response)
{
    YRLOG_INFO("{}|frontend proxy kill received by proxy({}), frontendClientID({}), instance({})",
               request.context().requestid(), param_.nodeID, request.context().frontendclientid(),
               request.kill().instanceid());
    LogLifecycleEvent("kill", "received", request.context(), param_.nodeID, request.kill().instanceid(),
                      "accepted", false, "", "", param_.endpointAddress, param_.nodeID);
    auto killFuture = DispatchReadyKill(param_, request);
    if (killFuture.has_value()) {
        bool cancelled = false;
        auto killResponse = WaitFrontendResult(killFuture.value(), context, param_.invokeResultTimeoutMs,
                                               cancelled);
        if (!killResponse.IsSome() || !killResponse.Get().has_kill()) {
            SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                      cancelled ? "frontend proxy kill cancelled with unknown dispatch outcome"
                                : "frontend proxy kill timed out with unknown dispatch outcome",
                      false, "post-dispatch-unknown");
            LogLifecycleEvent("kill", "terminal", request.context(), param_.nodeID, request.kill().instanceid(),
                              cancelled ? "cancelled-unknown" : "timeout-unknown", false,
                              "post-dispatch-unknown", "dispatch-outcome-unknown", param_.endpointAddress,
                              param_.nodeID);
            return ::grpc::Status::OK;
        }
        response.CopyFrom(killResponse.Get());
        const bool routeStale = response.kill().code() == common::ERR_INSTANCE_NOT_FOUND
                                && response.kill().message() == FRONTEND_KILL_ROUTE_STALE_MESSAGE;
        const bool ownerUnknown = response.kill().code() == common::ERR_INSTANCE_NOT_FOUND && !routeStale;
        SetStatus(response.mutable_status(), response.kill().code(), response.kill().message(), routeStale,
                  routeStale ? "route-stale" : (ownerUnknown ? "owner-unknown" : ""));
        const auto cleanup = ObserveKillCleanup(
            param_, context, request.context().requestid(), request.kill().instanceid(),
            response.kill().code() == common::ERR_NONE);
        LogLifecycleEvent("kill", "terminal", request.context(), param_.nodeID, request.kill().instanceid(),
                          response.kill().code() == common::ERR_NONE ? "success" : "failed", routeStale,
                          routeStale ? "route-stale" : (ownerUnknown ? "owner-unknown" : ""), cleanup.outcome,
                          param_.endpointAddress, param_.nodeID, 1, FrontendProxyService::lifecycleTransport,
                          &cleanup.snapshot);
        return ::grpc::Status::OK;
    }
    if (param_.enableKillDispatch) {
        SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  FRONTEND_PROXY_KILL_READY_NOT_WIRED, false, "control-path-not-wired");
        return ::grpc::Status::OK;
    }
    SetStatus(response.mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
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

// --- 方案 A: Workspace RPC 实现 ---

std::string FrontendProxyService::ResolveWorkspaceStorageRoot() const
{
    if (param_.workspaceStorageRootProvider) {
        auto root = param_.workspaceStorageRootProvider();
        if (!root.empty()) {
            return root;
        }
    }
    return DEFAULT_WORKSPACE_STORAGE_ROOT;
}

std::pair<uid_t, gid_t> FrontendProxyService::ResolveWorkspaceUidGid() const
{
    if (param_.workspaceUidGidProvider) {
        auto ids = param_.workspaceUidGidProvider();
        // uid/gid 为 0 也允许(root 场景),只校验 uid 不为非法值
        return ids;
    }
    return { DEFAULT_WORKSPACE_UID, DEFAULT_WORKSPACE_GID };
}

std::string FrontendProxyService::ValidateWorkspaceOwner(const std::string &owner)
{
    if (owner.empty()) {
        return "owner is required";
    }
    // 拒绝内嵌 NUL:libc 路径 API 以 NUL 截断,内嵌 NUL 会导致校验与实际操作的目标路径不一致。
    if (owner.find('\0') != std::string::npos) {
        return "owner must not contain embedded NUL";
    }
    if (owner.find('/') != std::string::npos) {
        return "owner must not contain '/': " + owner;
    }
    if (owner == ".." || owner.find("..") != std::string::npos) {
        return "owner must not be a path traversal: " + owner;
    }
    if (owner.front() == '.') {
        return "owner must not start with '.': " + owner;
    }
    return "";
}

int FrontendProxyService::ParseWorkspaceMode(const std::string &mode)
{
    if (mode.empty()) {
        return static_cast<int>(DEFAULT_WORKSPACE_DIR_MODE);
    }
    // 支持 "0700" / "755" 等八进制字符串
    try {
        std::size_t pos = 0;
        int parsed = std::stoi(mode, &pos, 8);
        if (pos != mode.size() || parsed < 0 || parsed > WORKSPACE_MODE_MAX) {
            return -1;
        }
        return parsed;
    } catch (...) {
        return -1;
    }
}

// JoinPath 拼接 storage_root + "/" + owner,去掉 storage_root 尾部多余的 "/"。
static std::string JoinWorkspacePath(const std::string &storageRoot, const std::string &owner)
{
    std::string root = storageRoot;
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    return root + "/" + owner;
}

// ValidateWorkspaceCall 组合校验 workspace 请求:
//   1. 请求身份:context.frontendClientID + context.requestID 必填(与 invoke/create/kill 一致)。
//      调用方契约(handler.go)不传 tenantID,故不强制 tenantid 非空。
//   2. 租户一致性:labels 中显式携带的租户键(tenantID/tenantId/tenant)必须与 context.tenantid() 一致,
//      防止调用方携带自相矛盾的租户信息绕过基于租户的审计。
//   3. owner 合法性:防路径穿越与 NUL 截断。
//   4. owner 归属授权:若注入 workspaceOwnerAuthorizer,校验调用租户是否有权操作该 owner;
//      未注入时信任 frontend 侧已做 owner 派生与绑定(单租户部署前提,见 .h 注释)。
bool FrontendProxyService::ValidateWorkspaceCall(const ::frontend_proxy::FrontendRequestContext *context,
                                                 const std::string &owner,
                                                 ::frontend_proxy::FrontendProxyStatus *status) const
{
    if (context == nullptr || !HasRequiredContext(*context)) {
        SetStatus(status, common::ERR_PARAM_INVALID,
                  "frontend proxy workspace requires context.frontendClientID and context.requestID");
        return false;
    }
    if (HasContextTenantMismatch(*context)) {
        SetStatus(status, common::ERR_PARAM_INVALID, "frontend proxy workspace tenant does not match context labels");
        return false;
    }
    const auto ownerErr = ValidateWorkspaceOwner(owner);
    if (!ownerErr.empty()) {
        SetStatus(status, common::ERR_PARAM_INVALID, ownerErr);
        return false;
    }
    if (param_.workspaceOwnerAuthorizer && !param_.workspaceOwnerAuthorizer(context->tenantid(), owner)) {
        SetStatus(status, common::ERR_AUTHORIZE_FAILED,
                  "frontend proxy workspace owner is not authorized for caller tenant");
        return false;
    }
    return true;
}

// MkdirSegment 尝试创建单级目录;已存在(EEXIST)时用 lstat 校验其为真实目录,
// 拒绝符号链接/普通文件冒充目录,防止后续 chmod/chown 落到符号链接目标上。
static std::string MkdirSegment(const std::string &path, mode_t mode)
{
    if (::mkdir(path.c_str(), mode) == 0) {
        return "";
    }
    if (errno != EEXIST) {
        return "mkdir failed for " + path + ": " + std::strerror(errno);
    }
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        return "stat failed for " + path + ": " + std::strerror(errno);
    }
    if (!S_ISDIR(st.st_mode)) {
        return "path exists but is not a directory: " + path;
    }
    return "";
}

// MakeWorkspaceDirs 沿路径逐级创建目录(类似 mkdir -p),每级允许已存在。
static std::string MakeWorkspaceDirs(const std::string &path, mode_t mode)
{
    if (path.empty() || path == "/") {
        return "";
    }
    std::string current;
    current.reserve(path.size());
    size_t pos = 0;
    if (path.front() == '/') {
        current.push_back('/');
        pos = 1;
    }
    while (pos < path.size()) {
        auto next = path.find('/', pos);
        if (next == std::string::npos) {
            next = path.size();
        }
        if (next > pos) {
            current.append(path, pos, next - pos);
            const auto err = MkdirSegment(current, mode);
            if (!err.empty()) {
                return err;
            }
        }
        if (next < path.size()) {
            current.push_back('/');
        }
        pos = next + 1;
    }
    return "";
}

// CreateWorkspaceDirectory 创建 workspace 目录:父目录按 WORKSPACE_PARENT_DIR_MODE 逐级创建,
// 叶子目录按请求的 mode 创建;已存在的路径必须是真实目录(幂等)。
static std::string CreateWorkspaceDirectory(const std::string &targetPath, mode_t leafMode)
{
    const auto lastSlash = targetPath.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        const auto parentErr = MakeWorkspaceDirs(targetPath.substr(0, lastSlash), WORKSPACE_PARENT_DIR_MODE);
        if (!parentErr.empty()) {
            return parentErr;
        }
    }
    return MkdirSegment(targetPath, leafMode);
}

// ApplyWorkspaceOwnership 对叶子目录执行 chmod + chown,确保权限位与归属和请求一致(幂等)。
// 先用 open(O_NOFOLLOW|O_DIRECTORY) 拿到目录 fd 再 fchmod/fchown:
//   1) 符号链接在 open 处被拒(O_NOFOLLOW → ELOOP),mode/uid/gid 不会作用到链接目标;
//   2) fd 绑定 open 时刻的真实目录 inode,消除 lstat 校验与 chmod/chown 之间目录
//      被并发替换为符号链接的竞态窗口。
static std::string ApplyWorkspaceOwnership(const std::string &path, mode_t mode, uid_t uid, gid_t gid)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        return "open directory failed for " + path + ": " + std::strerror(errno);
    }
    std::string err;
    if (::fchmod(fd, mode) != 0) {
        err = "fchmod failed for " + path + ": " + std::strerror(errno);
    } else if (::fchown(fd, uid, gid) != 0) {
        err = "fchown failed for " + path + ": " + std::strerror(errno);
    }
    ::close(fd);
    return err;
}

// nftw 回调只接受无捕获函数指针,用 thread_local 记录首个失败项的路径与 errno,保证线程安全。
// 路径用定长字符数组存储:thread_local 变量必须由 noexcept 构造初始化(G.ERR.10-CPP),
// std::string 构造可能抛出且线程入口无法捕获。
thread_local char g_nftwFailurePath[WORKSPACE_NFTW_PATH_MAX] = { 0 };
thread_local int g_nftwFailureErrno = 0;

// RemoveWorkspaceNftwCallback 先尝试 rmdir(目录),失败回退到 unlink(文件/符号链接)。
// 条目已不存在(ENOENT)按"已删除"的幂等成功处理:并发删除掉的条目同样满足移除目标,
// 不能让遍历中止;其它错误记录失败路径与 errno 后中止,由外层统一返回 ERR_INNER_SYSTEM_ERROR。
static int RemoveWorkspaceNftwCallback(const char *fpath, const struct stat *, int, struct FTW *)
{
    if (::rmdir(fpath) == 0 || errno == ENOENT) {
        return 0;
    }
    const int rmdirErrno = errno;
    if (rmdirErrno == ENOTDIR && (::unlink(fpath) == 0 || errno == ENOENT)) {
        return 0;
    }
    if (g_nftwFailurePath[0] == '\0') {
        const int savedErrno = errno;
        // 逐字节有界拷贝并保证 NUL 终止:与 snprintf("%s") 的截断语义一致,
        // 但不使用内存操作函数
        std::size_t pathLen = 0;
        while (pathLen + 1 < sizeof(g_nftwFailurePath) && fpath[pathLen] != '\0') {
            g_nftwFailurePath[pathLen] = fpath[pathLen];
            ++pathLen;
        }
        g_nftwFailurePath[pathLen] = '\0';
        g_nftwFailureErrno = savedErrno;
    }
    return -1;
}

::grpc::Status FrontendProxyService::PrepareWorkspace(::grpc::ServerContext *context,
                                                      const ::frontend_proxy::PrepareWorkspaceRequest *request,
                                                      ::frontend_proxy::PrepareWorkspaceResponse *response)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy prepare workspace requires an authenticated component certificate" };
    }
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy prepare workspace args" };
    }
    const auto *reqContext = request->has_context() ? &request->context() : nullptr;
    if (!ValidateWorkspaceCall(reqContext, request->owner(), response->mutable_status())) {
        return ::grpc::Status::OK;
    }
    const auto modeParsed = ParseWorkspaceMode(request->mode());
    if (modeParsed < 0) {
        SetStatus(response->mutable_status(), common::ERR_PARAM_INVALID,
                  "invalid workspace mode: " + request->mode());
        return ::grpc::Status::OK;
    }
    const auto storageRoot = ResolveWorkspaceStorageRoot();
    const auto targetPath = JoinWorkspacePath(storageRoot, request->owner());
    const auto uidGid = ResolveWorkspaceUidGid();
    const auto &requestID = reqContext->requestid();
    YRLOG_INFO("{}|frontend proxy prepare workspace owner={} path={} mode={:04o} uid={} gid={} proxy={}",
               requestID, request->owner(), targetPath, modeParsed, uidGid.first, uidGid.second, param_.nodeID);

    // mkdir -p:父目录按 WORKSPACE_PARENT_DIR_MODE 递归创建,叶子目录按请求的 mode(默认 0700)创建;
    // 已存在路径必须为真实目录(幂等且防符号链接假成功)。
    const auto makeErr = CreateWorkspaceDirectory(targetPath, static_cast<mode_t>(modeParsed));
    if (!makeErr.empty()) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR, makeErr);
        return ::grpc::Status::OK;
    }
    const auto ownErr = ApplyWorkspaceOwnership(targetPath, static_cast<mode_t>(modeParsed), uidGid.first,
                                                uidGid.second);
    if (!ownErr.empty()) {
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR, ownErr);
        return ::grpc::Status::OK;
    }
    YRLOG_INFO("{}|frontend proxy prepare workspace ok owner={} path={}", requestID, request->owner(), targetPath);
    SetStatus(response->mutable_status(), common::ERR_NONE, "");
    return ::grpc::Status::OK;
}

::grpc::Status FrontendProxyService::RemoveWorkspace(::grpc::ServerContext *context,
                                                     const ::frontend_proxy::RemoveWorkspaceRequest *request,
                                                     ::frontend_proxy::RemoveWorkspaceResponse *response)
{
    if (!HasAuthenticatedPeer(context)) {
        return { ::grpc::StatusCode::UNAUTHENTICATED,
                 "frontend proxy remove workspace requires an authenticated component certificate" };
    }
    if (request == nullptr || response == nullptr) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid frontend proxy remove workspace args" };
    }
    const auto *reqContext = request->has_context() ? &request->context() : nullptr;
    if (!ValidateWorkspaceCall(reqContext, request->owner(), response->mutable_status())) {
        return ::grpc::Status::OK;
    }
    const auto storageRoot = ResolveWorkspaceStorageRoot();
    const auto targetPath = JoinWorkspacePath(storageRoot, request->owner());
    const auto &requestID = reqContext->requestid();
    YRLOG_INFO("{}|frontend proxy remove workspace owner={} path={} proxy={}",
               requestID, request->owner(), targetPath, param_.nodeID);

    // 预检:目录不存在视为已删除(幂等),并避免 nftw 遍历中途失败后的 ENOENT 被误判为成功。
    struct stat st {};
    if (::lstat(targetPath.c_str(), &st) != 0) {
        if (errno == ENOENT) {
            SetStatus(response->mutable_status(), common::ERR_NONE, "");
            return ::grpc::Status::OK;
        }
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR,
                  "stat failed for " + targetPath + ": " + std::strerror(errno));
        return ::grpc::Status::OK;
    }
    // rm -rf:使用 nftw 递归删除,避免对 system() 的依赖。
    // FTW_DEPTH 先叶子后父节点,FTW_PHYS 不跟随符号链接(符号链接本身被 unlink,不波及链接目标)。
    g_nftwFailurePath[0] = '\0';
    g_nftwFailureErrno = 0;
    if (::nftw(targetPath.c_str(), RemoveWorkspaceNftwCallback, WORKSPACE_NFTW_MAX_FDS,
               FTW_DEPTH | FTW_PHYS) != 0) {
        // 失败详情取自 thread_local 记录的首个失败项,而非回调返回后可能已被覆盖的 errno。
        std::string err = "rm -rf failed for " + targetPath;
        if (g_nftwFailurePath[0] != '\0') {
            err += ": ";
            err += g_nftwFailurePath;
            err += ": ";
            err += std::strerror(g_nftwFailureErrno);
        }
        SetStatus(response->mutable_status(), common::ERR_INNER_SYSTEM_ERROR, err);
        return ::grpc::Status::OK;
    }
    YRLOG_INFO("{}|frontend proxy remove workspace ok owner={} path={}", requestID, request->owner(), targetPath);
    SetStatus(response->mutable_status(), common::ERR_NONE, "");
    return ::grpc::Status::OK;
}

#undef LogLifecycleEvent

}  // namespace functionsystem::local_scheduler
