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

#include "request_router.h"

#include <functional>

#include <litebus.hpp>
#include <async/async.hpp>

#include "common/status/status.h"
#include "function_proxy/busproxy/instance_proxy/request_dispatcher.h"
#include "function_proxy/busproxy/instance_proxy/instance_proxy.h"
#include "function_proxy/config/direct_routing_config.h"

namespace functionsystem::busproxy {

void RequestRouter::Init()
{
    ActorBase::Init();
    Receive("ForwardCall", &RequestRouter::ForwardCall);
}

void RequestRouter::ForwardCall(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto srcInstanceID = from.Name();
    internal::RouteCallRequest routeReq;
    routeReq.ParseFromString(msg);

    auto instanceAID = litebus::AID(routeReq.instanceid(), GetAID().Url());
    auto dstAID = litebus::GetActor(instanceAID);
    if (dstAID == nullptr) {
        YRLOG_WARN("{}|{}|instance {} not found, maybe be not created or killed", routeReq.req().callreq().traceid(),
                   routeReq.req().callreq().requestid(), routeReq.instanceid());

        if (function_proxy::DirectRoutingConfig::IsEnabled() && observer_ != nullptr) {
            observer_->QueryInstanceRouteForDirectRouting(routeReq.instanceid())
                .OnComplete(litebus::Defer(GetAID(), &RequestRouter::OnMissingActorRouteQuery, from, routeReq,
                                           std::placeholders::_1));
            return;
        }

        SharedStreamMsg rsp =
            CreateCallResponse(common::ErrorCode::ERR_INSTANCE_NOT_FOUND,
                               "instance is not found, maybe be not created or killed", routeReq.req().messageid());
        Send(from, "ResponseForwardCall", rsp->SerializeAsString());
        return;
    }
    auto request = std::make_shared<runtime_rpc::StreamingMessage>();
    request->CopyFrom(routeReq.req());
    const auto &callReq = request->callreq();
    YRLOG_INFO("{}|{}|received forward Call from {} and route to {}", callReq.traceid(), callReq.requestid(),
               srcInstanceID, routeReq.instanceid());
    (void)litebus::Async(instanceAID, &InstanceProxy::DoForwardCall, from, request);
    return;
}

void RequestRouter::OnMissingActorRouteQuery(
    const litebus::AID &from, const internal::RouteCallRequest &routeReq,
    const litebus::Future<function_proxy::DirectRouteQueryResult> &routeFuture)
{
    SharedStreamMsg rsp;
    if (routeFuture.IsError()) {
        rsp = CreateCallResponse(common::ErrorCode::ERR_INNER_COMMUNICATION,
                                 "failed to query instance route for direct routing", routeReq.req().messageid());
    } else {
        const auto routeResult = routeFuture.Get();
        if (!routeResult.status.IsError() && routeResult.routeInfo != nullptr &&
            !routeResult.routeInfo->proxygrpcaddress().empty()) {
            rsp = CreateCallResponseWithRouteUpdate(common::ErrorCode::ERR_INNER_COMMUNICATION,
                                                    "stale route updated", routeReq.req().messageid(),
                                                    routeReq.instanceid(),
                                                    routeResult.routeInfo->proxygrpcaddress(),
                                                    routeResult.routeInfo->functionproxyid(),
                                                    routeResult.routeInfo->version());
        } else {
            const auto errorCode = routeResult.status.IsError()
                ? Status::GetPosixErrorCode(routeResult.status.StatusCode())
                : common::ErrorCode::ERR_INNER_COMMUNICATION;
            const auto reason = routeResult.status.IsError()
                ? (routeResult.status.GetMessage().empty() ? routeResult.status.ToString()
                                                           : routeResult.status.GetMessage())
                : "instance route is empty for direct routing.";
            rsp = CreateCallResponse(errorCode, reason, routeReq.req().messageid());
        }
    }
    Send(from, "ResponseForwardCall", rsp->SerializeAsString());
}

}  // namespace functionsystem::busproxy
