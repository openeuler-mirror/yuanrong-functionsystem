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

#ifndef OBSERVABILITY_SDK_LOGS_LOGGER_CONTEXT_H
#define OBSERVABILITY_SDK_LOGS_LOGGER_CONTEXT_H

#include <chrono>
#include <unordered_map>

#include <spdlog/async.h>

#include "logs/api/logger.h"
#include "logs/api/logger_provider.h"
#include "logs/sdk/log_param_parser.h"

namespace observability::sdk::logs {
namespace LogsApi = observability::api::logs;

class LoggerContext {
public:
    LoggerContext() noexcept;
    explicit LoggerContext(const LogsApi::GlobalLogParam &globalLogParam) noexcept;
    ~LoggerContext();

    bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) const noexcept;

    bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds::max()) const noexcept;

    LogsApi::YrLogger CreateLogger(const LogsApi::LogParam &logParam) const noexcept;

    LogsApi::YrLogger GetLogger(const std::string &loggerName) const noexcept;

    void DropLogger(const std::string &loggerName) const noexcept;

private:
    void Initialize() noexcept;
    void InitCoreLogger(bool needToInit);
    void FlushCoreLogger() const;

    LogsApi::GlobalLogParam globalLogParam_;
};

}  // namespace observability::sdk::logs

#endif
