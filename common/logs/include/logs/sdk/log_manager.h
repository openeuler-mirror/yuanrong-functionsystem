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

#ifndef OBSERVABILITY_SDK_LOGS_LOG_MANAGER_H
#define OBSERVABILITY_SDK_LOGS_LOG_MANAGER_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "logs/api/log_param.h"

namespace observability::sdk::logs {

const uint32_t DEFAULT_LOG_HANDLER_INTERVAL = 30;  // seconds

class LogManager {
public:
    explicit LogManager(const observability::api::logs::LogParam &logParam) : logParam_(logParam),
        interval_(DEFAULT_LOG_HANDLER_INTERVAL)
    {
    }

    ~LogManager() = default;

    void StartRollingCompress(const std::function<void(observability::api::logs::LogParam &)> &func);

    void StopRollingCompress() noexcept;

private:
    // Allows multiple tasks to run in parallel
    void CronTask(const std::function<void(observability::api::logs::LogParam &)> &func);

    observability::api::logs::LogParam logParam_;
    uint32_t interval_;

    std::thread rollingCompressThread_;
    std::condition_variable rcCond_;
    std::mutex mtx_;

    enum class State { INITED = 0, RUNNING, STOPPED };
    State state_ = State::INITED;
};

}  // namespace observability::sdk::logs

#endif  // LOGS_LOG_MANAGER_H
