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

#include "profile_timer.h"

#include <thread>

#include "common/logs/logging.h"
#include "profiler.h"

namespace functionsystem {

ProfileTimer::ProfileTimer(const std::string &name) : name_(name)
{
    startTimePoint_ = std::chrono::steady_clock::now();
}

ProfileTimer::~ProfileTimer() noexcept
{
    if (!stopped_) {
        StopTimer();
    }
}

void ProfileTimer::StopTimer()
{
    try {
        auto endTimePoint = std::chrono::steady_clock::now();
        auto start = std::chrono::duration<double, std::micro>{ startTimePoint_.time_since_epoch() };
        auto elapsedTime = std::chrono::time_point_cast<std::chrono::microseconds>(endTimePoint).time_since_epoch() -
                           std::chrono::time_point_cast<std::chrono::microseconds>(startTimePoint_).time_since_epoch();

        Profiler::Get().WriteProfile({ name_, start, elapsedTime, std::this_thread::get_id() });

        stopped_ = true;
    } catch (const std::exception &e) {
        YRLOG_ERROR("failed in profileTimer stop");
    }
}

}  // namespace functionsystem