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

#include "logs/sdk/logger_provider.h"

#include <atomic>
#include <memory>
#include <mutex>

#include <cassert>
#include "logger_context.h"
#include "logs/sdk/logger.h"

namespace observability::sdk::logs {
namespace LogsApi = observability::api::logs;

LoggerProvider::LoggerProvider() noexcept : context_{ std::make_shared<LoggerContext>() }
{
}

LoggerProvider::LoggerProvider(const observability::api::logs::GlobalLogParam &globalLogParam) noexcept
    : context_{ std::make_shared<LoggerContext>(globalLogParam) }
{
}

LoggerProvider::~LoggerProvider()
{
    if (context_) {
        (void)context_->Shutdown();
    }
}

LogsApi::YrLogger LoggerProvider::GetYrLogger(const std::string &loggerName) noexcept
{
    assert((context_) != nullptr);
    return context_->GetLogger(loggerName);
}

LogsApi::YrLogger LoggerProvider::CreateYrLogger(const LogsApi::LogParam &logParam) noexcept
{
    assert((context_) != nullptr);
    auto logger = context_->GetLogger(logParam.loggerName);
    if (logger != nullptr) {
        return logger;
    }
    return context_->CreateLogger(logParam);
}

void LoggerProvider::DropYrLogger(const std::string &loggerName) noexcept
{
    assert((context_) != nullptr);
    (void)context_->DropLogger(loggerName);
}

std::shared_ptr<LogsApi::Logger> LoggerProvider::GetLogger(const std::string &loggerName) noexcept
{
    if (loggerName.empty()) {
        return nullptr;
    }
    std::lock_guard<std::mutex> guard{ lock_ };
    if (loggers_.find(loggerName) != loggers_.end()) {
        return loggers_.at(loggerName);
    }
    auto logger = std::make_shared<Logger>(loggerName, context_);
    loggers_[loggerName] = logger;
    return logger;
}

bool LoggerProvider::Shutdown() noexcept
{
    assert((context_) != nullptr);
    return context_->Shutdown();
}

bool LoggerProvider::ForceFlush(std::chrono::microseconds) noexcept
{
    assert((context_) != nullptr);
    return context_->ForceFlush();
}

}  // namespace observability::sdk::logs