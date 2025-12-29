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

#include "logs/sdk/log_handler.h"

#include <spdlog/details/file_helper.h>

#include <chrono>
#include <map>
#include <sstream>
#include <string>

#include "fileutils.h"
#include "logs/api/provider.h"
#include "logs/api/log_param.h"

using namespace std::chrono;

namespace observability::sdk::logs {

const static int DAY_MILLISECONDS = 24 * 60 * 60 * 1000;

void LogRollingCompress(const observability::api::logs::LogParam &logParam)
{
    DoLogFileCompress(logParam);
    DoLogFileRolling(logParam);
}

void DoLogFileRolling(const observability::api::logs::LogParam &logParam)
{
    // 1st: get log files based on regular expressions.
    std::vector<std::string> files;
    // glog gzip filename format: <nodeName>-<modelName>.<time>.log.gz
    std::stringstream ss;
    std::string logFileExtensions = "*[0-9]\\";
    if (!logParam.logFileExtensions.empty()) {
        logFileExtensions += logParam.logFileExtensions;
    } else {
        logFileExtensions += ".log";
    }
    ss << logParam.logDir.c_str() << "/" << logParam.fileNamePattern.c_str() << "\\."
       << "*[0-9]\\.log"
       << "\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, files);

    // 2nd: calculate the total size of the log files and get their timestamp.
    std::map<int64_t, FileUnit> fileMap;
    for (auto &file : files) {
        size_t size = FileSize(file);
        int64_t timestamp;
        GetFileModifiedTime(file, timestamp);
        if (!fileMap.emplace(timestamp, FileUnit(file, size)).second) {
            LOGS_CORE_WARN("timestamp emplace error, maybe cause by duplicate timestamp:{}, {},{}", file, size,
                           timestamp);
        }
    }

    // 3rd: delete the oldest files.
    int redundantNum = (fileMap.size() <= logParam.maxFiles) ? 0 : static_cast<int>(fileMap.size() - logParam.maxFiles);
    for (const auto &file : fileMap) {
        auto curTime = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        bool needDelByTime =
            (curTime - file.first / 1000) > ((static_cast<int64_t>(logParam.retentionDays)) * DAY_MILLISECONDS);
        bool needDelByNum = redundantNum > 0;
        if (!needDelByTime && !needDelByNum) {
            break;
        }
        DeleteFile(file.second.name);
        redundantNum--;
    }
    return;
}

void DoLogFileCompress(const observability::api::logs::LogParam &logParam)
{
    // 1st: get log files based on regular expressions.
    std::vector<std::string> files;
    // function system log filename format: <nodeName>-<modelName>.<idx>.log
    std::stringstream ss;
    std::string logFileExtensions = "*[0-9]\\";
    logParam.logFileExtensions.empty() ? logFileExtensions += ".log" : logFileExtensions += logParam.logFileExtensions;
    ss << logParam.logDir.c_str() << "/" << logParam.fileNamePattern.c_str() << "\\."
       << "*[0-9]\\.log";
    std::string pattern = ss.str();
    Glob(pattern, files);

    // 2nd: compress these file in '.gz' format
    for (const auto &file : files) {
        int64_t timestamp;
        GetFileModifiedTime(file, timestamp);

        // e.g: xxx-function_agent.1.log -> xxx-function_agent.{TIME}.log -> xxx-function_agent.{TIME}.log.gz
        std::string basename, ext, idx;
        std::tie(basename, ext) = yr_spdlog::details::file_helper::split_by_extension(file);
        std::tie(basename, idx) = yr_spdlog::details::file_helper::split_by_extension(basename);
        std::string targetFile = basename + "." + std::to_string(timestamp) + ext;
        if (!RenameFile(file, targetFile)) {
            LOGS_CORE_WARN("failed to rename {} to {}", file, targetFile);
            continue;
        }

        std::string gzFile = targetFile + ".gz";
        // Compress the file and delete the origin file, we just need the compress files!
        int ret = CompressFile(targetFile, gzFile);
        if (ret != 0) {
            LOGS_CORE_WARN("failed to compress log file: {}", targetFile);
            continue;
        }
        DeleteFile(targetFile);
    }
}
}  // namespace observability::sdk::logs
