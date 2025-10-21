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

#include "sdk/include/observe_actor.h"

#include "common/logs/log.h"

namespace observability::sdk::metrics {

const uint16_t SEC2MS = 1000;

ObserveActor::~ObserveActor()
{
    for (auto timerInfo : std::as_const(collectTimerMap_)) {
        auto timer = timerInfo.second;
        (void)litebus::TimerTools::Cancel(timer);
    }
    collectTimerMap_.clear();
    collectIntervals_.clear();
}

void ObserveActor::RegisterTimer(const int interval)
{
    if (interval <= 0) {
        METRICS_LOG_ERROR("Invalid interval {}", interval);
        return;
    }
    if (auto it = collectIntervals_.find(interval); it == collectIntervals_.end()) {
        METRICS_LOG_DEBUG("Register observable instrument timer {}", interval);
        (void)collectIntervals_.insert(interval);
        litebus::AsyncAfter(interval * SEC2MS, GetAID(), &ObserveActor::StartCollect, interval);
    }
}

void ObserveActor::StartCollect(const int interval)
{
    METRICS_LOG_DEBUG("Start to collect {} observable instrument", interval);
    (void)litebus::Async(GetAID(), &ObserveActor::Collect, interval);
    collectTimerMap_[interval] =
        litebus::AsyncAfter(interval * SEC2MS, GetAID(), &ObserveActor::StartCollect, interval);
}

void ObserveActor::Collect(const int interval)
{
    return collectFunc_(interval);
}
}
