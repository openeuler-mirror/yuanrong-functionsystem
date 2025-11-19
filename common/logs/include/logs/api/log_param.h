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

#ifndef OBSERVABILITY_SDK_LOGS_LOG_PARAM_H
#define OBSERVABILITY_SDK_LOGS_LOG_PARAM_H

#include <memory>
#include <string>

namespace observability::api::logs {

const int DEFAULT_MAX_SIZE = 100;  // 100 MB
const int DEFAULT_RETENTION_DAYS = 30;
const int DEFAULT_LOG_BUF_SECONDS = 30;
const uint32_t DEFAULT_MAX_FILES = 3;
const uint32_t DEFAULT_MAX_ASYNC_QUEUE_SIZE = 1024;  // 1024(every log length)
const uint32_t DEFAULT_ASYNC_THREAD_COUNT = 1;
const int64_t SIZE_MEGA_BYTES = 1024 * 1024;  // 1 MB

struct LogParam {
    std::string loggerName;
    std::string logLevel;
    std::string logDir;
    std::string nodeName;
    std::string modelName;
    std::string pattern;
    std::string fileNamePattern;
    bool logFileWithTime = false;
    bool alsoLog2Std = false;
    bool compressEnable = true;
    int maxSize = DEFAULT_MAX_SIZE;
    int retentionDays = DEFAULT_RETENTION_DAYS;
    uint32_t maxFiles = DEFAULT_MAX_FILES;
    std::string stdLogLevel;
    bool syncFlush = false;
};

struct GlobalLogParam {
    int logBufSecs = DEFAULT_LOG_BUF_SECONDS;
    uint32_t maxAsyncQueueSize = DEFAULT_MAX_ASYNC_QUEUE_SIZE;
    uint32_t asyncThreadCount = DEFAULT_ASYNC_THREAD_COUNT;
};
}  // namespace observability::sdk::logs

#endif