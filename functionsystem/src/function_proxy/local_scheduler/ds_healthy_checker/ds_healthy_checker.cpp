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
#include "ds_healthy_checker.h"

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"

namespace functionsystem::local_scheduler {

DsHealthyChecker::DsHealthyChecker(uint64_t checkInterval, uint64_t maxUnHealthyTimes,
                                   std::shared_ptr<DistributedCacheClient> distributedCacheClient)
    : litebus::ActorBase("DsHealthyChecker"),
      checkInterval_(checkInterval),
      maxUnHealthyTimes_(maxUnHealthyTimes),
      distributedCacheClient_(std::move(distributedCacheClient))
{
}

void DsHealthyChecker::Init()
{
    litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::InitCheck);
}

void DsHealthyChecker::InitCheck()
{
    if (const auto dataSystemFeatureUsed = litebus::os::GetEnv("DATA_SYSTEM_FEATURE_USED");
        dataSystemFeatureUsed.IsSome()) {
        dataSystemFeatureUsed_ = dataSystemFeatureUsed.Get();
        YRLOG_INFO("healthy checker used datasystem feature: {}", dataSystemFeatureUsed_);
    }

    YRLOG_INFO("first check ds worker isUnhealthy({})", isUnhealthy_.load());
    if (healthyCallback_) {
        healthyCallback_(!isUnhealthy_);
    }
    litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
}

void DsHealthyChecker::Check()
{
    const Status healthStatus = distributedCacheClient_->GetHealthStatus();
    if (healthStatus.IsOk()) {
        failedTimes_ = 0;
        // unhealthy -> healthy
        if (isUnhealthy_ && healthyCallback_) {
            YRLOG_INFO("ds worker is recovered.");
            healthyCallback_(true);
            isUnhealthy_.store(false);
        }
        (void)litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
        return;
    }
    failedTimes_++;
    if (failedTimes_ == maxUnHealthyTimes_
        || (healthStatus.StatusCode() == DS_SCALE_DOWN && dataSystemFeatureUsed_ == DATASYSTEM_FEATURE_USED_STREAM)) {
        YRLOG_ERROR("check times reached limitation {}, ds worker is not healthy, or status:{}, err:{}, msg:{}",
                    maxUnHealthyTimes_, fmt::underlying(healthStatus.StatusCode()), errno,
                    litebus::os::Strerror(errno));
        // healthy -> unhealthy
        if (!isUnhealthy_ && healthyCallback_) {
            healthyCallback_(false);
        }
        isUnhealthy_.store(true);
    }
    (void)litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
}

bool DsHealthyChecker::GetIsUnhealthy()
{
    return isUnhealthy_;
}

bool DsHealthyChecker::IsDataSystemFeatureUsed()
{
    return dataSystemFeatureUsed_ == DATASYSTEM_FEATURE_USED_STREAM;
}
}  // namespace functionsystem::local_scheduler