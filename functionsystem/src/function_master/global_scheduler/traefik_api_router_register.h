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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_API_ROUTER_REGISTER_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_API_ROUTER_REGISTER_H

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string>

#include "common/http/api_router_register.h"
#include "common/logs/logging.h"
#include "httpd/http_connect.hpp"
#include "traefik_route_cache.h"

namespace functionsystem::global_scheduler {

constexpr char TRAEFIK_CONFIG_URL[] = "/traefik/config";

// ---- Leader context shared between the HTTP handler and Explorer callback ----
// Updated by Explorer::AddLeaderChangedCallback when the leader changes.
// Read by the HTTP handler on every Traefik poll request.
struct TraefikLeaderContext {
    std::atomic<bool> isLeader{false};       // fast path: am I the leader?
    std::string selfHttpAddress;             // set once at startup, never mutated

    mutable std::shared_mutex mu;            // protects leaderHttpAddress
    std::string leaderHttpAddress;           // "ip:port" of the current leader

    void UpdateLeader(const std::string &addr, bool isSelf)
    {
        {
            std::unique_lock lock(mu);
            leaderHttpAddress = addr;
        }
        isLeader.store(isSelf);
    }

    std::string GetLeaderHttpAddress() const
    {
        std::shared_lock lock(mu);
        return leaderHttpAddress;
    }
};

// ---- Traefik HTTP provider endpoint with standby-to-leader forwarding ----
// Leader   -> serves config JSON from local TraefikRouteCache.
// Standby  -> forwards the request to the leader via litebus HTTP client.
// No leader / self-loop / forward failure -> returns 503.
class TraefikApiRouterRegister : public ApiRouterRegister {
public:
    TraefikApiRouterRegister(std::shared_ptr<TraefikRouteCache> cache,
                             std::shared_ptr<TraefikLeaderContext> leaderCtx,
                             uint32_t forwardTimeoutMs)
        : ApiRouterRegister()
    {
        auto handler = [c = std::move(cache),
                        ctx = std::move(leaderCtx),
                        timeoutMs = static_cast<uint64_t>(forwardTimeoutMs)]
            (const HttpRequest &req) -> litebus::Future<HttpResponse>
        {
            if (req.method != "GET") {
                return HttpResponse(litebus::http::ResponseCode::METHOD_NOT_ALLOWED);
            }

            // ---- Leader: serve from local cache ----
            if (ctx->isLeader.load()) {
                std::string body = c->GetConfigJSON();
                HttpResponse resp(litebus::http::ResponseCode::OK);
                resp.headers["Content-Type"] = "application/json";
                resp.body = std::move(body);
                return resp;
            }

            // ---- Standby: forward to leader ----
            std::string leaderAddr = ctx->GetLeaderHttpAddress();
            if (leaderAddr.empty() || leaderAddr == ctx->selfHttpAddress) {
                YRLOG_WARN("TraefikApiRouter: no leader available or self-loop, returning 503");
                return HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE);
            }

            auto colonPos = leaderAddr.rfind(':');
            if (colonPos == std::string::npos) {
                YRLOG_WARN("TraefikApiRouter: invalid leader address '{}', returning 503", leaderAddr);
                return HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE);
            }

            std::string ip = leaderAddr.substr(0, colonPos);
            uint16_t port = 0;
            try {
                port = static_cast<uint16_t>(std::stoul(leaderAddr.substr(colonPos + 1)));
            } catch (...) {
                YRLOG_WARN("TraefikApiRouter: failed to parse leader port from '{}', returning 503", leaderAddr);
                return HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE);
            }

            litebus::http::URL url;
            url.scheme = std::string("http");
            url.ip = ip;
            url.port = port;
            url.path = "/global-scheduler/traefik/config";

            auto promise = std::make_shared<litebus::Promise<HttpResponse>>();
            auto future = promise->GetFuture();

            litebus::http::Get(url, litebus::None(), litebus::Option<uint64_t>(timeoutMs))
                .OnComplete([promise, leaderAddr](const litebus::Future<litebus::http::Response> &fwdFuture) {
                    if (fwdFuture.IsError() || !fwdFuture.IsOK()) {
                        YRLOG_WARN("TraefikApiRouter: forward to leader '{}' failed, returning 503", leaderAddr);
                        promise->SetValue(HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE));
                        return;
                    }

                    const auto &fwdResp = fwdFuture.Get();
                    if (fwdResp.retCode != litebus::http::ResponseCode::OK) {
                        YRLOG_WARN("TraefikApiRouter: leader '{}' returned {}, returning 503",
                                   leaderAddr, static_cast<int>(fwdResp.retCode));
                        promise->SetValue(HttpResponse(litebus::http::ResponseCode::SERVICE_UNAVAILABLE));
                        return;
                    }

                    HttpResponse resp(litebus::http::ResponseCode::OK);
                    resp.headers["Content-Type"] = "application/json";
                    resp.body = fwdResp.body;
                    promise->SetValue(std::move(resp));
                });

            return future;
        };
        RegisterHandler(TRAEFIK_CONFIG_URL, handler);
    }

    ~TraefikApiRouterRegister() override = default;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_API_ROUTER_REGISTER_H
