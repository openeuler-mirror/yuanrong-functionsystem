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

#include "traefik_route_cache.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <vector>

#include "common/logs/logging.h"
#include "nlohmann/json.hpp"

namespace functionsystem::global_scheduler {

static const std::string PORT_FORWARD_KEY = "portForward";
constexpr size_t MAX_ROUTER_NAME_LEN = 200;

TraefikRouteCache::TraefikRouteCache(TraefikConfig cfg)
    : cfg_(std::move(cfg))
{
}

void TraefikRouteCache::OnInstanceRunning(const resource_view::InstanceInfo& instance)
{
    auto it = instance.extensions().find(PORT_FORWARD_KEY);
    if (it == instance.extensions().end() || it->second.empty()) {
        return;
    }

    auto routes = ParseRoutes(instance);
    if (routes.empty()) {
        return;
    }

    {
        std::unique_lock lock(routeTableMu_);
        routeTable_[instance.instanceid()] = std::move(routes);
    }
    dirty_ = true;
    YRLOG_DEBUG("TraefikRouteCache: added routes for instance {}", instance.instanceid());
}

void TraefikRouteCache::OnInstanceExited(const std::string& instanceID)
{
    {
        std::unique_lock lock(routeTableMu_);
        if (routeTable_.erase(instanceID) == 0) {
            return;
        }
    }
    dirty_ = true;
    YRLOG_DEBUG("TraefikRouteCache: removed routes for instance {}", instanceID);
}

std::string TraefikRouteCache::GetConfigJSON()
{
    std::lock_guard lock(cacheMu_);
    if (dirty_) {
        cachedJSON_ = BuildConfigJSON();
        dirty_ = false;
    }
    return cachedJSON_;
}

size_t TraefikRouteCache::GetRouteCount() const
{
    std::shared_lock lock(routeTableMu_);
    size_t count = 0;
    for (const auto& [_, entries] : routeTable_) {
        count += entries.size();
    }
    return count;
}

std::vector<TraefikRouteCache::RouteEntry> TraefikRouteCache::ParseRoutes(
    const resource_view::InstanceInfo& instance) const
{
    std::vector<RouteEntry> routes;

    std::string hostIP = ExtractIP(instance.proxygrpcaddress());
    if (hostIP.empty()) {
        YRLOG_WARN("TraefikRouteCache: invalid proxyGrpcAddress '{}' for instance {}",
                   instance.proxygrpcaddress(), instance.instanceid());
        return routes;
    }

    std::string safeID = SanitizeID(instance.instanceid());

    auto it = instance.extensions().find(PORT_FORWARD_KEY);
    if (it == instance.extensions().end() || it->second.empty()) {
        return routes;
    }

    try {
        nlohmann::json portJson = nlohmann::json::parse(it->second);
        if (!portJson.is_array()) {
            YRLOG_WARN("TraefikRouteCache: portForward is not array for instance {}", instance.instanceid());
            return routes;
        }

        for (const auto& entry : portJson) {
            if (!entry.is_string()) {
                continue;
            }
            std::string mapping = entry.get<std::string>();
            std::vector<std::string> parts;
            std::stringstream ss(mapping);
            std::string part;
            while (std::getline(ss, part, ':')) {
                parts.push_back(part);
            }

            std::string protocol;
            int hostPort = 0;
            int sandboxPort = 0;

            constexpr size_t NEW_FORMAT_PARTS = 3;
            constexpr size_t LEGACY_FORMAT_PARTS = 2;

            if (parts.size() == NEW_FORMAT_PARTS) {
                // "protocol:hostPort:containerPort"
                protocol = parts[0];
                hostPort = std::stoi(parts[1]);
                sandboxPort = std::stoi(parts[2]);
            } else if (parts.size() == LEGACY_FORMAT_PARTS) {
                // "hostPort:containerPort"
                protocol = "http";
                hostPort = std::stoi(parts[0]);
                sandboxPort = std::stoi(parts[1]);
            } else {
                YRLOG_WARN("TraefikRouteCache: invalid port mapping format '{}' for instance {}",
                           mapping, instance.instanceid());
                continue;
            }

            std::string protocolLower = protocol;
            std::transform(protocolLower.begin(), protocolLower.end(), protocolLower.begin(), ::tolower);
            bool useHttps = (protocolLower == "https");
            std::string scheme = useHttps ? "https" : "http";

            RouteEntry route;
            route.routerName  = safeID + "-p" + std::to_string(sandboxPort);
            route.backendURL  = scheme + "://" + hostIP + ":" + std::to_string(hostPort);
            route.sandboxPort = sandboxPort;
            route.useHttps    = useHttps;
            routes.push_back(std::move(route));
        }
    } catch (const std::exception& e) {
        YRLOG_WARN("TraefikRouteCache: failed to parse portForward for instance {}: {}",
                   instance.instanceid(), e.what());
    }

    return routes;
}

std::string TraefikRouteCache::ExtractIP(const std::string& addr)
{
    auto pos = addr.find(':');
    return pos != std::string::npos ? addr.substr(0, pos) : "";
}

std::string TraefikRouteCache::SanitizeID(const std::string& id)
{
    constexpr size_t AT_REPLACEMENT_LEN = 4;  // length of "-at-"
    std::string result = id;

    // Replace @ with -at-
    size_t pos = 0;
    while ((pos = result.find('@', pos)) != std::string::npos) {
        result.replace(pos, 1, "-at-");
        pos += AT_REPLACEMENT_LEN;
    }

    // Replace other problematic characters
    std::replace(result.begin(), result.end(), '/', '-');
    std::replace(result.begin(), result.end(), '.', '-');
    std::replace(result.begin(), result.end(), '_', '-');

    if (result.length() > MAX_ROUTER_NAME_LEN) {
        result = result.substr(0, MAX_ROUTER_NAME_LEN);
    }
    return result;
}

std::string TraefikRouteCache::BuildConfigJSON() const
{
    std::shared_lock lock(routeTableMu_);

    // Collect all route entries with sorted keys for deterministic output
    std::map<std::string, const RouteEntry*> sortedRoutes;
    for (const auto& [instanceID, entries] : routeTable_) {
        for (const auto& entry : entries) {
            sortedRoutes[entry.routerName] = &entry;
        }
    }

    // Build routers and services JSON (using nlohmann::json with std::map for sorted keys)
    nlohmann::json routersJson = nlohmann::json::object();
    nlohmann::json servicesJson = nlohmann::json::object();

    for (const auto& [name, entryPtr] : sortedRoutes) {
        const auto& entry = *entryPtr;

        // Parse safeID from routerName for the rule
        auto dashPPos = name.rfind("-p");
        if (dashPPos == std::string::npos || dashPPos == 0) {
            continue;
        }
        std::string safeID = name.substr(0, dashPPos);

        // Router
        nlohmann::json router;
        router["entryPoints"] = nlohmann::json::array({cfg_.httpEntryPoint});
        router["middlewares"] = nlohmann::json::array({"stripprefix-all"});
        router["rule"] = "PathPrefix(`/" + safeID + "/" + std::to_string(entry.sandboxPort) + "`)";
        router["service"] = name;
        if (cfg_.enableTLS) {
            router["tls"] = nlohmann::json::object();
        }
        routersJson[name] = std::move(router);

        // Service
        nlohmann::json service;
        nlohmann::json lb;
        lb["servers"] = nlohmann::json::array({nlohmann::json{{"url", entry.backendURL}}});
        if (entry.useHttps && !cfg_.serversTransport.empty()) {
            lb["serversTransport"] = cfg_.serversTransport;
        }
        service["loadBalancer"] = std::move(lb);
        servicesJson[name] = std::move(service);
    }

    // Build the full dynamic configuration
    // NOTE: Traefik's paerser (file.DecodeContent) cannot decode empty JSON objects ({})
    // into map types — it errors with "cannot be a standalone element".  Omitting the
    // key entirely is the correct way to express "no entries" in Traefik dynamic config.
    nlohmann::json middlewares;
    middlewares["stripprefix-all"]["stripPrefixRegex"]["regex"] =
        nlohmann::json::array({"^/[^/]+/[0-9]+"});

    nlohmann::json httpConfig;
    httpConfig["middlewares"] = std::move(middlewares);
    if (!routersJson.empty()) {
        httpConfig["routers"] = std::move(routersJson);
    }
    if (!servicesJson.empty()) {
        httpConfig["services"] = std::move(servicesJson);
    }

    nlohmann::json config;
    config["http"] = std::move(httpConfig);

    // nlohmann::json with default object type (std::map) sorts keys lexicographically
    return config.dump();
}

}  // namespace functionsystem::global_scheduler
