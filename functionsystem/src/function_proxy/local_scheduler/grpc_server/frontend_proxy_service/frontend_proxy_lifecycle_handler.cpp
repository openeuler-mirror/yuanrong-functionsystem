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

#include <atomic>
#include <utility>

#include "common/logs/logging.h"
#include "common/trace/trace_manager.h"
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

// The yr.create scheduling span for the FrontendProxyService gRPC create path is
// started by the actor in ScheduleFrontendAndWaitReady once the ready ticket is
// registered (so duplicate requestIDs never start or stop a span owned by the
// original request). The helpers below stop it — at ready fulfillment, at the
// 55s ready timeout, or in the dispatcher when schedule acceptance fails.

// Stops the yr.create scheduling span with the terminal attributes: result,
// instance_id, placement node (the proxy owning the runtime that fulfilled the
// ready ticket), user_id/agent_type and the source marker; failures add
// yr.error_code/message plus an exception event, and creates that went through
// readiness redeploys add the retry bookkeeping from the failure snapshot.
void StopFrontendCreateSpan(const messages::ScheduleRequest &scheduleReq,
                            const std::shared_ptr<functionsystem::CallResult> &result,
                            const FrontendCreateFailureSnapshot &snapshot)
{
    const bool success = result != nullptr && result->code() == common::ERR_NONE;
    trace::AttributesVector attrs;
    attrs.emplace_back("yr.result", success ? "success" : "fail");
    attrs.emplace_back("result", success ? "success" : "fail");
    attrs.emplace_back("yr.source", FRONTEND_STR);
    attrs.emplace_back("agent_type", scheduleReq.instance().function());
    // Prefer the CallResult's instanceID; failure results built before any instance
    // existed leave it empty, in which case fall back to the snapshot's (recorded by
    // the deploy failure path). The runtime's ready CallResult is enriched with
    // runtimeinfo (route/proxyid) server-side but does not carry the instance id, so
    // success results also arrive without one — fall back to the instance id the
    // frontend designated on the schedule request (always present on the
    // POST /api/agent path, which derives it deterministically).
    std::string instanceID = result != nullptr ? result->instanceid() : std::string {};
    if (instanceID.empty()) {
        instanceID = snapshot.instanceID;
    }
    if (instanceID.empty()) {
        instanceID = scheduleReq.instance().instanceid();
    }
    if (!instanceID.empty()) {
        attrs.emplace_back("instance_id", instanceID);
        attrs.emplace_back("yr.instance_id", instanceID);
    }
    if (result != nullptr && result->has_runtimeinfo() && !result->runtimeinfo().proxyid().empty()) {
        attrs.emplace_back("node", result->runtimeinfo().proxyid());
    }
    for (const char *tenantKey : { "tenantId", "tenantID" }) {
        auto it = scheduleReq.instance().createoptions().find(tenantKey);
        if (it != scheduleReq.instance().createoptions().end() && !it->second.empty()) {
            attrs.emplace_back("user_id", it->second);
            break;
        }
    }
    std::vector<std::string> events;
    if (!success) {
        // A null result must still carry a real error code — ERR_NONE (0) would
        // mark the span as "failed with the success code" and pollute error stats.
        attrs.emplace_back("yr.error_code",
                           static_cast<int64_t>(result != nullptr ? result->code()
                                                                  : common::ERR_INNER_SYSTEM_ERROR));
        attrs.emplace_back("yr.error_message",
                           result != nullptr ? result->message()
                                             : std::string { "frontend create failed without a call result" });
        events.emplace_back("exception");
    }
    if (snapshot.retriedTimes > 0) {
        attrs.emplace_back("yr.retry_count", static_cast<int64_t>(snapshot.retriedTimes));
        attrs.emplace_back("yr.retry.first_error_code", static_cast<int64_t>(snapshot.firstFailureCode));
        if (!snapshot.firstFailureMessage.empty()) {
            attrs.emplace_back("yr.retry.first_error_message", snapshot.firstFailureMessage);
        }
    }
    trace::TraceManager::GetInstance().StopSpan(trace::SpanName::K_CREATE, scheduleReq.requestid(), attrs, events);
}

