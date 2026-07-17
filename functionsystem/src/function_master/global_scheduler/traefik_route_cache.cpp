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
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <vector>

#include "common/logs/logging.h"
#include "nlohmann/json.hpp"

namespace functionsystem::global_scheduler {

static const std::string PORT_FORWARD_KEY = "portForward";
constexpr size_t MAX_ROUTER_NAME_LEN = 200;
constexpr size_t MAX_DNS_LABEL_LEN = 63;
constexpr size_t MAX_FQDN_LEN = 253;
constexpr uint32_t FNV_OFFSET_BASIS = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;
constexpr uint32_t TUNNEL_ROUTER_PRIORITY = 100;

TraefikRouteCache::TraefikRouteCache(TraefikConfig cfg)
    : cfg_(std::move(cfg))
{
    cfg_.publicBaseDomain = NormalizePublicBaseDomain(cfg_.publicBaseDomain);
}

void TraefikRouteCache::OnInstanceRunning(const resource_view::InstanceInfo& instance)
{
    auto routes = ParseRoutes(instance);

    {
        std::unique_lock lock(routeTableMu_);
        routeTable_.erase(instance.instanceid());
        if (!routes.empty()) {
            routeTable_[instance.instanceid()] = std::move(routes);
        }
    }
    dirty_ = true;
    YRLOG_DEBUG("TraefikRouteCache: replaced routes for instance {}", instance.instanceid());
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

        bool hasTunnelRoute = false;
        for (const auto& entry : portJson) {
            if (!entry.is_string()) {
                continue;
            }
            std::string mapping = entry.get<std::string>();
            const auto parsed = ParsePortForwardMapping(mapping);
            if (!parsed.has_value()) {
                YRLOG_WARN("TraefikRouteCache: unsupported port mapping '{}' for instance {}",
                           mapping, instance.instanceid());
                continue;
            }

            if (parsed->routeKind == PortRouteKind::DIRECT) {
                continue;
            }
            if (parsed->routeKind == PortRouteKind::TUNNEL && hasTunnelRoute) {
                YRLOG_WARN("TraefikRouteCache: duplicate tunnel mapping '{}' for instance {}",
                           mapping, instance.instanceid());
                continue;
            }
            if (parsed->legacyTransport) {
                YRLOG_WARN("TraefikRouteCache: legacy tcp mapping '{}' is treated as public HTTP", mapping);
            }

            RouteEntry route;
            route.routeKind = parsed->routeKind;
            route.safeID = safeID;
            route.routerName = parsed->routeKind == PortRouteKind::TUNNEL
                                   ? safeID + "-tunnel"
                                   : safeID + "-p" + std::to_string(parsed->containerPort);
            route.backendURL = parsed->backendScheme + "://" + hostIP + ":" +
                               std::to_string(parsed->hostPort);
            route.sandboxPort = parsed->containerPort;
            route.useHttps = parsed->backendScheme == "https";
            routes.push_back(std::move(route));
            hasTunnelRoute = hasTunnelRoute || parsed->routeKind == PortRouteKind::TUNNEL;
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

    // Traefik rules wrap paths in backticks. Restrict the embedded ID to a
    // conservative ASCII set so rule delimiters and path separators cannot
    // be injected through an instance ID.
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::isalnum(ch) || ch == '-' ? ch : '-');
    });

    if (result.length() > MAX_ROUTER_NAME_LEN) {
        result = result.substr(0, MAX_ROUTER_NAME_LEN);
    }
    return result;
}

