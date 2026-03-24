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

#ifndef FUNCTION_MASTER_TENANT_QUOTA_MANAGER_H
#define FUNCTION_MASTER_TENANT_QUOTA_MANAGER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "async/future.hpp"
#include "common/status/status.h"
#include "httpd/http_connect.hpp"

namespace functionsystem {

struct TenantQuota {
    int64_t cpuQuota{ -1 };  // CPU quota in millicores, -1 = not set
    int64_t memQuota{ -1 };  // Memory quota in MB, -1 = not set
};

class TenantQuotaManager {
public:
    explicit TenantQuotaManager(const std::string &iamServerAddr);
    ~TenantQuotaManager();

    Status Start();
    Status Stop();

    // Register a tenant for quota polling (called when tenant is first encountered)
    void RegisterTenant(const std::string &tenantId);

    // Get quota for a tenant (registers the tenant if not already tracked)
    TenantQuota GetQuota(const std::string &tenantId);

private:
    void PollQuotas();

    std::string iamServerAddr_;
    std::atomic<bool> running_{ false };
    std::unique_ptr<std::thread> pollThread_;
    mutable std::mutex quotaMutex_;
    std::unordered_map<std::string, TenantQuota> quotas_;
    std::set<std::string> tenantsToPoll_;  // Tenants registered for polling
};

}  // namespace functionsystem

#endif  // FUNCTION_MASTER_TENANT_QUOTA_MANAGER_H
