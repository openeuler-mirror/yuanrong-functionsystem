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
#include "api_router_register.h"
#include <unistd.h>

#include "async/collect.hpp"
#include "async/defer.hpp"

using namespace std;

namespace functionsystem {

const std::string HEALTHY_URL = "/healthy";

const std::string NODE_ID = "Node-ID";
const std::string PID = "PID";
const int64_t PROBE_TIMEOUT = 5000;

bool VerifyHeader(const HttpRequest &request, const std::string &key, const std::string &expected)
{
    auto node = request.headers.find(key);
    if (node == request.headers.end()) {
        return false;
    }
    if (node->second != expected) {
        return false;
    }
    return true;
}

ApiRouterRegister::ApiRouterRegister()
{
    handlerMap_ = make_shared<HandlerMap>();
}

shared_ptr<HandlerMap> ApiRouterRegister::GetHandlers() const
{
    return handlerMap_;
}

void ApiRouterRegister::RegisterHandler(const string &url, const HttpHandler &handler) const
{
    auto result = handlerMap_->emplace(url, handler);
    if (!result.second) {
        YRLOG_WARN("Register repeat url: {}", url);
    }
}

DefaultHealthyRouter::DefaultHealthyRouter(const std::string &nodeID) : ApiRouterRegister()
{
    auto pid = std::to_string(getpid());
    auto defaultHealthy = [nodeID, pid](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        YRLOG_DEBUG_COUNT_60("healthy request in.");
        if (!VerifyHeader(request, NODE_ID, nodeID)) {
            return litebus::http::BadRequest("error nodeID");
        }
        if (!VerifyHeader(request, PID, pid)) {
            return litebus::http::BadRequest("error PID");
        }
        return litebus::http::Ok();
    };
    RegisterHandler(HEALTHY_URL, defaultHealthy);
}

HttpResponse GenerateHttpResponse(const litebus::http::ResponseCode &httpCode, const std::string &msg)
{
    return HttpResponse(httpCode, msg, litebus::http::ResponseBodyType::JSON);
}


void HealthyApiRouter::AddProbe(HealthyProbe probe)
{
    (void)probes_.emplace_back(probe);
}

void HealthyApiRouter::Register()
{
    auto pid = std::to_string(getpid());
    auto defaultHealthy = [probes(probes_), nodeID(nodeID_), probeTimeoutMs(probeTimeoutMs_),
                           pid](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        if (!VerifyHeader(request, NODE_ID, nodeID)) {
            return litebus::http::BadRequest("error nodeID");
        }
        if (!VerifyHeader(request, PID, pid)) {
            return litebus::http::BadRequest("error PID");
        }
        std::list<litebus::Future<Status>> futures;
        for (const auto &probe : probes) {
            futures.emplace_back(probe());
        }
        auto promise = std::make_shared<litebus::Promise<HttpResponse>>();
        (void)litebus::Collect(futures)
            .After(probeTimeoutMs,
                   [](const litebus::Future<std::list<Status>> &future) {
                       auto promise = litebus::Promise<std::list<Status>>();
                       promise.SetFailed(REQUEST_TIME_OUT);
                       return promise.GetFuture();
                   })
            .OnComplete([promise](const litebus::Future<std::list<Status>> &future) {
                return future.IsError()
                           ? promise->SetValue(litebus::http::BadRequest("failed to probe business health"))
                           : promise->SetValue(litebus::http::Ok());
            });
        return promise->GetFuture();
    };
    RegisterHandler(HEALTHY_URL, defaultHealthy);
}
}  // namespace functionsystem::function_accessor