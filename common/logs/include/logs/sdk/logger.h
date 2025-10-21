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

#ifndef OBSERVABILITY_SDK_LOGS_LOGGER_H
#define OBSERVABILITY_SDK_LOGS_LOGGER_H

#include <memory>
#include <string>

#include "logs/api/logger.h"

namespace observability::sdk::logs {
class LoggerContext;
class Logger final : public observability::api::logs::Logger {
public:
    ~Logger() override = default;
    Logger(const std::string &name, const std::shared_ptr<LoggerContext> &context) noexcept;

    const std::string GetName() noexcept override;

private:
    std::string loggerName_;
    std::shared_ptr<LoggerContext> loggerContext_;
};
}  // namespace observability::sdk::logs

#endif