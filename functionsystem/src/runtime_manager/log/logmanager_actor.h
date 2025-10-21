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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_ACTOR_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_ACTOR_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "status/status.h"
#include "runtime_manager/config/flags.h"

#include <exec/exec.hpp>
#include <algorithm>
#include <ctime>
#include <queue>
#include <unordered_set>

namespace functionsystem::runtime_manager {

struct LogExpiratioinConfig {
    bool enable;
    int cleanupInterval;
    int timeThreshold;
    int maxFileCount;
};

// RuntimeLogFile represents a unique runtime log
// - Java's runtime log, Java runtime log dir is named in the format of "runtimeId", and there are 3 files below.
// - C++ runtime log, named in the format of "jobId-runtimeId.log", and rolling files "jobId-runtimeId.1.log",
//   "jobId-runtimeId.2.log", or rolling compression files "jobId-runtimeId.1.log.gz", "jobId-runtimeId.2.log.gz"
// - Python runtime log, named in the format of "runtimeId.log", and rolling files or compression files as shown above
class RuntimeLogFile {
public:
    RuntimeLogFile(const std::string &runtimeID, const std::string &path, time_t modTime, bool isDir = false)
        : runtimeID_(runtimeID), filePath_(path), modificationTime_(modTime), isDir_(isDir)
    {}

    std::string GetRuntimeID() const
    {
        return runtimeID_;
    }

    bool IsDir() const
    {
        return isDir_;
    }

    time_t GetModificationTime() const
    {
        return modificationTime_;
    }

    const std::string &GetFilePath() const
    {
        return filePath_;
    }

    // sort by descending order of modifiction time
    bool operator<(const RuntimeLogFile &other) const
    {
        return (modificationTime_ != other.modificationTime_)
                   ? modificationTime_ > other.modificationTime_
                   : (isDir_ && !other.isDir_   ? true  // dir last to delete
                      : !isDir_ && other.isDir_ ? false
                                                : true);
    }

private:
    std::string runtimeID_;
    std::string filePath_;
    time_t modificationTime_;
    bool isDir_{false};
};

// ExpiredLogQueue records expired RuntimeLogFile in order of their last modification time.
class ExpiredLogQueue {
public:
    void AddLogFile(const RuntimeLogFile &logFile);

    bool IsLogFileExist(const RuntimeLogFile &logFile);

    size_t GetLogCount() const;

    bool DeleteOldestRuntimeLogFile();

private:
    std::unordered_set<std::string> filePathSet_;
    std::priority_queue<RuntimeLogFile> queue_;
};

class LogManagerActor : public litebus::ActorBase {
public:
    explicit LogManagerActor(const std::string &name, const litebus::AID &runtimeManagerAID);

    ~LogManagerActor() override = default;

    void SetConfig(const Flags &flags);

    void ScanLogsRegularly();

    void StopScanLogs();

    void CleanLogs();

    virtual litebus::Future<bool> IsRuntimeActive(const std::string &runtimeID) const;

    litebus::Future<bool> CppAndPythonRuntimeLogProcess(const bool &isActive, const std::string &runtimeID,
                                                          const std::string &filePath, const time_t &nowTimeStamp);

    litebus::Future<bool> JavaRuntimeDirProcess(const bool &isActive, const std::string &runtimeID,
                                                  const std::string &filePath, const time_t &nowTimeStamp);

protected:
    void Init() override;
    void Finalize() override;

private:
    LogExpiratioinConfig logExpirationConfig_;
    litebus::Timer scanLogsTimer_;
    std::shared_ptr<ExpiredLogQueue> expiredLogQueue_;
    std::string runtimeLogsPath_;
    std::string runtimeStdLogDir_;
    litebus::AID runtimeManagerAID_;

    std::string GetJavaRuntimeIDFromLogDirName(const std::string &file, const std::string &filePath);

    std::string GetRuntimeIDFromLogFileName(const std::string &file, const std::string &filePath);

    litebus::Future<bool> CollectAddFilesFuture(const std::list<litebus::Future<bool>> &adds);
};

}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_ACTOR_H
