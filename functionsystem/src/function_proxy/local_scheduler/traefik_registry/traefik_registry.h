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
    struct PortMapping {
        int sandboxPort;
        int hostPort;
    };

    TraefikRegistry(std::shared_ptr<MetaStorageAccessor> accessor,
                    const std::string& keyPrefix,
                    const std::string& httpEntryPoint = "websecure",
                    bool enableTLS = true,
                    bool useBackendTLS = false,
                    const std::string& serversTransport = "yr-backend-tls@file");
    ~TraefikRegistry() = default;

    litebus::Future<Status> RegisterInstance(
        const std::string& instanceID,
        const std::string& hostIP,
        const std::vector<PortMapping>& portMappings);

    litebus::Future<Status> UnregisterInstance(const std::string& instanceID);

private:
    static std::string SanitizeID(const std::string& id);

    std::shared_ptr<MetaStorageAccessor> accessor_;
    std::string keyPrefix_;
    std::string httpEntryPoint_;
    bool enableTLS_;
    bool useBackendTLS_;
    std::string serversTransport_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_TRAEFIK_REGISTRY_H
