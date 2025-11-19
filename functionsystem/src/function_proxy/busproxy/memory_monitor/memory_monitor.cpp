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

#include "memory_monitor.h"

#include <actor/actor.hpp>
#include <async/async.hpp>
#include <utils/os_utils.hpp>

#include "common/logs/logging.h"

namespace functionsystem {

MemoryMonitor::MemoryMonitor(const MemoryControlConfig &config) : config_(config)
{
    std::string name = "SystemMemoryCollector_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    actor_ = std::make_shared<SystemMemoryCollector>(name);
    litebus::Spawn(actor_);
}

bool MemoryMonitor::Allow(const std::string &instanceID, const std::string &requestID, uint64_t msgSize)
{
    ASSERT_IF_NULL(actor_);
    auto limitUsage = actor_->GetLimit();
    auto currentUsage = actor_->GetCurrent();
    uint64_t estimateUsage = estimateUsage_.load();
    uint64_t highThreshold = static_cast<uint64_t>(limitUsage * config_.highMemoryThreshold);
    uint64_t lowThreshold = static_cast<uint64_t>(limitUsage * config_.lowMemoryThreshold);

    YRLOG_INFO("{}|{}|memory usage (cur{}/est{}/lmt{}), message size ({}Bytes)", requestID,
               instanceID, currentUsage, estimateUsage, limitUsage, msgSize);

    if ((UINT64_MAX - currentUsage < msgSize) || (currentUsage > UINT64_MAX - msgSize)) {
        YRLOG_WARN("{}|{}|memory usage {} + {} is oversize reject request.", requestID, instanceID, currentUsage,
                   msgSize);
        return false;
    }

    if (currentUsage + msgSize > highThreshold) {
        YRLOG_WARN("{}|{}|memory usage (cur{}/est{}/lmt{}) reaches high threshold({}), reject request.", requestID,
                   instanceID, currentUsage, estimateUsage, limitUsage, config_.highMemoryThreshold);
        return false;
    }
    if (msgSize <= static_cast<uint64_t>(config_.msgSizeThreshold)) {
        return true;
    }
    // count estimate memory usage of the request (estimated usage above msgSizeThreshold)
    if (currentUsage <= lowThreshold && estimateUsage <= lowThreshold) {
        AllocateEstimateMemory(instanceID, requestID, msgSize);
        return true;
    }
    uint64_t instanceUsage = GetInstanceUsage(instanceID);
    uint64_t averageUsage = GetAverageUsage(estimateUsage);
    if (instanceUsage == 0) {
        AllocateEstimateMemory(instanceID, requestID, msgSize);
        return true;
    }
    if (instanceUsage <= averageUsage) {
        AllocateEstimateMemory(instanceID, requestID, msgSize);
        return true;
    }
    YRLOG_WARN(
        "{}|{}|memory usage (cur{}/est{}/lmt{}) reaches low threshold({}), reject request (estimate "
        "usage {} exceeds average ({}Bytes).",
        requestID, instanceID, currentUsage, estimateUsage, limitUsage, config_.lowMemoryThreshold, instanceUsage,
        averageUsage);
    return false;
}

void MemoryMonitor::RefreshActualMemoryUsage() const
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SystemMemoryCollector::RefreshActualMemoryUsage);
}

void MemoryMonitor::StopRefreshActualMemoryUsage() const
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &SystemMemoryCollector::StopRefreshActualMemoryUsage);
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

void MemoryMonitor::AllocateEstimateMemory(const std::string &instanceID, const std::string &requestID,
                                           uint64_t msgSize)
{
    std::lock_guard<std::mutex> lockGuard(mapMtx_);
    estimateUsage_ = (UINT64_MAX - estimateUsage_ < msgSize) ? UINT64_MAX : (estimateUsage_ + msgSize);
    if (instanceUsageMap_.find(instanceID) != instanceUsageMap_.end()) {
        auto instanceUsage = instanceUsageMap_[instanceID];
        instanceUsageMap_[instanceID] = (UINT64_MAX - msgSize < instanceUsage) ? UINT64_MAX : (instanceUsage + msgSize);
    } else {
        instanceUsageMap_[instanceID] = msgSize;
    }
    requestSizeMap_[requestID] = msgSize;
}

void MemoryMonitor::ReleaseEstimateMemory(const std::string &instanceID, const std::string &requestID)
{
    std::lock_guard<std::mutex> lockGuard(mapMtx_);
    if (requestSizeMap_.find(requestID) == requestSizeMap_.end()) {
        return;
    }
    auto msgSize = requestSizeMap_[requestID];
    if (estimateUsage_.load() < msgSize) {
        estimateUsage_.store(0);
    } else {
        estimateUsage_ -= msgSize;
    }
    if (instanceUsageMap_[instanceID] <= msgSize) {
        (void)instanceUsageMap_.erase(instanceID);
    } else {
        instanceUsageMap_[instanceID] -= msgSize;
    }
    (void)requestSizeMap_.erase(requestID);
}

uint64_t MemoryMonitor::GetInstanceUsage(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lockGuard(mapMtx_);
    if (instanceUsageMap_.find(instanceID) != instanceUsageMap_.end()) {
        return instanceUsageMap_[instanceID];
    }
    return 0;
}

uint64_t MemoryMonitor::GetAverageUsage(uint64_t estimateUsage)
{
    std::lock_guard<std::mutex> lockGuard(mapMtx_);
    return static_cast<uint64_t>(estimateUsage / (instanceUsageMap_.size() + 1));
}

}  // namespace functionsystem