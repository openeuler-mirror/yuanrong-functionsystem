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

#ifndef OBSERVABILITY_API_LOGS_NULL_H
#define OBSERVABILITY_API_LOGS_NULL_H

#include <memory>

#include "logs/api/logger.h"
#include "logs/api/logger_provider.h"

namespace observability::api::logs {
class NullLogger final : public Logger {
public:
    ~NullLogger() override = default;
    const std::string GetName() noexcept override
    {
        return "null logger";
    }
};

class NullLoggerProvider final : public LoggerProvider {
public:
    NullLoggerProvider() : logger_{ std::make_shared<NullLogger>() }
    {
    }

    std::shared_ptr<Logger> GetLogger(const std::string & /* loggerName */) noexcept override
    {
        return logger_;
    }

    YrLogger GetYrLogger(const std::string & /* loggerName */) noexcept override
    {
        return nullptr;
    }

    YrLogger CreateYrLogger(const LogParam & /* logParam */) noexcept override
    {
        return nullptr;
    }

    void DropYrLogger(const std::string & /* loggerName */) noexcept override
    {
    }

private:
    std::shared_ptr<Logger> logger_;
};
}  // namespace observability::api::logs

#endif