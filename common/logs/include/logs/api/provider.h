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

#ifndef OBSERVABILITY_API_LOGS_PROVIDER_H
#define OBSERVABILITY_API_LOGS_PROVIDER_H

#include <spdlog/spdlog.h>

#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>

#include "logs/api/null.h"

namespace observability::api::logs {

class LoggerProvider;

class Provider {
public:
    static std::shared_ptr<LoggerProvider> GetLoggerProvider() noexcept
    {
        std::shared_lock<std::shared_mutex> lock(GetLock());
        return std::shared_ptr<LoggerProvider>(GetProvider());
    }

    static void SetLoggerProvider(const std::shared_ptr<LoggerProvider> &tp) noexcept
    {
        std::unique_lock<std::shared_mutex> lock(GetLock());
        GetProvider() = tp;
    }

private:
    static std::shared_ptr<LoggerProvider> &GetProvider() noexcept
    {
        static std::shared_ptr<LoggerProvider> provider = std::make_shared<NullLoggerProvider>();
        return provider;
    }

    static std::shared_mutex &GetLock() noexcept
    {
        static std::shared_mutex lock;
        return lock;
    }
};

#define LOGS_LOGGER_CALL SPDLOG_LOGGER_CALL

#define LOGS_LEVEL_TRACE yr_spdlog::level::trace
#define LOGS_LEVEL_DEBUG yr_spdlog::level::debug
#define LOGS_LEVEL_INFO yr_spdlog::level::info
#define LOGS_LEVEL_WARN yr_spdlog::level::warn
#define LOGS_LEVEL_ERROR yr_spdlog::level::err
#define LOGS_LEVEL_FATAL yr_spdlog::level::critical
#define LOGS_LEVEL_OFF yr_spdlog::level::off

#define LOGS_LOGGER(logger, level, ...)                                    \
    do {                                                                   \
        if (logger == nullptr) {                                           \
            break;                                                         \
        }                                                                  \
        try {                                                              \
            LOGS_LOGGER_CALL(logger, level, __VA_ARGS__);                  \
        } catch (std::exception & e) {                                     \
            std::cerr << e.what() << std::endl;                            \
        }                                                                  \
        if (level == LOGS_LEVEL_FATAL) {                                   \
            (void)raise(SIGINT);                                           \
        }                                                                  \
    } while (0)

inline YrLogger GetCoreLogger()
{
    auto lp = Provider::GetLoggerProvider();
    if (lp == nullptr) {
        std::cerr << "No logger provider available" << std::endl;
        return nullptr;
    }
    return lp->GetYrLogger("CoreLogger");
}

#define LOGS_CORE_LOGGER(level, ...) LOGS_LOGGER(observability::api::logs::GetCoreLogger(), level, __VA_ARGS__)

#define LOGS_CORE_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define LOGS_CORE_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define LOGS_CORE_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define LOGS_CORE_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define LOGS_CORE_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)

}  // namespace observability::api::logs

#endif