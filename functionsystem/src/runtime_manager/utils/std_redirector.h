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

#ifndef RUNTIME_MANAGER_EXECUTOR_STD_OUT_REDIRECTOR_H
#define RUNTIME_MANAGER_EXECUTOR_STD_OUT_REDIRECTOR_H

#include <fstream>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/future.hpp"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "utils/constants.h"

namespace functionsystem {
// redirect
const int32_t MAX_LOG_LENGTH = 1024 * 1024;  // 1MB
const int32_t FLUSH_DURATION = 10000;        // 10s

// user func std logger rolling
const unsigned long STD_ROLLING_MAX_FILE_SIZE = 100;  // MB
const unsigned long STD_ROLLING_MAX_FILES = 100;

const std::string ERROR_LEVEL = "ERROR";
const std::string INFO_LEVEL = "INFO";
const std::string STD_POSTFIX = "-user_func_std.log";
const std::string DEFAULT_LOG_FILE_EXTENSIONS = ".log";
const std::string DEFAULT_LOG_FILE_OUT_EXTENSIONS = ".out";

struct StdRedirectParam {
    int32_t maxLogLength{ MAX_LOG_LENGTH };
    int32_t flushDuration{ FLUSH_DURATION };
    unsigned long stdRollingMaxFileSize{ STD_ROLLING_MAX_FILE_SIZE };
    unsigned long stdRollingMaxFiles{ STD_ROLLING_MAX_FILES };
    std::string exportMode { functionsystem::runtime_manager::FILE_EXPORTER };
    std::string logFileExtensions{ DEFAULT_LOG_FILE_EXTENSIONS };
};

struct RuntimeStandardLog {
    std::string time;
    std::string level;
    std::string instanceID;
    std::string runtimeID;
    std::string message;
};

struct LogInfo {
    int32_t length = 0;
    std::stringstream message;
};

class StdRedirector : public litebus::ActorBase {
public:
    StdRedirector(const std::string &path, const std::string &logName)
        : litebus::ActorBase(logName), path_(path), logName_(logName){};

    StdRedirector(const std::string &path, const std::string &logName, int32_t maxLogLength, int32_t flushDuration)
        : litebus::ActorBase(logName), path_(path), logName_(logName)
    {
        param_.maxLogLength = maxLogLength;
        param_.flushDuration = flushDuration;
        param_.stdRollingMaxFileSize = STD_ROLLING_MAX_FILE_SIZE;
        param_.stdRollingMaxFiles = STD_ROLLING_MAX_FILES;
        param_.logFileExtensions = DEFAULT_LOG_FILE_EXTENSIONS;
    }

    StdRedirector(const std::string &path, const std::string &logName, const StdRedirectParam &param)
        : litebus::ActorBase(logName), path_(path), logName_(logName), param_(param)
    {
    }

    ~StdRedirector() override;

    Status Start();
    void StartRuntimeStdRedirection(const std::string &runtimeID, const std::string &instanceID,
                                    const litebus::Option<int> stdOut, const litebus::Option<int> stdErr);
    static std::string GetStdLog(const std::string &logFile, const std::string &runtimeID, const std::string &level,
                                 int32_t targetLineCnt = 20, int32_t readLineCnt = 1000);
    void ReadStdLogRealTime(int fd, int event);

    void TerminatePipeReader(int fd);

private:
    void FlushToDisk(const RuntimeStandardLog &log);
    void SetStdLogContent(const std::string &content, const std::string &runtimeID,
                          const std::string &instanceID, const std::string &level);
    void SetStdLogRawContent(const std::string &content);
    void FlushLogContentRegularly();
    void SetTimer(const litebus::Timer &timer)
    {
        StopTimer();
        timer_ = timer;
    }

    void StopTimer()
    {
        (void)litebus::TimerTools::Cancel(timer_);
    }

    void MoveLogsToReady()
    {
        if (logs_.length <= 0) {
            return;
        }
        readyToFlushLogs_.message << logs_.message.str();
        readyToFlushLogs_.length += logs_.length;
        logs_.message.str("");
        logs_.message.clear();
        logs_.length = 0;
    }

    void ExportLog();
    void FlushToDiskDirectly();
    void FlushToStd();

    litebus::AID GetPipeAsyncReader(int fd);

    bool logFileNotExist_{ false };
    // (origin) logger
    std::string path_;
    std::string logName_;
    std::string logNameForLogsSdk_;

    // std redirect control
    StdRedirectParam param_;
    LogInfo logs_;
    LogInfo readyToFlushLogs_;
    litebus::Timer timer_;

    std::shared_ptr<yr_spdlog::logger> userStdLogger_;
    std::shared_ptr<observability::api::logs::LoggerProvider> lp_{ nullptr };
    std::shared_ptr<observability::sdk::logs::LogManager> logManager_{ nullptr };
    std::unordered_map<int, litebus::AID> fd2Readers_;

    void Finalize() override;
};
}  // namespace functionsystem

#endif  // RUNTIME_MANAGER_EXECUTOR_STD_OUT_REDIRECTOR_H
