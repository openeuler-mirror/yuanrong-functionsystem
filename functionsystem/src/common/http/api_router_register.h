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

#ifndef COMMON_HTTP_API_ROUTER_REGISTER_H
#define COMMON_HTTP_API_ROUTER_REGISTER_H

#include <async/async.hpp>
#include <functional>
#include <httpd/http.hpp>
#include <string>
#include <unordered_map>
#include <utility>

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem {
using HttpResponse = litebus::http::Response;
using HttpRequest = litebus::http::Request;
using HttpHandler = std::function<litebus::Future<HttpResponse>(const HttpRequest &)>;
using HttpChecker = std::function<Status(const HttpRequest &)>;
using HandlerMap = std::unordered_map<std::string, HttpHandler>;

HttpResponse GenerateHttpResponse(const litebus::http::ResponseCode &httpCode, const std::string &msg);
bool VerifyHeader(const HttpRequest &request, const std::string &key, const std::string &expected);

class ApiRouterRegister {
public:
    /**
     * Constructor.
     */
    ApiRouterRegister();

    /**
     * Get url-handler pairs.
     *
     * @return Url-handler pairs.
     */
    virtual std::shared_ptr<HandlerMap> GetHandlers() const;

    virtual ~ApiRouterRegister() = default;

protected:
    /**
     * Classes extend from ApiRouterRegister needs to invoke this method, register their url-handler pair to
     * handlerMap_.
     *
     * @param url
     * @param handler
     */
    virtual void RegisterHandler(const std::string &url, const HttpHandler &handler) const;

private:
    std::shared_ptr<HandlerMap> handlerMap_;
};

/**
 * Default Healthy check Router Register. This class can only be used to determine whether a process is running.
 */
class DefaultHealthyRouter : public ApiRouterRegister {
public:
    explicit DefaultHealthyRouter(const std::string &nodeID);
    ~DefaultHealthyRouter() override = default;
};

/**
 * Healthy check Router Register. This class can only be used to determine and add probe of business for liveness.
 */
using HealthyProbe = std::function<litebus::Future<Status>()>;
class HealthyApiRouter : public ApiRouterRegister {
public:
    HealthyApiRouter(std::string nodeID, const litebus::Duration &probeTimeoutMs)
        : ApiRouterRegister(), nodeID_(std::move(nodeID)), probeTimeoutMs_(probeTimeoutMs)
    {
    }
    ~HealthyApiRouter() override = default;

    void AddProbe(HealthyProbe probe);
    void Register();

private:
    std::vector<HealthyProbe> probes_;
    std::string nodeID_;
    litebus::Duration probeTimeoutMs_;
};

}  // namespace functionsystem

#endif  // COMMON_HTTP_API_ROUTER_REGISTER_H
