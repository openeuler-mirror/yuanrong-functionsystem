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

#include "logmanager_actor.h"
#include "logs/logging.h"
#include "proto/pb/posix/message.pb.h"
#include "files.h"
#include "manager/runtime_manager.h"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "async/collect.hpp"

#include <ctime>
#include <cstring>
#include <regex>

namespace functionsystem::runtime_manager {

namespace {
const uint32_t MILLISECONDS_PRE_SECOND = 1000;
const int EXCEPTION_DIR_LEN = 9;
const std::string RUNTIME_UUID_PREFIX = "runtime-";
const std::string RUNTIME_LOG_REGEX_PATTERN =
    "(" + RUNTIME_UUID_PREFIX + "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})";
const std::regex JAVA_RUNTIME_LOG_REGEX_PATTERN_REGEX(RUNTIME_LOG_REGEX_PATTERN);
const std::string PYTHON_RUNTIME_LOG_REGEX_PATTERN =
    "^" + RUNTIME_LOG_REGEX_PATTERN + "(-\\d{14})?(\\.\\d*)?\\.log(\\.gz)?(\\.\\d+)?$";
const std::regex PYTHON_RUNTIME_LOG_REGEX_PATTERN_REGEX(PYTHON_RUNTIME_LOG_REGEX_PATTERN);
// The logs of cpp runtime exist in multiple formats, contains soft link
const std::string CPP_RUNTIME_LOG_REGEX_PATTERN = "^\\S+" + RUNTIME_LOG_REGEX_PATTERN + "\\S*(\\.log)?(\\.gz)?$";
const std::regex CPP_RUNTIME_LOG_REGEX_PATTERN_REGEX(CPP_RUNTIME_LOG_REGEX_PATTERN);
const std::string LIB_RUNTIME_LOG_REGEX_PATTERN =
    "^job-[0-9a-f]{8}-" + RUNTIME_LOG_REGEX_PATTERN + "(-\\d{14})?(\\.\\d*)?\\.log(\\.gz)?$";
const std::regex LIB_RUNTIME_LOG_REGEX_PATTERN_REGEX(LIB_RUNTIME_LOG_REGEX_PATTERN);
}  // namespace

bool ExpiredLogQueue::IsLogFileExist(const RuntimeLogFile &logFile)
{
    return filePathSet_.find(logFile.GetFilePath()) != filePathSet_.end();
}

void ExpiredLogQueue::AddLogFile(const RuntimeLogFile &logFile)
{
    if (IsLogFileExist(logFile)) {
        YRLOG_DEBUG("log file({}) already exists in ExpiredLogQueue", logFile.GetFilePath());
        return;
    }
    queue_.push(logFile);
    filePathSet_.insert(logFile.GetFilePath());
    YRLOG_DEBUG("AddLogFile: {}, modificationTime: {}, curtime: {}", logFile.GetFilePath(),
                logFile.GetModificationTime(), std::time(nullptr));
}

size_t ExpiredLogQueue::GetLogCount() const
{
    return queue_.size();
}

bool ExpiredLogQueue::DeleteOldestRuntimeLogFile()
{
    if (queue_.empty()) {
        YRLOG_DEBUG("queue_ is empty");
        return false;
    }
    RuntimeLogFile oldestLog = queue_.top();
    YRLOG_DEBUG("{} is to be deleted", oldestLog.GetFilePath());
    queue_.pop();
    filePathSet_.erase(oldestLog.GetFilePath());
    if (oldestLog.IsDir()) {
        if (litebus::os::Rmdir(oldestLog.GetFilePath(), true).IsSome()) {
            YRLOG_ERROR("failed to rm expired runtime log({})", oldestLog.GetFilePath());
            return false;
        }
    } else {
        if (litebus::os::Rm(oldestLog.GetFilePath()).IsSome()) {
            YRLOG_DEBUG("failed to rm expired runtime log({}), it has already been deleted", oldestLog.GetFilePath());
            return false;
        }
    }
    YRLOG_DEBUG("expired runtime log({}) deleted", oldestLog.GetFilePath());
    return true;
}

LogManagerActor::LogManagerActor(const std::string &name, const litebus::AID &runtimeManagerAID)
    : ActorBase(name), runtimeManagerAID_(runtimeManagerAID)
{
    expiredLogQueue_ = std::make_shared<ExpiredLogQueue>();
}

litebus::Future<bool> LogManagerActor::CppAndPythonRuntimeLogProcess(const bool &isActive, const std::string &runtimeID,
                                                                     const std::string &filePath,
                                                                     const time_t &nowTimeStamp)
{
    auto promise = std::make_shared<litebus::Promise<bool>>();
    if (isActive) {
        YRLOG_DEBUG("runtime({}) is active, not delete it's file", runtimeID);
        return true;
    }

    auto fileInfoOption = GetFileInfo(filePath);
    if (fileInfoOption.IsNone()) {
        YRLOG_WARN("Failed to get file info for {}", filePath);
        promise->SetFailed(litebus::Status::KERROR);
        return promise->GetFuture();
    }

    // The accuracy is maintained to the level of seconds.
    auto modificationTime = fileInfoOption.Get().st_mtime;
    // Check if the log file is expired based on its modification time.
    if (nowTimeStamp - modificationTime >= logExpirationConfig_.timeThreshold) {
        YRLOG_DEBUG("Log file {} is expired", filePath);
        expiredLogQueue_->AddLogFile(RuntimeLogFile(runtimeID, filePath, modificationTime, false));
    }
    promise->SetValue(true);
    return promise->GetFuture();
}

litebus::Future<bool> LogManagerActor::JavaRuntimeDirProcess(const bool &isActive, const std::string &runtimeID,
                                                             const std::string &filePath, const time_t &nowTimeStamp)
{
    auto promise = std::make_shared<litebus::Promise<bool>>();
    if (isActive) {
        YRLOG_DEBUG("java runtime({}) is active, not delete it's file", runtimeID);
        return true;
    }

    bool isSubFilesAllExpired = true;
    time_t dirModificationTime = 0;
    auto subFilesOption = litebus::os::Ls(filePath);
    if (subFilesOption.IsSome()) {
        auto subFiles = subFilesOption.Get();
        for (const auto &subFile : subFiles) {
            auto subFilePath = litebus::os::Join(filePath, subFile, '/');
            auto subFileInfoOption = GetFileInfo(subFilePath);
            if (subFileInfoOption.IsNone()) {
                YRLOG_WARN("Failed to get file info for {}", subFilePath);
                continue;
            }
            auto modificationTime = subFileInfoOption.Get().st_mtime;
            YRLOG_DEBUG("{} modificationTime: {}", subFilePath, modificationTime);
            if (modificationTime > dirModificationTime) {
                dirModificationTime = modificationTime;
            }

            // Check if the log file is expired based on its modification time.
            YRLOG_DEBUG("nowTimeStamp: {}, modificationTime: {}, sub: {}, timeThreshold: {}", nowTimeStamp,
                        modificationTime, nowTimeStamp - modificationTime, logExpirationConfig_.timeThreshold);
            if (nowTimeStamp - modificationTime >= logExpirationConfig_.timeThreshold) {
                YRLOG_DEBUG("Log file {} is expired", subFilePath);
                expiredLogQueue_->AddLogFile(RuntimeLogFile(runtimeID, subFilePath, modificationTime, false));
            } else {
                isSubFilesAllExpired &= false;
            }
        }
        // the directory is also count as one expired log file
        if (isSubFilesAllExpired) {
            expiredLogQueue_->AddLogFile(RuntimeLogFile(runtimeID, filePath, dirModificationTime, true));
        }
    }
    promise->SetValue(true);
    return promise->GetFuture();
}

void LogManagerActor::Init()
{
    YRLOG_INFO("Init LogManagerActor.");
}

void LogManagerActor::Finalize()
{}

void LogManagerActor::SetConfig(const Flags &flags)
{
    YRLOG_DEBUG("Init LogManagerActor config");
    runtimeLogsPath_ = flags.GetRuntimeLogPath();
    runtimeStdLogDir_ = flags.GetRuntimeStdLogDir();
    logExpirationConfig_ = {.enable = flags.GetLogExpirationEnable(),
        .cleanupInterval = flags.GetLogExpirationCleanupInterval(),
        .timeThreshold = flags.GetLogExpirationTimeThreshold(),
        .maxFileCount = flags.GetLogExpirationMaxFileCount()};
}

void LogManagerActor::StopScanLogs()
{
    if (!logExpirationConfig_.enable) {
        YRLOG_DEBUG("runtime expired log manage disabled");
        return;
    }
    (void)litebus::TimerTools::Cancel(scanLogsTimer_);
}

std::string LogManagerActor::GetJavaRuntimeIDFromLogDirName(const std::string &file, const std::string &filePath)
{
    std::string runtimeID;
    std::smatch matchResult;
    if (std::regex_match(file, matchResult, JAVA_RUNTIME_LOG_REGEX_PATTERN_REGEX)) {
        YRLOG_DEBUG("Processing java runtime log file {}", filePath);
        if (matchResult.size() > 1) {
            runtimeID = matchResult[1].str();
            YRLOG_DEBUG("Extracted java runtimeId: {}", runtimeID);
        }
    }
    return runtimeID;
}

std::string LogManagerActor::GetRuntimeIDFromLogFileName(const std::string &file, const std::string &filePath)
{
    std::string runtimeID;
    std::smatch matchResult;
    if (std::regex_match(file, matchResult, PYTHON_RUNTIME_LOG_REGEX_PATTERN_REGEX)) {
        YRLOG_DEBUG("Processing python runtime log file {}", filePath);
        if (matchResult.size() > 1) {
            runtimeID = matchResult[1].str();
            YRLOG_DEBUG("Extracted python runtimeId: {}", runtimeID);
        }
    } else if (std::regex_match(file, matchResult, LIB_RUNTIME_LOG_REGEX_PATTERN_REGEX)) {
        YRLOG_DEBUG("Processing lib runtime log file {}", filePath);
        if (matchResult.size() > 1) {
            runtimeID = matchResult[1].str();
            YRLOG_DEBUG("Extracted lib runtimeId: {}", runtimeID);
        }
    } else if (std::regex_match(file, matchResult, CPP_RUNTIME_LOG_REGEX_PATTERN_REGEX)) { // last match
        YRLOG_DEBUG("Processing cpp runtime log file {}", filePath);
        if (matchResult.size() > 1) {
            runtimeID = matchResult[1].str();
            YRLOG_DEBUG("Extracted cpp runtimeId: {}", runtimeID);
        }
    }
    return runtimeID;
}

litebus::Future<bool> LogManagerActor::IsRuntimeActive(const std::string &runtimeID) const
{
    return litebus::Async(runtimeManagerAID_, &RuntimeManager::IsRuntimeActive, runtimeID);
}

litebus::Future<bool> LogManagerActor::CollectAddFilesFuture(const std::list<litebus::Future<bool>> &adds)
{
    auto promise = std::make_shared<litebus::Promise<bool>>();
    (void)litebus::Collect(adds).OnComplete([promise](const litebus::Future<std::list<bool>> &future) {
        if (future.IsError()) {
            YRLOG_DEBUG("Collect future error");
            promise->SetValue(false);
            return;
        }
        bool isError = false;
        for (auto resp : future.Get()) {
            if (resp) {
                continue;
            }
            isError = true;
            YRLOG_WARN("error occurs of files in this scanning round");
        }
        YRLOG_DEBUG("done. result: {}", !isError);
        promise->SetValue(!isError);
    });
    return promise->GetFuture();
}

void LogManagerActor::ScanLogsRegularly()
{
    if (!logExpirationConfig_.enable) {
        YRLOG_DEBUG("runtime expired log manage disabled");
        return;
    }

    YRLOG_DEBUG("start ScanLogs");
    if (!litebus::os::ExistPath(runtimeLogsPath_)) {
        YRLOG_WARN("{} is not exist", runtimeLogsPath_);
        return;
    }

    auto filesOption = litebus::os::Ls(runtimeLogsPath_);
    if (filesOption.IsNone() || filesOption.Get().empty()) {
        YRLOG_WARN("no log file in {}", runtimeLogsPath_);
        return;
    }

    auto files = filesOption.Get();
    time_t nowTimeStamp = std::time(nullptr);
    std::list<litebus::Future<bool>> adds;
    for (const auto &file : files) {
        auto filePath = litebus::os::Join(runtimeLogsPath_, file, '/');
        bool isDir = !IsFile(filePath);
        YRLOG_DEBUG("Processing filePath: {}", filePath);
        if (isDir) {
            // Skip the exception and instance folders, which are for storing function exception
            // and user console output logs respectively.
            if (strncmp(file.c_str(), "exception", EXCEPTION_DIR_LEN) == 0 ||
                strncmp(file.c_str(), runtimeStdLogDir_.c_str(), runtimeStdLogDir_.size()) == 0) {
                continue;
            }

            // If it is a directory, check the latest modification time of the files in the directory.
            std::string runtimeID = GetJavaRuntimeIDFromLogDirName(file, filePath);
            if (runtimeID.empty()) {
                continue;
            }
            auto future = IsRuntimeActive(runtimeID).Then(
                litebus::Defer(GetAID(), &LogManagerActor::JavaRuntimeDirProcess, std::placeholders::_1, runtimeID,
                               filePath, nowTimeStamp));
            adds.emplace_back(future);
        } else {
            std::string runtimeID = GetRuntimeIDFromLogFileName(file, filePath);
            if (runtimeID.empty()) {
                continue;
            }
            auto future = IsRuntimeActive(runtimeID).Then(
                litebus::Defer(GetAID(), &LogManagerActor::CppAndPythonRuntimeLogProcess, std::placeholders::_1,
                               runtimeID, filePath, nowTimeStamp));
            adds.emplace_back(future);
        }
    }

    // CleanLogs after expired log files added to queue
    CollectAddFilesFuture(adds).OnComplete(litebus::Defer(GetAID(), &LogManagerActor::CleanLogs));
    scanLogsTimer_ = litebus::AsyncAfter(logExpirationConfig_.cleanupInterval * MILLISECONDS_PRE_SECOND, GetAID(),
                                         &LogManagerActor::ScanLogsRegularly);
}

void LogManagerActor::CleanLogs()
{
    YRLOG_DEBUG("start CleanLogs");
    auto logCount = expiredLogQueue_->GetLogCount();
    YRLOG_DEBUG("expiredLogQueue_ count: {}, maxFileCount:{}", logCount, logExpirationConfig_.maxFileCount);
    if (logCount <= (size_t)logExpirationConfig_.maxFileCount) {
        return;
    }
    size_t toDeleteCount = logCount - logExpirationConfig_.maxFileCount;
    for (size_t i = 0; i < toDeleteCount; ++i) {
        expiredLogQueue_->DeleteOldestRuntimeLogFile();
    }
}

}  // namespace functionsystem::runtime_manager