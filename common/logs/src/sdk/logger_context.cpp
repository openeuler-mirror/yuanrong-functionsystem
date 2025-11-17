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

#include "logger_context.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <iostream>
#include <map>

namespace observability::sdk::logs {

static void FlushLogger(std::shared_ptr<yr_spdlog::logger> l)
{
    if (l == nullptr) {
        return;
    }
    l->flush();
}

static const std::map<std::string, yr_spdlog::level::level_enum> &GetLogLevelMap()
{
    static const std::map<std::string, yr_spdlog::level::level_enum> LOG_LEVEL_MAP = {
        { "DEBUG", yr_spdlog::level::debug },
        { "INFO", yr_spdlog::level::info },
        { "WARN", yr_spdlog::level::warn },
        { "ERROR", yr_spdlog::level::err },
        { "FATAL", yr_spdlog::level::critical }
    };
    return LOG_LEVEL_MAP;
}

static yr_spdlog::level::level_enum GetLogLevel(const std::string &level)
{
    auto iter = GetLogLevelMap().find(level);
    return iter == GetLogLevelMap().end() ? yr_spdlog::level::info : iter->second;
}

LoggerContext::LoggerContext() noexcept
{
    yr_spdlog::drop_all();
}

LoggerContext::LoggerContext(const LogsApi::GlobalLogParam &globalLogParam) noexcept : globalLogParam_(globalLogParam)
{
    yr_spdlog::drop_all();
    yr_spdlog::init_thread_pool(static_cast<size_t>(globalLogParam_.maxAsyncQueueSize),
                             static_cast<size_t>(globalLogParam_.asyncThreadCount));
    yr_spdlog::flush_every(std::chrono::seconds(globalLogParam_.logBufSecs));
}

LoggerContext::~LoggerContext()
{
}

LogsApi::YrLogger LoggerContext::CreateLogger(const LogsApi::LogParam &logParam) const noexcept
{
    try {
        std::vector<yr_spdlog::sink_ptr> sinks{};
        std::string logFile = GetLogFile(logParam);
        auto rotatingSink = std::make_shared<yr_spdlog::sinks::rotating_file_sink_mt>(
            logFile, logParam.maxSize * LogsApi::SIZE_MEGA_BYTES, logParam.maxFiles);
        (void)sinks.emplace_back(rotatingSink);

        if (logParam.alsoLog2Std) {
            auto consoleSink = std::make_shared<yr_spdlog::sinks::stdout_color_sink_mt>();
            const auto logLevel = GetLogLevel(logParam.stdLogLevel);
            consoleSink->set_level(logLevel);
            (void)sinks.emplace_back(consoleSink);
        }
        std::shared_ptr<yr_spdlog::logger> logger;
        if (logParam.syncFlush) {
            logger = std::make_shared<yr_spdlog::logger>(logParam.loggerName, sinks.begin(), sinks.end());
        } else {
            logger = std::make_shared<yr_spdlog::async_logger>(logParam.loggerName, sinks.begin(), sinks.end(),
                                                               yr_spdlog::thread_pool(),
                                                               yr_spdlog::async_overflow_policy::block);
        }
        yr_spdlog::initialize_logger(logger);

        const auto logLevel = GetLogLevel(logParam.logLevel);
        logger->set_level(logLevel);

        // log with international UTC time
        logger->set_pattern(logParam.pattern, yr_spdlog::pattern_time_type::utc);
        return logger;
    } catch (std::exception &e) {
        std::cerr << "failed to init logger, error: " << e.what() << std::endl;
        return nullptr;
    }
}

LogsApi::YrLogger LoggerContext::GetLogger(const std::string &loggerName) const noexcept
{
    return yr_spdlog::get(loggerName);
}

void LoggerContext::DropLogger(const std::string &loggerName) const noexcept
{
    yr_spdlog::drop(loggerName);
}

bool LoggerContext::ForceFlush(std::chrono::microseconds) const noexcept
{
    yr_spdlog::apply_all(FlushLogger);
    return true;
}

bool LoggerContext::Shutdown(std::chrono::microseconds) const noexcept
{
    yr_spdlog::apply_all(FlushLogger);
    return true;
}
}  // namespace observability::sdk::logs