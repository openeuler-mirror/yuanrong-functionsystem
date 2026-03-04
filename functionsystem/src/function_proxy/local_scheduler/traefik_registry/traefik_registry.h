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

#ifndef LOCAL_SCHEDULER_TRAEFIK_REGISTRY_H
#define LOCAL_SCHEDULER_TRAEFIK_REGISTRY_H

#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <async/future.hpp>
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/status/status.h"

namespace functionsystem::local_scheduler {

class TraefikRegistry {
public:
    // Port mapping: sandbox port (inside container) -> host port
    struct PortMapping {
        int sandboxPort;
        int hostPort;
    };

    TraefikRegistry(std::shared_ptr<MetaStorageAccessor> accessor,
                    const std::string& domain,
                    const std::string& keyPrefix,
                    int leaseTTL,
                    const std::string& tcpEntryPoint = "tcpsecure");
    ~TraefikRegistry() = default;

    // Register instance with multiple port mappings (TCP L4 routing)
    // All ports share one lease, managed by LeaseActor
    litebus::Future<Status> RegisterInstance(
        const std::string& instanceID,
        const std::string& hostIP,
        const std::vector<PortMapping>& portMappings);

    // Unregister instance (revokes the shared lease, all keys auto-deleted)
    litebus::Future<Status> UnregisterInstance(const std::string& instanceID);

private:
    // Replace characters not allowed in Traefik router/service names (e.g. @)
    static std::string SanitizeID(const std::string& id);

    std::shared_ptr<MetaStorageAccessor> accessor_;
    std::string domain_;       // e.g. "example.com"
    std::string keyPrefix_;    // e.g. "traefik"
    int leaseTTL_;            // milliseconds
    std::string tcpEntryPoint_;  // e.g. "tcpsecure"
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_TRAEFIK_REGISTRY_H
