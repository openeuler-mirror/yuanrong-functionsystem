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
#include "std_redirector.h"
#include <cstdlib>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "runtime_manager/utils/utils.h"
#include "utils/os_utils.hpp"
#include "common/utils/files.h"
#include "logs/sdk/log_param_parser.h"

namespace functionsystem {

std::string GetTimeOfNow()
{
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    struct tm buf;
    (void)localtime_r(&t, &buf);
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

Status StdRedirector::Start()
{
    YRLOG_INFO("user log export mode: {}", param_.exportMode);
    if (!litebus::os::ExistPath(path_)) {
        YRLOG_WARN("std log path {} not found, try to make dir", path_);
        if (!litebus::os::Mkdir(path_).IsNone()) {
            YRLOG_WARN("failed to make dir {}, msg: {}", path_, litebus::os::Strerror(errno));
            logFileNotExist_ = true;
            return Status(StatusCode::LOG_CONFIG_ERROR);
        }
    }
    char realPath[PATH_MAX] = { 0 };
    if (realpath(path_.c_str(), realPath) == nullptr) {
        YRLOG_WARN("real path std log file {} failed, errno: {}, {}", path_, errno, litebus::os::Strerror(errno));
        logFileNotExist_ = true;
        return Status(StatusCode::LOG_CONFIG_ERROR);
    }
    auto logFile = litebus::os::Join(std::string(realPath), logName_);
    if (!litebus::os::ExistPath(logFile) && TouchFile(logFile) != 0) {
        YRLOG_WARN("create std log file {} failed.", path_);
        logFileNotExist_ = true;
        return Status(StatusCode::LOG_CONFIG_ERROR);
    }

    // set user func std logger
    size_t pos = logName_.rfind(param_.logFileExtensions);
    if (pos != std::string::npos && pos == logName_.length() - std::string(param_.logFileExtensions).size()) {
        logNameForLogsSdk_ = logName_.substr(0, pos);  // compatible with previous configurations
    } else {
        logNameForLogsSdk_ = logName_;
    }
    namespace LogsSdk = observability::sdk::logs;
    namespace LogsApi = observability::api::logs;

    LogsApi::LogParam loggerParam;
    loggerParam.loggerName = logNameForLogsSdk_;
    loggerParam.nodeName = "nodeID";
    loggerParam.modelName = "runtime-manager";
    loggerParam.logFileWithTime = false;
    loggerParam.fileNamePattern = logNameForLogsSdk_;  // modify to real log file name
    loggerParam.logDir = std::string(realPath);        // modify to real logDir
    const std::string defaultLogLevel = "DEBUG";
    loggerParam.logLevel = defaultLogLevel;
    loggerParam.pattern = "%v";  // modify to origin log pattern
    loggerParam.compressEnable = true;
    loggerParam.maxFiles = param_.stdRollingMaxFiles;
    loggerParam.maxSize = param_.stdRollingMaxFileSize;  // MB
    loggerParam.alsoLog2Std = false; // not print to std output
    loggerParam.logFileExtensions = param_.logFileExtensions;
    YRLOG_DEBUG("loggerParam.maxFiles: {}, loggerParam.maxSize: {} MB", loggerParam.maxFiles, loggerParam.maxSize);

    lp_ = LogsApi::Provider::GetLoggerProvider();
    userStdLogger_ = lp_->CreateYrLogger(loggerParam);
    YRLOG_DEBUG("create user func std logger: {}", loggerParam.loggerName);
    auto fileName = ::observability::sdk::logs::GetLogFile(loggerParam);
    logFileNotExist_ = false;
    YRLOG_DEBUG("user func std log file path: {}", fileName);

    logManager_ = std::make_shared<LogsSdk::LogManager>(loggerParam);
    logManager_->StartRollingCompress(LogsSdk::LogRollingCompress);

    SetTimer(litebus::AsyncAfter(param_.flushDuration, GetAID(), &StdRedirector::FlushLogContentRegularly));
    return Status::OK();
}

void StdRedirector::FlushToDisk(const RuntimeStandardLog &log)
{
    // replaced with userStdLogger_
    userStdLogger_->info("{}|{}|{}|{}|{}", log.time, log.instanceID, log.runtimeID, log.level, log.message);
}

void StdRedirector::ExportLog()
{
    if (readyToFlushLogs_.length <= 0) {
        YRLOG_DEBUG("log is empty.");
        return;
    }

    if (param_.exportMode == functionsystem::runtime_manager::STD_EXPORTER) {
        FlushToStd();
    } else {
        FlushToDiskDirectly();
    }

    readyToFlushLogs_.message.str("");
    readyToFlushLogs_.message.clear();
    readyToFlushLogs_.length = 0;
}

void StdRedirector::FlushToDiskDirectly()
{
    userStdLogger_->info("{}", readyToFlushLogs_.message.str());
    userStdLogger_->flush();
}

void StdRedirector::FlushToStd()
{
    auto str = readyToFlushLogs_.message.str();
    std::cout << str << std::endl;
}

StdRedirector::~StdRedirector()
{
}

void StdRedirector::Finalize()
{
    try {
        StopTimer();
        MoveLogsToReady();
        ExportLog();
        if (!logFileNotExist_) {
            ASSERT_IF_NULL(logManager_);
            logManager_->StopRollingCompress();
            ASSERT_IF_NULL(lp_);
            lp_->DropYrLogger(logNameForLogsSdk_);
        }
        YRLOG_DEBUG("drop user func std logger: {}", logNameForLogsSdk_);
    } catch (std::ios_base::failure &e) {
        std::cerr << "close fileWriteStream failed. error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "close fileWriteStream failed." << std::endl;
    }
    for (auto iter : fd2Readers_) {
        litebus::Terminate(iter.second);
        litebus::Await(iter.second);
    }
    fd2Readers_.clear();
}

litebus::AID StdRedirector::GetPipeAsyncReader(int fd)
{
    if (fd2Readers_.count(fd) > 0) {
        return fd2Readers_[fd];
    }
    const std::string strPipeReadAsync = "AID" + std::to_string(fd);
    const litebus::AID pipeAsyncReader = litebus::Spawn(std::make_shared<litebus::os::PipeReadActor>(strPipeReadAsync));
    fd2Readers_[fd] = pipeAsyncReader;
    return pipeAsyncReader;
}

void StdRedirector::TerminatePipeReader(int fd)
{
    auto it = fd2Readers_.find(fd);
    if (it == fd2Readers_.end()) {
        return;
    }

    litebus::Terminate(it->second);
    litebus::Await(it->second);
    fd2Readers_.erase(it);
}

void StdRedirector::ReadStdLogRealTime(int fd, int event)
{
    bool isValidEvent = (!(event & EPOLLERR) && !(event & EPOLLHUP));
    if (!isValidEvent) {
        if (event & EPOLLERR) {
            YRLOG_ERROR("fd({}) I/O error occurred (EPOLLERR)", fd);
        } else if (event & EPOLLHUP) {
            YRLOG_WARN("fd({}) connection closed by peer (EPOLLHUP)", fd);
        }
        MoveLogsToReady();
        litebus::Async(GetAID(), &StdRedirector::ExportLog);
    }
    if (event & EPOLLIN) {
        const auto reader = GetPipeAsyncReader(fd);

        auto dataCallback = [aid(GetAID())](const std::string &content) {
            litebus::Async(aid, &StdRedirector::SetStdLogRawContent, content);
        };
        litebus::Async(reader, &litebus::os::PipeReadActor::ImmediatePipeReadOperation, fd, dataCallback);
    }
}

void StdRedirector::StartRuntimeStdRedirection(const std::string &runtimeID, const std::string &instanceID,
                                               const litebus::Option<int> stdOut, const litebus::Option<int> stdErr)
{
    if (logFileNotExist_) {
        return;
    }

    if (stdOut.IsSome()) {
        (void)litebus::os::ReadPipeAsyncRealTime(stdOut.Get(),
                                                 [aid(GetAID()), runtimeID, instanceID](const std::string &content) {
                                                     litebus::Async(aid, &StdRedirector::SetStdLogContent, content,
                                                                    runtimeID, instanceID, INFO_LEVEL);
                                                 })
            .OnComplete(litebus::Defer(GetAID(), &StdRedirector::MoveLogsToReady))
            .OnComplete(litebus::Defer(GetAID(), &StdRedirector::ExportLog));
    }
    if (stdErr.IsSome()) {
        (void)litebus::os::ReadPipeAsyncRealTime(stdErr.Get(),
                                                 [aid(GetAID()), runtimeID, instanceID](const std::string &content) {
                                                     litebus::Async(aid, &StdRedirector::SetStdLogContent, content,
                                                                    runtimeID, instanceID, ERROR_LEVEL);
                                                 })
            .OnComplete(litebus::Defer(GetAID(), &StdRedirector::MoveLogsToReady))
            .OnComplete(litebus::Defer(GetAID(), &StdRedirector::ExportLog));
    }
}

std::string StdRedirector::GetStdLog(const std::string &logFile, const std::string &runtimeID, const std::string &level,
                                     int32_t targetLineCnt, int32_t readLineCnt)
{
    char realPath[PATH_MAX] = { 0 };
    if (realpath(logFile.c_str(), realPath) == nullptr) {
        YRLOG_WARN("real path logFile {} failed, errno: {}, {}", logFile, errno, litebus::os::Strerror(errno));
        return "";
    }
    std::ifstream fin(realPath, std::ios::ate);
    if (!fin.is_open()) {
        YRLOG_WARN("not found std log file {}", logFile);
        return std::string();
    }
    std::string line;
    char ch;
    int errorLine = 0;
    int readLine = 0;
    std::string errMsg;
    while (fin.seekg(-1, std::ios::cur), fin.get(ch) && readLine < readLineCnt && errorLine < targetLineCnt) {
        if (fin.eof()) {
            break;
        }

        fin.seekg(-1, std::ios::cur);
        if (ch != fin.widen('\n')) {
            line = ch + line;
            continue;
        }

        if (line.find(runtimeID) == std::string::npos) {
            line = "";
            readLine++;
            continue;
        }

        if (line.find(level) == std::string::npos) {
            line = "";
            readLine++;
            continue;
        }
        line += '\n';
        errorLine++;
        readLine++;
        errMsg = line + errMsg;
        line = "";
    }

    if (errorLine < targetLineCnt && line.find(runtimeID) != std::string::npos &&
        line.find(level) != std::string::npos) {
        line += '\n';
        errMsg = line + errMsg;
    }
    fin.clear();
    fin.seekg(0);
    fin.close();
    YRLOG_INFO("got {} line of runtime {} standard {} output: {} in {} lines", errorLine, runtimeID, level, errMsg,
               readLineCnt);
    return errMsg;
}

void StdRedirector::SetStdLogContent(const std::string &content, const std::string &runtimeID,
                                     const std::string &instanceID, const std::string &level)
{
    auto now = GetTimeOfNow();
    for (auto raw : runtime_manager::Utils::SplitByFunc(
             content, [](const char &ch) -> bool { return ch == '\n' || ch == '\r'; })) {
        if (raw.empty()) {
            continue;
        }
        logs_.length += static_cast<int32_t>(raw.length());
        logs_.message << now << "|" << instanceID << "|" << runtimeID << "|" << level << "|" << raw << std::endl;
    }

    if (logs_.length < param_.maxLogLength) {
        YRLOG_DEBUG("log length is smaller than {}byte.", param_.maxLogLength);
        return;
    }

    MoveLogsToReady();
    YRLOG_DEBUG("ready to flush log when log larger then {} byte.", param_.maxLogLength);
    litebus::Async(GetAID(), &StdRedirector::ExportLog);
}

void StdRedirector::SetStdLogRawContent(const std::string &content)
{
    logs_.length += static_cast<int32_t>(content.length());
    logs_.message << content;

    if (logs_.length < param_.maxLogLength) {
        return;
    }

    MoveLogsToReady();
    YRLOG_DEBUG("ready to flush log to disk when log larger then {} byte.", param_.maxLogLength);
    litebus::Async(GetAID(), &StdRedirector::ExportLog);
}

void StdRedirector::FlushLogContentRegularly()
{
    YRLOG_DEBUG("ready to flush log regularly.");
    MoveLogsToReady();
    SetTimer(litebus::AsyncAfter(param_.flushDuration, GetAID(), &StdRedirector::FlushLogContentRegularly));
    litebus::Async(GetAID(), &StdRedirector::ExportLog);
}

}  // namespace functionsystem
