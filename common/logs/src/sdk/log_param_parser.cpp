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

#include "logs/sdk/log_param_parser.h"

#include <chrono>
#include <csignal>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

#include "fileutils.h"

namespace observability::sdk::logs {
namespace LogsApi = observability::api::logs;

std::string FormatTimePoint()
{
    using std::chrono::system_clock;
    std::time_t tt = system_clock::to_time_t(system_clock::now());
    struct std::tm ptm {};
    (void)::localtime_r(&tt, &ptm);

    std::stringstream ss;
    ss << std::put_time(&ptm, "%Y%m%d%H%M%S");

    return ss.str();
}

std::string GetLogFile(const LogsApi::LogParam &param)
{
    std::string logFile;
    if (!param.fileNamePattern.empty()) {
        logFile = param.logDir + "/" + param.fileNamePattern;
    } else {
        logFile = param.logDir + "/" + param.nodeName + "-" + param.modelName;
    }
    if (param.logFileWithTime) {
        logFile += "-" + FormatTimePoint();
    }
    if (!param.logFileExtensions.empty()) {
        logFile += param.logFileExtensions;
    } else {
        logFile += ".log";
    }

    return logFile;
}

void ParseFilePath(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("filepath") != confJson.end()) {
        std::optional<std::string> dir = RealPath(confJson.at("filepath").get<std::string>());
        if (dir.has_value()) {
            logParam.logDir = dir.value();
        }
    }
}

void ParseLogLevel(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("level") != confJson.end()) {
        logParam.logLevel = confJson.at("level").get<std::string>();
    }
}

void ParseLogCompress(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("compress") != confJson.end()) {
        logParam.compressEnable = confJson.at("compress").get<bool>();
    }
}

void ParseLogRolling(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("rolling") != confJson.end()) {
        if (confJson.at("rolling").find("maxsize") != confJson.at("rolling").end()) {
            int size = confJson.at("rolling").at("maxsize").get<int>();
            if (size > 0) {
                logParam.maxSize = size;
            }
        }
        if (confJson.at("rolling").find("maxfiles") != confJson.at("rolling").end()) {
            int files = confJson.at("rolling").at("maxfiles").get<int>();
            if (files > 0) {
                logParam.maxFiles = static_cast<uint32_t>(files);
            }
        }
        if (confJson.at("rolling").find("retentionDays") != confJson.at("rolling").end()) {
            int retentionDays = confJson.at("rolling").at("retentionDays").get<int>();
            if (retentionDays > 0) {
                logParam.retentionDays = retentionDays;
            }
        }
    }
}

void ParseLogAsync(const nlohmann::json &confJson, LogsApi::GlobalLogParam &globalLogParam)
{
    if (confJson.find("async") != confJson.end()) {
        if (confJson.at("async").find("logBufSecs") != confJson.at("async").end()) {
            int bufSecs = confJson.at("async").at("logBufSecs").get<int>();
            if (bufSecs > 0) {
                globalLogParam.logBufSecs = bufSecs;
            }
        }
        if (confJson.at("async").find("maxQueueSize") != confJson.at("async").end()) {
            unsigned int queueSize = confJson.at("async").at("maxQueueSize").get<unsigned int>();
            if (queueSize > 0) {
                globalLogParam.maxAsyncQueueSize = queueSize;
            }
        }
        if (confJson.at("async").find("threadCount") != confJson.at("async").end()) {
            unsigned int cnt = confJson.at("async").at("threadCount").get<unsigned int>();
            if (cnt > 0) {
                globalLogParam.asyncThreadCount = cnt;
            }
        }
    }
}

void ParseAlsoLog2Std(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("alsologtostderr") != confJson.end()) {
        logParam.alsoLog2Std = confJson.at("alsologtostderr").get<bool>();
    }
    if (confJson.find("stdLogLevel") != confJson.end()) {
        logParam.stdLogLevel = confJson.at("stdLogLevel").get<std::string>();
    }
}

void ParseSyncFlush(const nlohmann::json &confJson, LogsApi::LogParam &logParam)
{
    if (confJson.find("syncFlush") != confJson.end()) {
        logParam.syncFlush = confJson.at("syncFlush").get<bool>();
    }
}

LogsApi::LogParam GetLogParam(const std::string &configJsonString, const std::string &nodeName,
    const std::string &modelName, const bool logFileWithTime, const std::string &fileNamePattern)
{
    LogsApi::LogParam logParam;
    logParam.nodeName = nodeName;
    logParam.modelName = modelName;
    logParam.fileNamePattern = fileNamePattern;
    logParam.logFileWithTime = logFileWithTime;

    const std::string defaultLoggerName = "CoreLogger";
    logParam.loggerName = defaultLoggerName;
    const std::string defaultLogPath = "/home/yr/log";
    logParam.logDir = defaultLogPath;
    const std::string defaultLogLevel = "INFO";
    logParam.logLevel = defaultLogLevel;
    logParam.pattern = "%L%m%d %H:%M:%S.%f %t %s:%#] %P,%!]" + logParam.nodeName + "," + logParam.modelName + "]%v";
    const std::string defaultStdLogLevel = "ERROR";
    logParam.stdLogLevel = defaultStdLogLevel;

    if (configJsonString.empty()) {
        return logParam;
    }
    try {
        auto confJson = nlohmann::json::parse(configJsonString);
        ParseFilePath(confJson, logParam);
        ParseLogLevel(confJson, logParam);
        ParseLogCompress(confJson, logParam);
        ParseLogRolling(confJson, logParam);
        ParseAlsoLog2Std(confJson, logParam);
        ParseSyncFlush(confJson, logParam);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        (void)raise(SIGINT);
    }

    return logParam;
}

LogsApi::GlobalLogParam GetGlobalLogParam(const std::string &configJsonString)
{
    LogsApi::GlobalLogParam globalLogParam;
    try {
        auto confJson = nlohmann::json::parse(configJsonString);
        ParseLogAsync(confJson, globalLogParam);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        (void)raise(SIGINT);
    }
    return globalLogParam;
}

}  // namespace observability::sdk::logs