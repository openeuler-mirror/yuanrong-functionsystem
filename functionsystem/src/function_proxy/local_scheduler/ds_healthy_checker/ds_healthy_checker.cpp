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
#include "logs/logging.h"

namespace functionsystem::local_scheduler {

void DsHealthyChecker::Init()
{
    litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::InitCheck);
}

void DsHealthyChecker::InitCheck()
{
    YRLOG_INFO("first check ds worker isUnhealthy({})", isUnhealthy_);
    if (healthyCallback_) {
        healthyCallback_(!isUnhealthy_);
    }
    litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
}

void DsHealthyChecker::Check()
{
    if (distributedCacheClient_->GetHealthStatus().IsOk()) {
        failedTimes_ = 0;
        // 不健康 -> 健康
        if (isUnhealthy_ && healthyCallback_) {
            YRLOG_INFO("ds worker is recovered.");
            healthyCallback_(true);
            isUnhealthy_.store(false);
        }
        (void)litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
        return;
    }
    failedTimes_++;
    if (failedTimes_ == maxUnHealthyTimes_) {
        YRLOG_ERROR("check times reached limitation {}, ds worker is not healthy, error:{} str:{}", maxUnHealthyTimes_,
                    errno, litebus::os::Strerror(errno));
        // 健康 -> 不健康
        if (!isUnhealthy_ && healthyCallback_) {
            healthyCallback_(false);
        }
        isUnhealthy_.store(true);
    }
    (void)litebus::AsyncAfter(checkInterval_, GetAID(), &DsHealthyChecker::Check);
}
}  // namespace functionsystem::local_scheduler