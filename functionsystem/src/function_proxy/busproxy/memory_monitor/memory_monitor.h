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

#ifndef BUSPROXY_MEMORY_MONITOR_H
#define BUSPROXY_MEMORY_MONITOR_H

#include "common/proto/pb/posix_pb.h"
#include "system_memory_collector.h"

namespace functionsystem {

const float DEFAULT_BUSPROXY_LOW_MEMORY_THRESHOLD = 0.6;
const float DEFAULT_BUSPROXY_HIGH_MEMORY_THRESHOLD = 0.8;
const uint64_t DEFAULT_BUSPROXY_MESSAGE_SIZE_THRESHOLD = 20 * 1024;
const uint64_t MAXIMUM_BUSPROXY_MESSAGE_SIZE_THRESHOLD = 50 * 1024;

struct MemoryControlConfig {
    bool enable{ false };
    float lowMemoryThreshold{ DEFAULT_BUSPROXY_LOW_MEMORY_THRESHOLD };
    float highMemoryThreshold{ DEFAULT_BUSPROXY_HIGH_MEMORY_THRESHOLD };
    uint64_t msgSizeThreshold{ DEFAULT_BUSPROXY_MESSAGE_SIZE_THRESHOLD };
};

class MemoryMonitor {
public:
    explicit MemoryMonitor(const MemoryControlConfig& config);
    ~MemoryMonitor() = default;

    /**
     * Check whether the invoke request can be processed.
     */
    bool Allow(const std::string &instanceID, const std::string &requestID, uint64_t msgSize);

    /**
     * release estimate memory of instance after finish invoke process.
     */
    void ReleaseEstimateMemory(const std::string &instanceID, const std::string &requestID);

    /**
     * check whether invoke limitation is enabled or not.
     */
    bool IsEnabled() const
    {
        return config_.enable;
    }

    void RefreshActualMemoryUsage() const;
    void StopRefreshActualMemoryUsage() const;

    // for test
    [[maybe_unused]] std::shared_ptr<SystemMemoryCollector> GetCollector() const
    {
        return actor_;
    }

    // for test
    [[maybe_unused]] uint64_t GetEstimateUsage() const
    {
        return estimateUsage_;
    }

    // for test
    [[maybe_unused]] std::unordered_map<std::string, uint64_t> GetFunctionMemMap() const
    {
        return instanceUsageMap_;
    }

    // for test
    [[maybe_unused]] void SetConfigForTest(const MemoryControlConfig config)
    {
        config_ = config;
    }

    // for test
    [[maybe_unused]] const MemoryControlConfig GetConfigForTest()
    {
        return config_;
    }

private:
    void AllocateEstimateMemory(const std::string &instanceID, const std::string &requestID, uint64_t msgSize);
    uint64_t GetInstanceUsage(const std::string &instanceID);
    uint64_t GetAverageUsage(uint64_t estimateUsage);
    std::atomic<uint64_t> estimateUsage_ = 0;
    std::unordered_map<std::string, uint64_t> instanceUsageMap_;  // key:instanceID, value:msgSizeOfInstance
    std::unordered_map<std::string, uint64_t> requestSizeMap_;  // key:requestID, value:msgSizeOfRequest
    MemoryControlConfig config_ {};
    std::shared_ptr<SystemMemoryCollector> actor_{ nullptr };
    std::mutex mapMtx_;
};
}   // namespace functionsystem

#endif  // BUSPROXY_BUSINESS_INSTANCE_ACTOR_MEMORY_MONITOR_H
