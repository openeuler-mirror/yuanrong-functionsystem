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

#ifndef OBSERVABILITY_API_LOGS_LOGGER_PROVIDER_H
#define OBSERVABILITY_API_LOGS_LOGGER_PROVIDER_H

#include <memory>

#include "logs/api/logger.h"
#include "logs/api/log_param.h"

namespace yr_spdlog {
class logger;
}

namespace observability::api::logs {
using YrLogger = std::shared_ptr<yr_spdlog::logger>;
class Logger;
class LoggerProvider {
public:
    virtual ~LoggerProvider() = default;
    virtual std::shared_ptr<Logger> GetLogger(const std::string &loggerName) noexcept = 0;
    virtual YrLogger GetYrLogger(const std::string &loggerName) noexcept = 0;
    virtual YrLogger CreateYrLogger(const LogParam &logParam) noexcept = 0;
    virtual void DropYrLogger(const std::string &loggerName) noexcept = 0;
};
}  // namespace observability::api::logs

#endif