std::string TraefikRouteCache::NormalizePublicBaseDomain(const std::string& domain)
{
    std::string result = domain;
    result.erase(0, result.find_first_not_of(" \t\r\n"));
    auto last = result.find_last_not_of(" \t\r\n");
    if (last == std::string::npos) {
        return "";
    }
    result.erase(last + 1);

    auto schemePos = result.find("://");
    if (schemePos != std::string::npos) {
        result.erase(0, schemePos + 3);
    }
    auto pathPos = result.find('/');
    if (pathPos != std::string::npos) {
        result.erase(pathPos);
    }
    if (result.rfind("*.", 0) == 0) {
        result.erase(0, 2);
    }
    auto portPos = result.rfind(':');
    if (portPos != std::string::npos) {
        result.erase(portPos);
    }
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string TraefikRouteCache::SanitizeDNSLabelComponent(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    bool previousWasDash = false;
    for (unsigned char c : value) {
        const bool isAlphaNum = std::isalnum(c) != 0;
        const char next = isAlphaNum ? static_cast<char>(std::tolower(c)) : '-';
        if (next == '-') {
            if (!result.empty() && !previousWasDash) {
                result.push_back(next);
            }
            previousWasDash = true;
            continue;
        }
        result.push_back(next);
        previousWasDash = false;
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    if (result.empty()) {
        return "sandbox";
    }
    return result;
}

std::string TraefikRouteCache::StableHashSuffix(const std::string& value)
{
    uint32_t hash = FNV_OFFSET_BASIS;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= FNV_PRIME;
    }

    constexpr char HEX[] = "0123456789abcdef";
    std::string result(8, '0');
    for (int i = 7; i >= 0; --i) {
        result[static_cast<size_t>(i)] = HEX[hash & 0xFU];
        hash >>= 4;
    }
    return result;
}

std::string TraefikRouteCache::BuildHostLabel(int sandboxPort, const std::string& safeID)
{
    const std::string safeDNS = SanitizeDNSLabelComponent(safeID);
    std::string label = std::to_string(sandboxPort) + "-" + safeDNS;
    if (label.length() > MAX_DNS_LABEL_LEN) {
        const std::string suffix = "-" + StableHashSuffix(std::to_string(sandboxPort) + ":" + safeID);
        label = label.substr(0, MAX_DNS_LABEL_LEN - suffix.length()) + suffix;
    }
    return label;
}

std::string TraefikRouteCache::BuildHostRouterName(const std::string& routerName)
{
    const std::string suffix = "-host";
    if (routerName.length() + suffix.length() <= MAX_ROUTER_NAME_LEN) {
        return routerName + suffix;
    }

    const std::string hashSuffix = "-" + StableHashSuffix(routerName) + suffix;
    return routerName.substr(0, MAX_ROUTER_NAME_LEN - hashSuffix.length()) + hashSuffix;
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

        // Router
        nlohmann::json router;
        router["entryPoints"] = nlohmann::json::array({cfg_.httpEntryPoint});
        if (entry.routeKind == PortRouteKind::TUNNEL) {
            router["middlewares"] = nlohmann::json::array({"stripprefix-tunnel"});
            const std::string tunnelPath = "/tunnel/" + entry.safeID;
            router["rule"] = "Path(`" + tunnelPath + "`) || PathPrefix(`" + tunnelPath + "/`)";
            router["priority"] = TUNNEL_ROUTER_PRIORITY;
        } else {
            router["middlewares"] = nlohmann::json::array({"stripprefix-all"});
            router["rule"] = "PathPrefix(`/" + entry.safeID + "/" + std::to_string(entry.sandboxPort) + "`)";
        }
        router["service"] = name;
        if (cfg_.enableTLS) {
            router["tls"] = nlohmann::json::object();
        }
        routersJson[name] = std::move(router);

        if (entry.routeKind == PortRouteKind::PUBLIC && !cfg_.publicBaseDomain.empty()) {
            nlohmann::json hostRouter;
            hostRouter["entryPoints"] = nlohmann::json::array({cfg_.httpEntryPoint});
            // Example:
            //   port=5888, safeID=akernel-abc, publicBaseDomain=sandbox-gateway.example.com
            //   => Host(`5888-akernel-abc.sandbox-gateway.example.com`)
            const std::string hostName = BuildHostLabel(entry.sandboxPort, entry.safeID) + "." + cfg_.publicBaseDomain;
            if (hostName.length() > MAX_FQDN_LEN) {
                YRLOG_WARN("TraefikRouteCache: skip host-based route for router {} because host '{}' is too long",
                           name, hostName);
            } else {
                const std::string hostRule = "Host(`" + hostName + "`)";
                hostRouter["rule"] = hostRule;
                hostRouter["service"] = name;
                if (cfg_.enableTLS) {
                    hostRouter["tls"] = nlohmann::json::object();
                }
                routersJson[BuildHostRouterName(name)] = std::move(hostRouter);
            }
        }

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
    middlewares["stripprefix-tunnel"]["stripPrefixRegex"]["regex"] =
        nlohmann::json::array({"^/tunnel/[^/]+"});

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
