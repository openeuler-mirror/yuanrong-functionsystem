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

#include "traefik_registry.h"

#include <sstream>

#include "common/logs/logging.h"

namespace functionsystem::local_scheduler {

TraefikRegistry::TraefikRegistry(std::shared_ptr<MetaStorageAccessor> accessor,
                                   const std::string& domain,
                                   const std::string& keyPrefix,
                                   const std::string& tcpEntryPoint)
    : accessor_(std::move(accessor)),
      domain_(domain),
      keyPrefix_(keyPrefix),
      tcpEntryPoint_(tcpEntryPoint)
{
    YRLOG_INFO("TraefikRegistry initialized: domain={}, keyPrefix={}, tcpEntryPoint={}",
              domain_, keyPrefix_, tcpEntryPoint_);
}

litebus::Future<Status> TraefikRegistry::RegisterInstance(
    const std::string& instanceID,
    const std::string& hostIP,
    const std::vector<PortMapping>& portMappings)
{
    if (!accessor_) {
        YRLOG_ERROR("TraefikRegistry: accessor is null, skip registration for instanceID={}", instanceID);
        return litebus::Future<Status>(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "meta storage accessor is null"));
    }

    if (portMappings.empty()) {
        YRLOG_INFO("No port mappings for instance {}, skip Traefik registration", instanceID);
        return Status::OK();
    }

    std::string safeID = SanitizeID(instanceID);

    // Build key-value pairs for TCP routing
    // Each port needs 5 keys: rule, service, entryPoints/0, tls, loadbalancer address
    std::vector<std::pair<std::string, std::string>> kvs;

    for (const auto& [sandboxPort, hostPort] : portMappings) {
        std::string routerName = safeID + "-p" + std::to_string(sandboxPort);

        // TCP router rule: HostSNI(`inst-001-p8080.example.com`)
        std::string ruleValue = "HostSNI(`" + routerName + "." + domain_ + "`)";
        kvs.push_back({keyPrefix_ + "/tcp/routers/" + routerName + "/rule", ruleValue});

        // TCP router service name
        kvs.push_back({keyPrefix_ + "/tcp/routers/" + routerName + "/service", routerName});

        // TCP router entryPoint
        kvs.push_back({keyPrefix_ + "/tcp/routers/" + routerName + "/entryPoints/0", tcpEntryPoint_});

        // TCP router TLS (empty value enables TLS)
        kvs.push_back({keyPrefix_ + "/tcp/routers/" + routerName + "/tls", ""});

        // TCP service loadbalancer address: hostIP:hostPort
        std::ostringstream addrStream;
        addrStream << hostIP << ":" << hostPort;
        kvs.push_back({keyPrefix_ + "/tcp/services/" + routerName + "/loadbalancer/servers/0/address",
                       addrStream.str()});
    }

    YRLOG_INFO("Registering instance {} to Traefik: {} ports, {} keys",
              instanceID, portMappings.size(), kvs.size());

    return accessor_->Txn(kvs)
        .Then([instanceID, portMappingsCount = portMappings.size()](const Status& status) -> Status {
            if (!status.IsOk()) {
                YRLOG_ERROR("Failed to register instance {} to Traefik: {}", instanceID, status.GetMessage());
                return status;
            }
            YRLOG_INFO("Successfully registered instance {} to Traefik with {} ports",
                      instanceID, portMappingsCount);
            return Status::OK();
        });
}

litebus::Future<Status> TraefikRegistry::UnregisterInstance(const std::string& instanceID)
{
    if (!accessor_) {
        YRLOG_WARN("TraefikRegistry: accessor is null, skip unregistration for instanceID={}", instanceID);
        return litebus::Future<Status>(Status::OK());
    }

    YRLOG_INFO("Unregistering instance {} from Traefik", instanceID);

    std::string safeID = SanitizeID(instanceID);
    std::string routerPrefix = keyPrefix_ + "/tcp/routers/" + safeID;
    std::string servicePrefix = keyPrefix_ + "/tcp/services/" + safeID;

    return accessor_->Delete(routerPrefix, true)
        .Then([this, servicePrefix](const Status& status) {
            if (!status.IsOk()) {
                YRLOG_WARN("Failed to delete Traefik routers: {}", status.GetMessage());
            }
            return accessor_->Delete(servicePrefix, true);
        })
        .Then([instanceID](const Status& status) -> Status {
            if (!status.IsOk()) {
                YRLOG_WARN("Failed to unregister instance {} from Traefik: {}", instanceID, status.GetMessage());
            } else {
                YRLOG_INFO("Successfully unregistered instance {} from Traefik", instanceID);
            }
            return Status::OK();
        });
}

std::string TraefikRegistry::SanitizeID(const std::string& id)
{
    std::string result = id;
    // Replace @ with -at- (Traefik doesn't allow @ in router/service names)
    size_t pos = 0;
    while ((pos = result.find('@', pos)) != std::string::npos) {
        result.replace(pos, 1, "-at-");
        pos += 4; // Skip past the replacement
    }
    // Replace other potentially problematic characters
    // Traefik router/service names should be DNS-compatible
    std::replace(result.begin(), result.end(), '/', '-');
    std::replace(result.begin(), result.end(), '.', '-');
    std::replace(result.begin(), result.end(), '_', '-');
    // Limit length (Traefik may have limits on router names)
    if (result.length() > 200) {
        result = result.substr(0, 200);
    }
    return result;
}

}  // namespace functionsystem::local_scheduler
