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

#ifndef FUNCTION_PROXY_COMMON_DS_HEALTHY_CHECKER_DS_HEALTHY_CHECKER_H
#define FUNCTION_PROXY_COMMON_DS_HEALTHY_CHECKER_DS_HEALTHY_CHECKER_H

#include <atomic>

#include "actor/actor.hpp"
#include "common/distribute_cache_client/distributed_cache_client.h"

namespace functionsystem::local_scheduler {
class DsHealthyChecker : public litebus::ActorBase {
public:
    DsHealthyChecker(uint64_t checkInterval, uint64_t maxUnHealthyTimes,
                     std::shared_ptr<DistributedCacheClient> distributedCacheClient);

    ~DsHealthyChecker() override = default;

    void SubscribeDsHealthy(const std::function<void(bool)> &cb)
    {
        healthyCallback_ = cb;
    }

    bool GetIsUnhealthy();

    bool IsDataSystemFeatureUsed();

protected:
    std::atomic<bool> isUnhealthy_{ false };

    void Init() override;

private:
    void Check();

    void InitCheck();

    std::string dataSystemFeatureUsed_;

    uint64_t checkInterval_;
    uint64_t maxUnHealthyTimes_;
    std::shared_ptr<DistributedCacheClient> distributedCacheClient_{};
    std::function<void(bool)> healthyCallback_;
    uint32_t failedTimes_{ 0 };
};
}  // namespace functionsystem::local_scheduler

#endif  // FUNCTION_PROXY_COMMON_DS_HEALTHY_CHECKER_DS_HEALTHY_CHECKER_H
