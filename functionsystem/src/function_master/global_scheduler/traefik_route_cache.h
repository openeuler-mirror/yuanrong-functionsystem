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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_ROUTE_CACHE_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_ROUTE_CACHE_H

#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/resource_view/resource_type.h"

namespace functionsystem::global_scheduler {

struct TraefikConfig {
    std::string httpEntryPoint   = "websecure";
    bool        enableTLS        = true;
    std::string serversTransport = "yr-backend-tls@file";
};

class TraefikRouteCache {
public:
    explicit TraefikRouteCache(TraefikConfig cfg);
    ~TraefikRouteCache() = default;

    // Called when an instance enters RUNNING state.
    // Extracts route-relevant fields from InstanceInfo and updates routeTable.
    void OnInstanceRunning(const resource_view::InstanceInfo& instance);

    // Called when an instance exits (FATAL / EVICTED / EXITED / DELETE event).
    void OnInstanceExited(const std::string& instanceID);

    // Returns the current Traefik dynamic configuration as a JSON string.
    // When content is unchanged, returns byte-identical output (for Traefik FNV hash stability).
    std::string GetConfigJSON();

    // Visible for testing
    size_t GetRouteCount() const;

private:
    struct RouteEntry {
        std::string routerName;   // safeID-pPort
        std::string backendURL;   // https://hostIP:hostPort or http://hostIP:hostPort
        int         sandboxPort = 0;
        bool        useHttps    = false;
    };

    // Parse route entries from InstanceInfo extensions.
    // Parses extensions["portForward"] JSON array and proxyGrpcAddress.
    std::vector<RouteEntry> ParseRoutes(const resource_view::InstanceInfo& instance) const;

    // Extract IP from proxyGrpcAddress (ip:port format)
    static std::string ExtractIP(const std::string& addr);

    // Sanitize instanceID for use as Traefik router/service name
    static std::string SanitizeID(const std::string& id);

    // Build full Traefik dynamic.Configuration JSON.
    // JSON keys sorted lexicographically for FNV hash stability.
    std::string BuildConfigJSON() const;

    TraefikConfig cfg_;

    // instanceID → route entries for that instance (one instance may have multiple ports)
    std::unordered_map<std::string, std::vector<RouteEntry>> routeTable_;
    mutable std::shared_mutex routeTableMu_;

    // Configuration snapshot cache (rebuilt when dirty)
    std::string cachedJSON_;
    std::atomic<bool> dirty_{true};
    mutable std::mutex cacheMu_;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_TRAEFIK_ROUTE_CACHE_H
