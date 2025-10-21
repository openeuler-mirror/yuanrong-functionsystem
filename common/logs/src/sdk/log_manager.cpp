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

#include "logs/sdk/log_manager.h"

#include <sys/prctl.h>

#include <string>
#include <thread>

#include "logs/api/provider.h"

namespace observability::sdk::logs {
namespace LogsApi = observability::api::logs;

const std::string LOG_ROLLING_COMPRESS = "LOG_ROLLING_COMPRESS";

void LogManager::StartRollingCompress(const std::function<void(LogsApi::LogParam &)> &func)
{
    if (!logParam_.compressEnable) {
        LOGS_CORE_INFO("log compress is disabled");
        return;
    }
    LOGS_CORE_DEBUG("start log rolling compress process.");

    if (state_ == State::INITED) {
        state_ = State::RUNNING;
    } else {
        LOGS_CORE_WARN("failed to start rolling compress, the state is not INITED");
        return;
    }
    // Reduce the number of threads
    rollingCompressThread_ = std::thread(&LogManager::CronTask, this, func);
}

void LogManager::StopRollingCompress() noexcept
{
    {
        std::unique_lock<std::mutex> l(mtx_);
        if (state_ == State::RUNNING) {
            state_ = State::STOPPED;
        } else {
            LOGS_CORE_INFO("log rolling compress is not running.");
            return;
        }
        rcCond_.notify_all();
    }
    rollingCompressThread_.join();
    LOGS_CORE_DEBUG("stop log rolling compress complete.");
}

void LogManager::CronTask(const std::function<void(LogsApi::LogParam &)> &func)
{
    (void)prctl(PR_SET_NAME, LOG_ROLLING_COMPRESS.c_str());
    std::unique_lock<std::mutex> l(mtx_);
    while (state_ == State::RUNNING) {
        std::cv_status cs = rcCond_.wait_for(l, std::chrono::seconds(interval_));
        if (cs == std::cv_status::no_timeout) {
            LOGS_CORE_DEBUG("thread wake up by app thread, do last log manage work and ready to exit.");
        }
        l.unlock();
        try {
            func(logParam_);
        } catch (std::exception &ex) {
            LOGS_CORE_WARN("get error: {}", ex.what());
        }
        l.lock();
    }
}

}  // namespace observability::sdk::logs