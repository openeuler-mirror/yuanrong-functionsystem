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

#ifndef OBSERVABILITY_SDK_LOGS_LOGGER_PROVIDER_H
#define OBSERVABILITY_SDK_LOGS_LOGGER_PROVIDER_H

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "logs/api/logger_provider.h"
#include "logs/api/log_param.h"

namespace observability::sdk::logs {
class Logger;
class LoggerContext;

class LoggerProvider final : public observability::api::logs::LoggerProvider {
public:
    LoggerProvider() noexcept;
    explicit LoggerProvider(const observability::api::logs::GlobalLogParam &globalLogParam) noexcept;
    ~LoggerProvider() override;

    using observability::api::logs::LoggerProvider::GetLogger;
    std::shared_ptr<observability::api::logs::Logger> GetLogger(const std::string &loggerName) noexcept override;
    using observability::api::logs::LoggerProvider::GetYrLogger;
    observability::api::logs::YrLogger GetYrLogger(const std::string &loggerName) noexcept override;
    using observability::api::logs::LoggerProvider::CreateYrLogger;
    observability::api::logs::YrLogger CreateYrLogger(const observability::api::logs::LogParam &logParam) noexcept
        override;
    using observability::api::logs::LoggerProvider::DropYrLogger;
    void DropYrLogger(const std::string &loggerName) noexcept override;

    bool Shutdown() noexcept;

    bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept;

private:
    std::unordered_map<std::string, std::shared_ptr<observability::sdk::logs::Logger>> loggers_;
    std::shared_ptr<LoggerContext> context_{ nullptr };
    std::mutex lock_;
};
}  // namespace observability::sdk::logs

#endif