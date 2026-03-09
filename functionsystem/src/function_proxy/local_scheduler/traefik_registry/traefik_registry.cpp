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
#include <regex>

#include "common/logs/logging.h"

namespace functionsystem::local_scheduler {

TraefikRegistry::TraefikRegistry(std::shared_ptr<MetaStorageAccessor> accessor,
                                   const std::string& keyPrefix,
                                   const std::string& httpEntryPoint,
                                   bool enableTLS,
                                   const std::string& serversTransport)
    : accessor_(std::move(accessor)),
      keyPrefix_(keyPrefix),
      httpEntryPoint_(httpEntryPoint),
      enableTLS_(enableTLS),
      serversTransport_(serversTransport)
{
    // Validate serversTransport format (needed for 'https' protocol ports)
    if (serversTransport_.empty()) {
        YRLOG_WARN("TraefikRegistry: serversTransport is empty, 'https' protocol ports will not use backend TLS");
    } else {
        // Validate format: should be like "name@provider" (e.g., "yr-backend-tls@file")
        static const std::regex transportPattern(R"(^[^@]+@[^@]+$)");
        if (!std::regex_match(serversTransport_, transportPattern)) {
            YRLOG_ERROR("TraefikRegistry: invalid serversTransport format '{}', expected 'name@provider' (e.g., 'yr-backend-tls@file')",
                       serversTransport_);
            throw std::invalid_argument("serversTransport must be in format 'name@provider' (e.g., 'yr-backend-tls@file')");
        }
        YRLOG_INFO("TraefikRegistry: 'https' protocol ports will use backend TLS with ServersTransport: {}", serversTransport_);
    }

    YRLOG_INFO("TraefikRegistry initialized: keyPrefix={}, httpEntryPoint={}, enableTLS={}, serversTransport={}",
              keyPrefix_, httpEntryPoint_, enableTLS_, serversTransport_);

    // Create global StripPrefix middleware for all instances
    // This middleware removes /{instanceID}/{port} prefix using regex
    if (accessor_) {
        accessor_->Put(keyPrefix_ + "/http/middlewares/stripprefix-all/stripPrefixRegex/regex",
                       "^/[^/]+/[0-9]+")
            .Then([](const Status& status) -> Status {
                if (!status.IsOk()) {
                    YRLOG_WARN("Failed to create global StripPrefix middleware: {}", status.GetMessage());
                } else {
                    YRLOG_INFO("Global StripPrefix middleware created successfully");
                }
                return Status::OK();
            });
    }
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

    // Build key-value pairs for HTTP routing based on path prefix
    // Each port needs 5-6 keys: rule, service, middlewares/0, entryPoints/0, tls (optional), loadbalancer url
    // Uses global StripPrefix middleware to remove /{instanceID}/{port} prefix
    std::vector<std::pair<std::string, std::string>> kvs;

    for (const auto& mapping : portMappings) {
        std::string routerName = safeID + "-p" + std::to_string(mapping.sandboxPort);
        // URL path format: /{safeID}/{sandbox_port}
        // This format must match Python sandbox._build_gateway_url() for consistency
        std::string prefixPath = "/" + safeID + "/" + std::to_string(mapping.sandboxPort);

        // Determine backend protocol based on protocol field
        // "https" (case-insensitive) -> HTTPS backend with serversTransport
        // Any other value -> HTTP backend without serversTransport
        std::string protocolLower = mapping.protocol;
        std::transform(protocolLower.begin(), protocolLower.end(), protocolLower.begin(), ::tolower);
        bool useHttpsForBackend = (protocolLower == "https");
        std::string scheme = useHttpsForBackend ? "https" : "http";

        YRLOG_DEBUG("TraefikRegistry: instance {} port {} protocol '{}' -> backend {}",
                   instanceID, mapping.sandboxPort, mapping.protocol, scheme);

        // HTTP router rule: PathPrefix(`/{instanceID}/{port}`)
        std::string ruleValue = "PathPrefix(`" + prefixPath + "`)";
        kvs.push_back({keyPrefix_ + "/http/routers/" + routerName + "/rule", ruleValue});

        // HTTP router service name
        kvs.push_back({keyPrefix_ + "/http/routers/" + routerName + "/service", routerName});

        // HTTP router middlewares - use global StripPrefix middleware
        kvs.push_back({keyPrefix_ + "/http/routers/" + routerName + "/middlewares/0", "stripprefix-all"});

        // HTTP router entryPoint (websecure for HTTPS, web for HTTP)
        kvs.push_back({keyPrefix_ + "/http/routers/" + routerName + "/entryPoints/0", httpEntryPoint_});

        // HTTP router TLS (optional, empty value enables TLS)
        if (enableTLS_) {
            kvs.push_back({keyPrefix_ + "/http/routers/" + routerName + "/tls", ""});
        }

        // HTTP service loadbalancer URL: http(s)://hostIP:hostPort
        std::ostringstream urlStream;
        urlStream << scheme << "://" << hostIP << ":" << mapping.hostPort;
        kvs.push_back({keyPrefix_ + "/http/services/" + routerName + "/loadbalancer/servers/0/url",
                       urlStream.str()});

        // Only add serversTransport for https protocol ports
        if (useHttpsForBackend) {
            if (!serversTransport_.empty()) {
                kvs.push_back({keyPrefix_ + "/http/services/" + routerName + "/loadbalancer/serverstransport",
                               serversTransport_});
                YRLOG_DEBUG("TraefikRegistry: port {} using HTTPS with serversTransport={}",
                           mapping.sandboxPort, serversTransport_);
            } else {
                YRLOG_WARN("TraefikRegistry: port {} has 'https' protocol but serversTransport is empty, skipping TLS",
                           mapping.sandboxPort);
            }
        }
    }

    YRLOG_INFO("Registering instance {} to Traefik HTTP: {} ports, {} keys",
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

    YRLOG_INFO("Unregistering instance {} from Traefik HTTP", instanceID);

    std::string safeID = SanitizeID(instanceID);
    std::string routerPrefix = keyPrefix_ + "/http/routers/" + safeID;
    std::string servicePrefix = keyPrefix_ + "/http/services/" + safeID;

    return accessor_->Delete(routerPrefix, true)
        .Then([this, servicePrefix](const Status& status) {
            if (!status.IsOk()) {
                YRLOG_WARN("Failed to delete Traefik HTTP routers: {}", status.GetMessage());
            }
            return accessor_->Delete(servicePrefix, true);
        })
        .Then([instanceID](const Status& status) -> Status {
            if (!status.IsOk()) {
                YRLOG_WARN("Failed to unregister instance {} from Traefik HTTP: {}", instanceID, status.GetMessage());
            } else {
                YRLOG_INFO("Successfully unregistered instance {} from Traefik HTTP", instanceID);
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