// Stops the yr.create span after a fulfilled ready ticket (success or failure
// CallResult), consuming the failure snapshot — which carries the retry
// bookkeeping — via the actor-side lookup when one is wired.
void StopFrontendCreateSpanOnFulfilled(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                                       const std::shared_ptr<functionsystem::CallResult> &result,
                                       const FrontendCreateFailureLookup &createFailureLookup)
{
    if (createFailureLookup) {
        createFailureLookup(scheduleReq->requestid())
            .OnComplete([scheduleReq, result](const litebus::Future<FrontendCreateFailureSnapshot> &future) {
                FrontendCreateFailureSnapshot snapshot {};
                if (future.IsOK()) {
                    snapshot = future.Get();
                }
                // An errored lookup still stops the span (empty snapshot): the span
                // must never outlive the create request.
                StopFrontendCreateSpan(*scheduleReq, result, snapshot);
            });
        return;
    }
    StopFrontendCreateSpan(*scheduleReq, result, FrontendCreateFailureSnapshot {});
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
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq)
{
    const std::string &requestID = scheduleReq->requestid();
    if (createFailureLookup) {
        return createFailureLookup(requestID).Then(
            [scheduleReq, readyUnregister](const FrontendCreateFailureSnapshot &snapshot)
                -> std::shared_ptr<functionsystem::CallResult> {
                if (readyUnregister) {
                    readyUnregister(scheduleReq->requestid(), FRONTEND_CREATE_READY_UNREGISTER_REASON);
                }
                auto result = std::make_shared<functionsystem::CallResult>();
                result->set_requestid(scheduleReq->requestid());
                if (snapshot.present) {
                    result->set_code(static_cast<common::ErrorCode>(snapshot.code));
                    result->set_message(snapshot.message);
                    result->set_instanceid(snapshot.instanceID);
                } else {
                    result->set_code(common::ERR_INNER_SYSTEM_ERROR);
                    result->set_message(FRONTEND_CREATE_READY_TIMEOUT_MSG);
                }
                StopFrontendCreateSpan(*scheduleReq, result, snapshot);
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
    StopFrontendCreateSpan(*scheduleReq, result, FrontendCreateFailureSnapshot {});
    return result;
}

// Ready-ticket fulfillment outlet of the create dispatcher: the first outlet
// through the latch resolves the ready promise and stops the yr.create span
// (consuming the retry bookkeeping); a late result after the timeout or
// schedule-failure outlet already ran is dropped.
litebus::Future<CallResultAck> OnReadyTicketFulfilled(
    const std::shared_ptr<litebus::Promise<std::shared_ptr<functionsystem::CallResult>>> &readyPromise,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const FrontendCreateFailureLookup &createFailureLookup,
    const std::shared_ptr<std::atomic_bool> &spanOutletTaken,
    const std::shared_ptr<functionsystem::CallResult> &readyResult)
{
    CallResultAck ack;
    ack.set_code(common::ERR_NONE);
    ack.set_message("success");
    if (spanOutletTaken->exchange(true)) {
        // The timeout or schedule-failure outlet already stopped the
        // span; the late ready result is dropped.
        return ack;
    }
    readyPromise->SetValue(readyResult);
    // Ready ticket fulfilled (success or failure CallResult): stop the
    // yr.create span, consuming the retry bookkeeping first.
    StopFrontendCreateSpanOnFulfilled(scheduleReq, readyResult, createFailureLookup);
    return ack;
}

// Schedule-response outlet of the create dispatcher: builds the create
// response from the ScheduleResponse (returning the IID on success, without
// waiting for ready). On schedule failure, the first outlet through the latch
// resolves the ready promise — disarming the ready-timeout closure — and stops
// the span when this request owns it (registered-ticket marker present).
::frontend_proxy::CreateInstanceResponse OnScheduleResponseResolved(
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const std::shared_ptr<litebus::Promise<std::shared_ptr<functionsystem::CallResult>>> &readyPromise,
    const FrontendCreateFailureLookup &createFailureLookup,
    const std::shared_ptr<std::atomic_bool> &spanOutletTaken,
    const messages::ScheduleResponse &scheduleResponse)
{
    ::frontend_proxy::CreateInstanceResponse response;
    response.mutable_create()->CopyFrom(TransFromScheduleRspToCreateRsp(scheduleResponse));
    if (scheduleResponse.code() == common::ERR_NONE) {
        return response;
    }
    // Schedule acceptance failed: no instance will ever fulfill this request's
    // ready ticket. Resolve the promise to disarm the 55s timeout closure
    // (litebus cancels the After timer on completion).
    auto result = std::make_shared<functionsystem::CallResult>();
    result->set_requestid(scheduleReq->requestid());
    result->set_code(static_cast<common::ErrorCode>(scheduleResponse.code()));
    result->set_message(scheduleResponse.message());
    if (spanOutletTaken->exchange(true)) {
        // The ready ticket was fulfilled or timed out first and that outlet
        // stopped the span; nothing to do here.
        return response;
    }
    readyPromise->SetValue(result);
    if (scheduleReq->instance().extensions().find(FRONTEND_READY_TICKET_REGISTERED)
        != scheduleReq->instance().extensions().end()) {
        // The ticket was registered (the actor started the yr.create span for
        // this request) and the schedule then failed: stop the span now with
        // the real error instead of letting the 55s timeout end it with a
        // generic message. The ticket itself is unregistered actor-side by
        // OnFrontendScheduleCompleted, which watches this same schedule future
        // and calls UnregisterFrontendReadyWait on non-ERR_NONE codes. Consume
        // the failure snapshot through the lookup like the fulfilled outlet, so
        // a deploy failure recorded before the schedule response carries its
        // retry bookkeeping onto this span too.
        StopFrontendCreateSpanOnFulfilled(scheduleReq, result, createFailureLookup);
    }
    // Otherwise this is a duplicate-request rejection: no span was started for
    // this request and the pending span under this requestID belongs to the
    // original in-flight request — leave it to the original's outlets.
    return response;
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
        // Single-outlet latch: the ready callback (actor fulfillment), the
        // schedule-failure branch below and the ready-timeout closure run on
        // different execution paths and may race. Exactly the first one to run
        // performs the promise SetValue + span stop; the others must be no-ops —
        // a second SetValue on a satisfied litebus promise is an error, and a
        // second span stop would overwrite the terminal attributes.
        auto spanOutletTaken = std::make_shared<std::atomic_bool>(false);
        // the yr.create scheduling span is started by the actor inside
        // ScheduleFrontendAndWaitReady once the ready ticket is registered (it covers
        // dispatch acceptance -> ready fulfillment or the ready timeout and is stopped
        // at either outlet; the stage sub-spans attach to it via the span context
        // propagated into createOptions). Starting it there — not here — keeps a
        // duplicate requestID rejection from touching the original request's span.
        // Still wired so the ready ticket and its timeout cleanup drive state-machine
        // cleanup in the background, even though the create response no longer waits.
        // Note: litebus Future::After is expected to cancel the timer once the future
        // completes, but the timeout closure additionally checks the latch so it stays
        // a no-op even if that cancellation semantic ever changes.
        [[maybe_unused]] auto readyResolved = readyPromise->GetFuture().After(
            readyTimeoutMs,
            [scheduleReq, readyUnregister, createFailureLookup, spanOutletTaken, readyPromise](
                const litebus::Future<std::shared_ptr<functionsystem::CallResult>> &)
                -> litebus::Future<std::shared_ptr<functionsystem::CallResult>> {
                if (spanOutletTaken->exchange(true)) {
                    // Another outlet (fulfillment or schedule failure) already
                    // resolved this request; the late timeout is a no-op.
                    return litebus::Future<std::shared_ptr<functionsystem::CallResult>>(
                        std::make_shared<functionsystem::CallResult>());
                }
                // This outlet owns the resolution: settle the ready promise with the
                // timeout result (so the promise is always satisfied exactly once)
                // and return it — never a null CallResult holder.
                return BuildFrontendCreateReadyTimeoutResult(
                    createFailureLookup, readyUnregister, scheduleReq)
                    .Then([readyPromise](const std::shared_ptr<functionsystem::CallResult> &result)
                              -> std::shared_ptr<functionsystem::CallResult> {
                        readyPromise->SetValue(result);
                        return result;
                    });
            });
        YRLOG_INFO("{}|frontend system create function({}) from frontendClientID({}), tenantID({})",
                   scheduleReq->requestid(), scheduleReq->instance().function(), request.context().frontendclientid(),
                   request.context().tenantid());
        auto onReadyFulfilled = [readyPromise, scheduleReq, createFailureLookup, spanOutletTaken](
            const std::shared_ptr<functionsystem::CallResult> &readyResult) {
            return OnReadyTicketFulfilled(readyPromise, scheduleReq, createFailureLookup, spanOutletTaken,
                                          readyResult);
        };
        return scheduler(scheduleReq, runtimePromise, std::move(onReadyFulfilled))
            .Then([scheduleReq, readyPromise, createFailureLookup, spanOutletTaken](
                      const messages::ScheduleResponse &scheduleResponse) {
                return litebus::Future<::frontend_proxy::CreateInstanceResponse>(OnScheduleResponseResolved(
                    scheduleReq, readyPromise, createFailureLookup, spanOutletTaken, scheduleResponse));
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
