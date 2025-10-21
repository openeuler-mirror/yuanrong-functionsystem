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

#ifndef DEF_BUSLOG_HPP
#define DEF_BUSLOG_HPP

#include <sstream>

#include "logs/api/provider.h"
#include "actor/buserrcode.hpp"

// Reducing debug and info logs, litebus logs are recorded in the log file of the functionsystem.
#define BUSLOG_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_TRACE, __VA_ARGS__)
#define BUSLOG_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define BUSLOG_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define BUSLOG_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define BUSLOG_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)

constexpr int DLEVEL4 = 1000;
constexpr int DLEVEL3 = 3;
constexpr int DLEVEL2 = 2;
constexpr int DLEVEL1 = 1;
constexpr int DLEVEL0 = 0;

#define BUS_ASSERT(expression)                                                                             \
    do {                                                                                                   \
        if (!(expression)) {                                                                               \
            std::stringstream ss;                                                                          \
            ss << "Assertion failed: " << #expression << ", file: " << __FILE__ << ", line: " << __LINE__; \
            BUSLOG_FATAL(ss.str());                                                                        \
        }                                                                                                  \
    } while (0)

#define BUS_EXIT(ret)                                                               \
    do {                                                                            \
        std::stringstream ss;                                                       \
        ss << (ret) << "  ( file: " << __FILE__ << ", line: " << __LINE__ << " )."; \
        BUSLOG_FATAL(ss.str());                                                     \
    } while (0)

#define BUS_OOM_EXIT(ptr)                          \
    {                                              \
        if ((ptr) == nullptr) {                    \
            BUSLOG_ERROR("new failed, will exit"); \
            BUS_EXIT("Exit for OOM.");             \
        }                                          \
    }

constexpr int LOG_CHECK_EVERY_FIRSTNUM = 10;
constexpr int LOG_CHECK_EVERY_NUM1 = 10;
constexpr int LOG_CHECK_EVERY_NUM2 = 100;
constexpr int LOG_CHECK_EVERY_NUM3 = 1000;
constexpr int LOG_CHECK_EVERY_NUM4 = 10000;

#define LOG_CHECK_ID_CONCAT(word1, word2) word1##word2

#define LOG_CHECK_ID LOG_CHECK_ID_CONCAT(__FUNCTION__, __LINE__)

#define LOG_CHECK_FIRST_N                  \
    [](uint32_t firstNum) {                \
        static uint32_t LOG_CHECK_ID = 0;  \
        ++LOG_CHECK_ID;                    \
        return (LOG_CHECK_ID <= firstNum); \
    }

#define LOG_CHECK_EVERY_N1                                                \
    [](uint32_t firstNum, uint32_t num) {                                 \
        static uint32_t LOG_CHECK_ID = 0;                                 \
        ++LOG_CHECK_ID;                                                   \
        return ((LOG_CHECK_ID <= firstNum) || (LOG_CHECK_ID % num == 0)); \
    }

#define LOG_CHECK_EVERY_N2                                                                      \
    [](uint32_t firstNum, uint32_t num1, uint32_t num2) {                                       \
        static uint32_t LOG_CHECK_ID = 0;                                                       \
        ++LOG_CHECK_ID;                                                                         \
        return ((LOG_CHECK_ID <= firstNum) || (LOG_CHECK_ID < num2 && LOG_CHECK_ID % num1 == 0) \
                || (LOG_CHECK_ID % num2 == 0));                                                 \
    }

#define LOG_CHECK_EVERY_N3                                                                           \
    [](uint32_t firstNum, uint32_t num1, uint32_t num2, uint32_t num3) {                             \
        static uint32_t LOG_CHECK_ID = 0;                                                            \
        ++LOG_CHECK_ID;                                                                              \
        return ((LOG_CHECK_ID <= firstNum) || (LOG_CHECK_ID < num2 && LOG_CHECK_ID % num1 == 0)      \
                || (LOG_CHECK_ID < num3 && LOG_CHECK_ID % num2 == 0) || (LOG_CHECK_ID % num3 == 0)); \
    }

#define LOG_CHECK_EVERY_N4                                                                           \
    [](uint32_t firstNum, uint32_t num1, uint32_t num2, uint32_t num3, uint32_t num4) {              \
        static uint32_t LOG_CHECK_ID = 0;                                                            \
        ++LOG_CHECK_ID;                                                                              \
        return ((LOG_CHECK_ID <= firstNum) || (LOG_CHECK_ID < num2 && LOG_CHECK_ID % num1 == 0)      \
                || (LOG_CHECK_ID < num3 && LOG_CHECK_ID % num2 == 0)                                 \
                || (LOG_CHECK_ID < num4 && LOG_CHECK_ID % num3 == 0) || (LOG_CHECK_ID % num4 == 0)); \
    }

#define LOG_CHECK_EVERY_N                                                                            \
    []() {                                                                                           \
        static uint32_t LOG_CHECK_ID = 0;                                                            \
        ++LOG_CHECK_ID;                                                                              \
        return ((LOG_CHECK_ID <= LOG_CHECK_EVERY_FIRSTNUM)                                           \
                || (LOG_CHECK_ID < LOG_CHECK_EVERY_NUM2 && LOG_CHECK_ID % LOG_CHECK_EVERY_NUM1 == 0) \
                || (LOG_CHECK_ID < LOG_CHECK_EVERY_NUM3 && LOG_CHECK_ID % LOG_CHECK_EVERY_NUM2 == 0) \
                || (LOG_CHECK_ID < LOG_CHECK_EVERY_NUM4 && LOG_CHECK_ID % LOG_CHECK_EVERY_NUM3 == 0) \
                || (LOG_CHECK_ID % LOG_CHECK_EVERY_NUM4 == 0));                                      \
    }

#endif
