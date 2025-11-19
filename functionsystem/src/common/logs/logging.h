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

#ifndef COMMON_LOGS_LOGGING_H
#define COMMON_LOGS_LOGGING_H

#include <csignal>

#include "counter.h"
#include "logs/api/provider.h"
#include "logs/sdk/log_handler.h"
#include "logs/sdk/log_manager.h"
#include "logs/sdk/logger_provider.h"

#define YRLOG_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define YRLOG_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define YRLOG_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define YRLOG_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define YRLOG_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)

#define YRLOG_DEBUG_COUNT_60(...) YRLOG_DEBUG_COUNT(60, __VA_ARGS__)
#define YRLOG_DEBUG_COUNT(frequent, ...)  \
    do {                                  \
        static Counter counter(frequent); \
        if (counter.Proc()) {             \
            YRLOG_DEBUG(__VA_ARGS__);     \
        }                                 \
    } while (false)

// write log if expression is true
#define YRLOG_DEBUG_IF(expr, ...)     \
    do {                              \
        if ((expr)) {                 \
            YRLOG_DEBUG(__VA_ARGS__); \
        }                             \
    } while (false)

inline void KillProcess(const std::string &ret)
{
    YRLOG_ERROR("Exit Tip: {}", ret);
    (void)raise(SIGINT);
}

#define YR_EXIT(ret)                                                                \
    do {                                                                            \
        std::stringstream ss;                                                       \
        ss << (ret) << "  ( file: " << __FILE__ << ", line: " << __LINE__ << " )."; \
        KillProcess(ss.str());                                                      \
    } while (0)

#define EXIT_IF_NULL(ptr)                                          \
    {                                                              \
        if ((ptr) == nullptr) {                                    \
            YRLOG_ERROR("ptr{} null, will exit", #ptr);            \
            YR_EXIT("Exit for Bad alloc or Dynamic cast failed."); \
        }                                                          \
    }

#endif