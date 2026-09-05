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
#include "common/status/status.h"
#include "runtime_manager/config/flags.h"

#include <exec/exec.hpp>
#include <algorithm>
#include <ctime>
#include <queue>
#include <unordered_set>
#include <unistd.h>

namespace functionsystem::runtime_manager {

struct LogExpiratioinConfig {
    bool enable;
    int cleanupInterval;
    int timeThreshold;
    int maxFileCount;
};

// RuntimeLogRotateConfig drives one-shot logrotate invocations spawned by
// LogManagerActor: each active runtime log ({runtimeID}.log / {runtimeID}.std /
// {runtimeID}.out / {runtimeID}.err) that reaches maxSizeMb is
// copytruncate-rotated, keeping up to maxFiles archives ({runtimeID}.log.1..N,
// {runtimeID}.std.1..N, {runtimeID}.out.1..N, {runtimeID}.err.1..N). Global
// file count stays under the logExpiration LRU cleanup in this actor.
struct RuntimeLogRotateConfig {
    bool enable = false;
    int maxSizeMb = 0;
    int maxFiles = 0;
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

private:
    std::string runtimeID_;
    std::string filePath_;
    time_t modificationTime_;
    bool isDir_{false};
};

struct RuntimeLogFileComparator {
    bool operator()(const std::shared_ptr<RuntimeLogFile> &cur, const std::shared_ptr<RuntimeLogFile> &other) const
    {
        // sort by descending order of modification time
        if (cur->GetModificationTime() != other->GetModificationTime()) {
            return cur->GetModificationTime() > other->GetModificationTime();
        }
        // if modification time is same, dir is placed after file
        return cur->IsDir() && !other->IsDir();
    }
};

// ExpiredLogQueue records expired RuntimeLogFile in order of their last modification time.
class ExpiredLogQueue {
public:
    void AddLogFile(const std::shared_ptr<RuntimeLogFile> &logFile, bool needAddSet);

    bool IsLogFileExist(const std::shared_ptr<RuntimeLogFile> &logFile);

    size_t GetLogCount() const;

    bool DeleteOldestRuntimeLogFile();

    std::shared_ptr<RuntimeLogFile> PopLogFile();

    void Reset();

private:
    std::unordered_set<std::string> filePathSet_;
    std::priority_queue<
        std::shared_ptr<RuntimeLogFile>,
        std::vector<std::shared_ptr<RuntimeLogFile>>,
        RuntimeLogFileComparator> queue_;
};

class LogManagerActor : public litebus::ActorBase {
public:
    explicit LogManagerActor(const std::string &name, const litebus::AID &runtimeManagerAID, bool logReuse = false);

    ~LogManagerActor() override = default;

    void SetConfig(const Flags &flags);

    void ScanLogsRegularly();
    void StopScanLogs();

    litebus::Future<bool> CleanLogs();

    void StartScanLogs();

    void PartitionDeleteFile(const std::vector<std::shared_ptr<RuntimeLogFile>> &toBeDeleteFiles, size_t index,
                             litebus::Promise<bool> promise);

    virtual litebus::Future<bool> IsRuntimeActive(const std::string &runtimeID) const;

    virtual litebus::Future<bool> IsRuntimeActiveByPid(const pid_t &pid) const;

    litebus::Future<bool> CppAndPythonRuntimeLogProcess(const bool &isActive, const std::string &runtimeID,
                                                          const std::string &filePath, const time_t &nowTimeStamp);

    litebus::Future<bool> DsClientLogProcess(const bool &isActive, const pid_t &pid,
                                             const std::string &filePath, const time_t &nowTimeStamp);

    litebus::Future<bool> JavaRuntimeDirProcess(const bool &isActive, const std::string &runtimeID,
                                                  const std::string &filePath, const time_t &nowTimeStamp);

    litebus::Future<std::string> AcquireLogPrefix(const std::string &runtimeID);
    void ReleaseLogPrefix(const std::string &runtimeID);

    void NoReuseLogScanAndClean(const std::vector<std::string> &files);
    void ReuseLogScanAndClean(const std::vector<std::string> &files);
    litebus::Future<bool> RecycleReuseLog(const bool &isActive, const std::string &runtimeID, const std::string &file,
                                          const time_t &nowTimeStamp);
    litebus::Future<bool> NeedCleanFile(const std::string &runtimeID, const std::string &filePath,
                                        const time_t &nowTimeStamp);

protected:
    void Init() override;
    void Finalize() override;

    // One-shot logrotate rotation of active logs that reached maxSizeMb;
    // independent of logExpirationConfig_.enable.
    void RotateOversizeLogs(const std::vector<std::string> &files);

private:
    LogExpiratioinConfig logExpirationConfig_;
    RuntimeLogRotateConfig logRotateConfig_;
    litebus::Timer scanLogsTimer_;
    std::shared_ptr<ExpiredLogQueue> expiredLogQueue_;
    std::string runtimeLogsPath_;
    std::string runtimeStdLogDir_;
    litebus::AID runtimeManagerAID_;

    std::string GetJavaRuntimeIDFromLogDirName(const std::string &file, const std::string &filePath);

    std::string GetRuntimeIDFromLogFileName(const std::string &file, const std::string &filePath);

    pid_t GetDsClientPidFromLogFileName(const std::string &file, const std::string &filePath);

    litebus::Future<bool> CollectAddFilesFuture(const std::list<litebus::Future<bool>> &adds);

    // Writes the logrotate conf for the given logs, mode pinned to 0644.
    bool WriteRotateConf(const std::vector<std::string> &rotateTargets, const std::string &confPath);

    // Spawns one-shot "logrotate -s <state> <conf>" via litebus::Exec, waits up
    // to rotateTimeoutMs; on timeout stops the child SIGTERM-first and escalates
    // to SIGKILL only after a grace window. Returns the exit code, or -1 on
    // spawn error/timeout; captured stderr goes into stderrOut.
    static int RunLogrotate(const std::string &confPath, const std::string &statePath, int rotateTimeoutMs,
                            std::string &stderrOut);

    // for reuse logPrefix
    std::unordered_map<std::string, std::string> logPrefix2RuntimeID_;
    std::unordered_map<std::string, std::string> runtimeID2LogPrefix_;
    std::deque<std::string> logPrefixDequeue_;
    std::string actorPid_ = std::to_string(getpid());
    int logCount_ = 0;
    bool logReuse_ = false;
};

}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_ACTOR_H
