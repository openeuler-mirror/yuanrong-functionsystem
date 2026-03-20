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

#include "tenant_quota_manager.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

#include "common/logs/logging.h"

namespace functionsystem {

namespace {
const int POLL_INTERVAL_SECONDS = 60;
const int HTTP_TIMEOUT_MS = 5000;
}  // namespace

TenantQuotaManager::TenantQuotaManager(const std::string &iamServerAddr) : iamServerAddr_(iamServerAddr)
{
}

TenantQuotaManager::~TenantQuotaManager()
{
    Stop();
}

Status TenantQuotaManager::Start()
{
    if (running_.load()) {
        return Status::OK();
    }

    if (iamServerAddr_.empty()) {
        YRLOG_WARN("TenantQuotaManager: IAM server address not configured, quota polling disabled");
        return Status::OK();
    }

    running_.store(true);
    pollThread_ = std::make_unique<std::thread>(&TenantQuotaManager::PollQuotas, this);
    YRLOG_INFO("TenantQuotaManager started, polling every {} seconds", POLL_INTERVAL_SECONDS);

    return Status::OK();
}

Status TenantQuotaManager::Stop()
{
    if (!running_.load()) {
        return Status::OK();
    }

    running_.store(false);
    if (pollThread_ && pollThread_->joinable()) {
        pollThread_->join();
    }
    YRLOG_INFO("TenantQuotaManager stopped");

    return Status::OK();
}

void TenantQuotaManager::RegisterTenant(const std::string &tenantId)
{
    std::lock_guard<std::mutex> lock(quotaMutex_);
    if (tenantsToPoll_.insert(tenantId).second) {
        YRLOG_INFO("Registered tenant {} for quota polling", tenantId);
    }
}

TenantQuota TenantQuotaManager::GetQuota(const std::string &tenantId)
{
    std::lock_guard<std::mutex> lock(quotaMutex_);
    auto it = quotas_.find(tenantId);
    if (it != quotas_.end()) {
        return it->second;
    }
    // Register tenant for polling on first access
    const_cast<std::set<std::string> &>(tenantsToPoll_).insert(tenantId);
    YRLOG_INFO("Auto-registered tenant {} for quota polling", tenantId);
    return TenantQuota{};
}

void TenantQuotaManager::PollQuotas()
{
    YRLOG_INFO("TenantQuotaManager: starting quota polling");

    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));

        if (!running_.load()) {
            break;
        }

        std::vector<std::string> tenantsToQuery;
        {
            std::lock_guard<std::mutex> lock(quotaMutex_);
            for (const auto &tenantId : tenantsToPoll_) {
                tenantsToQuery.push_back(tenantId);
            }
        }

        if (tenantsToQuery.empty()) {
            YRLOG_DEBUG("TenantQuotaManager: no tenants registered for polling");
            continue;
        }

        for (const auto &tenantId : tenantsToQuery) {
            if (!running_.load()) {
                break;
            }

            std::string url = "http://" + iamServerAddr_ + "/v1/tenant/quota?tenant_id=" + tenantId;

            litebus::Try<litebus::http::URL> parsedUrl = litebus::http::URL::Decode(url);
            if (parsedUrl.IsError()) {
                YRLOG_ERROR("Failed to parse quota URL for tenant {}: {}", tenantId, parsedUrl.GetErrorCode());
                continue;
            }

            litebus::Future<litebus::http::Response> response =
                litebus::http::Get(parsedUrl.Get(), litebus::None(), HTTP_TIMEOUT_MS);
            response.Wait();

            if (response.IsError()) {
                YRLOG_ERROR("Failed to query quota for tenant {}: {}", tenantId, response.GetErrorCode());
                continue;
            }

            const auto &httpResponse = response.Get();
            if (httpResponse.retCode != litebus::http::OK) {
                YRLOG_ERROR("HTTP error querying quota for tenant {}: {} - {}", tenantId, httpResponse.retCode,
                            httpResponse.body);
                continue;
            }

            try {
                auto json = nlohmann::json::parse(httpResponse.body);
                TenantQuota quota;
                if (json.contains("cpu_quota")) {
                    quota.cpuQuota = json["cpu_quota"].get<int64_t>();
                }
                if (json.contains("mem_quota")) {
                    quota.memQuota = json["mem_quota"].get<int64_t>();
                }

                {
                    std::lock_guard<std::mutex> lock(quotaMutex_);
                    quotas_[tenantId] = quota;
                }

                YRLOG_DEBUG("Updated quota for tenant {}: cpu={}, mem={}", tenantId, quota.cpuQuota, quota.memQuota);
            } catch (const std::exception &e) {
                YRLOG_ERROR("Failed to parse quota response for tenant {}: {}", tenantId, e.what());
            }
        }
    }

    YRLOG_INFO("TenantQuotaManager: polling stopped");
}

}  // namespace functionsystem
