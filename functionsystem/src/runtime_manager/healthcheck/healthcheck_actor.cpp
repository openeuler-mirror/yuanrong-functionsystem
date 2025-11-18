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

#include "healthcheck_actor.h"

#include <async/asyncafter.hpp>
#include <async/defer.hpp>
#include <regex>

#include "common/logs/logging.h"
#include "common/utils/exec_utils.h"
#include "common/utils/generate_message.h"
#include "common/utils/proc_fs_tools.h"
#include "runtime_manager/utils/std_redirector.h"

namespace functionsystem::runtime_manager {
const uint32_t RETRY_CYCLE = 1000;
const std::vector<std::string> OOM_MSG = { "Memory cgroup out of memory: Kill process",
                                           "Memory cgroup out of memory: Killed process",
                                           "Killed process",
                                           "Out of memory: Kill process" };

void RecycleSubProcess(int /* sigNo */, siginfo_t *info, void * /* context */)
{
}

HealthCheckActor::HealthCheckActor(const std::string &name) : ActorBase(name)
{
}

void HealthCheckActor::Init()
{
    YRLOG_INFO("init HealthCheckActor {}", ActorBase::GetAID().Name());
    ActorBase::Receive("UpdateInstanceStatusResponse", &HealthCheckActor::CheckHealthResponse);
    pid2RuntimeIDMap_.clear();
    runtimeStatus_.clear();
    instanceIDMap_.clear();
    instanceID2PidMap_.clear();
    logMap_.clear();
    oomMap_.clear();

    litebus::Async(GetAID(), &HealthCheckActor::ReapProcess);
}

void HealthCheckActor::Finalize()
{
    YRLOG_INFO("finalize HealthCheckActor {}", ActorBase::GetAID().Name());
}

void HealthCheckActor::UpdateAgentInfo(const litebus::AID &to)
{
    HealthCheckActor::functionAgentAID_ = to;
}

void HealthCheckActor::RegisterProcessExitCallback(const std::function<void(const pid_t)> &func)
{
    HealthCheckActor::processExitCallback_ = func;
}

void HealthCheckActor::RegisterHandleLogPrefixExit(const std::function<void(const std::string)> &func)
{
    HealthCheckActor::logPrefixExitCallback_ = func;
}

void HealthCheckActor::ReapProcess()
{
    YRLOG_INFO("ReapProcess start");
    struct sigaction sa;
    sa.sa_sigaction = &RecycleSubProcess;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_SIGINFO;
    (void)sigaction(SIGCHLD, &sa, nullptr);

    (void)litebus::AsyncAfter(RETRY_CYCLE, GetAID(), &HealthCheckActor::WaitProcessCyclical);
}

void HealthCheckActor::AddRuntimeRecord(const litebus::AID &to, const pid_t &pid, const std::string &instanceID,
                                        const std::string &runtimeID, const std::string &stdLogName)
{
    pid2RuntimeIDMap_[pid] = runtimeID;
    runtimeStatus_[runtimeID] = std::make_shared<litebus::Promise<Status>>();
    instanceIDMap_[pid] = instanceID;
    instanceID2PidMap_[instanceID] = pid;
    logMap_[runtimeID] = stdLogName;
    HealthCheckActor::functionAgentAID_ = to;
}

void HealthCheckActor::CheckHealthResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    messages::UpdateInstanceStatusResponse res;
    if (msg.empty() || !res.ParseFromString(msg)) {
        YRLOG_ERROR("message from {} is invalid.", std::string(from));
        return;
    }
    YRLOG_DEBUG("{}|received UpdateInstanceStatusResponse from {}", res.requestid(), std::string(from));
    if (timers_.find(res.requestid()) != timers_.end()) {
        (void)litebus::TimerTools::Cancel(timers_[res.requestid()]);
        (void)timers_.erase(res.requestid());
    }
    (void)sendCounter_.erase(res.requestid());
    if (oomKillEnable_) {
        if (oomNotifyMap_.find(res.requestid()) != oomNotifyMap_.end()) {
            oomNotifyMap_[res.requestid()]->SetValue(Status{ StatusCode(res.status()), res.message() });
            YRLOG_DEBUG("{}|start to oom kill instance, after get response status({}), message({})", res.requestid(),
                        res.status(), res.message());
        }
    }
}

void HealthCheckActor::SetConfig(const Flags &flags)
{
    runtimeLogsPath_ = flags.GetRuntimeLogPath();
    runtimeStdLogDir_ = flags.GetRuntimeStdLogDir();
    oomKillEnable_ = flags.GetOomKillEnable();
}

void HealthCheckActor::SetMaxSendFrequency(const uint32_t frequency)
{
    sendFrequency_ = frequency;
}

litebus::Future<messages::InstanceStatusInfo> HealthCheckActor::QueryInstanceStatusInfo(const std::string &instanceID,
                                                                                        const std::string &runtimeID)
{
    YRLOG_INFO("query instanceID({}) runtimeID({}) status.", instanceID, runtimeID);
    return GetRuntimeException(runtimeID, instanceID, -1).Then([instanceID](const ExceptionInfo &exception) {
        messages::InstanceStatusInfo req;
        req.set_instanceid(instanceID);
        req.set_status(-1);
        req.set_instancemsg(exception.message);
        req.set_type(exception.type);
        return req;
    });
}

litebus::Future<Status> HealthCheckActor::SendInstanceStatus(const std::string &instanceID,
                                                             const std::string &runtimeID, const int status,
                                                             const std::string &requestID)
{
    // status: instance exit situation:
    // 1) user kill instance by function system. ps. doesn't need to send instance status
    // 2) value: 0. instance return by itself.
    // 3) instance happens exceptions.
    // 4) value: -1. runtime_manager kill instance for RuntimeMemoryExceedLimit(OOM). need to send instance status
    auto req = GenUpdateInstanceStatusRequest(instanceID, status, requestID);
    auto info = req->mutable_instancestatusinfo();
    if (status == 0) {
        // runtime return by itself
        auto exitMsg = "runtime had been returned";
        info->set_instancemsg(exitMsg);
        info->set_type(static_cast<int32_t>(EXIT_TYPE::RETURN));
        litebus::Async(GetAID(), &HealthCheckActor::StartUpdateInstanceStatus, req, functionAgentAID_, runtimeID,
                       status);
        return Status{ StatusCode::SUCCESS, exitMsg };
    }
    if (status == -1) {
        // marked RuntimeMemoryExceedLimit(OOM) kill
        auto exitMsg = "runtime memory exceed limit";
        info->set_instancemsg(exitMsg);
        info->set_type(static_cast<int32_t>(EXIT_TYPE::RUNTIME_MEMORY_EXCEED_LIMIT));
        litebus::Async(GetAID(), &HealthCheckActor::StartUpdateInstanceStatus, req, functionAgentAID_, runtimeID,
                       status);
        return Status{ StatusCode::FAILED, exitMsg };
    }
    // runtime exit with exception
    return GetRuntimeException(runtimeID, instanceID, status)
        .Then([req, functionAgentAID(functionAgentAID_), runtimeID, status,
               aid(GetAID())](const ExceptionInfo &exception) -> litebus::Future<Status> {
            auto info = req->mutable_instancestatusinfo();
            info->set_instancemsg(exception.message);
            info->set_type(exception.type);
            litebus::Async(aid, &HealthCheckActor::StartUpdateInstanceStatus, req, functionAgentAID, runtimeID, status);
            return Status{ StatusCode::FAILED, exception.message };
        });
}

void HealthCheckActor::StartUpdateInstanceStatus(const std::shared_ptr<messages::UpdateInstanceStatusRequest> &req,
                                                 const litebus::AID &to, const std::string &runtimeID, const int status)
{
    const auto &requestID = req->requestid();
    const auto &instanceID = req->instancestatusinfo().instanceid();
    YRLOG_INFO("{}|update instanceID({}) runtimeID({}) status({}) to {}.", requestID, instanceID, runtimeID, status,
               std::string(to));
    (void)Send(to, "UpdateInstanceStatus", req->SerializeAsString());
    sendCounter_[requestID]++;
    timers_[requestID] =
        litebus::AsyncAfter(RETRY_CYCLE, GetAID(), &HealthCheckActor::UpdateInstanceStatus, req, to, runtimeID, status);
}

void HealthCheckActor::UpdateInstanceStatus(const std::shared_ptr<messages::UpdateInstanceStatusRequest> &req,
                                            const litebus::AID &to, const std::string &runtimeID, const int status)
{
    const auto &requestID = req->requestid();
    if (timers_.find(requestID) == timers_.end() || sendCounter_.find(requestID) == sendCounter_.end() ||
        sendCounter_[requestID] == sendFrequency_) {
        (void)timers_.erase(requestID);
        (void)sendCounter_.erase(requestID);
        return;
    }
    StartUpdateInstanceStatus(req, to, runtimeID, status);
}

Status HealthCheckActor::StopReapProcessByPID(const std::shared_ptr<litebus::Exec> &exec)
{
    if (exec == nullptr) {
        return Status::OK();
    }

    auto pid = exec->GetPid();
    if (pid2RuntimeIDMap_.find(pid) != pid2RuntimeIDMap_.end()) {
        auto runtimeID = pid2RuntimeIDMap_[pid];
        (void)pid2RuntimeIDMap_.erase(pid);
        if (runtimeStatus_.find(runtimeID) != runtimeStatus_.end()) {
            YRLOG_DEBUG("runtimeStatus_[{}] SetValue", runtimeID);
            runtimeStatus_[runtimeID]->SetValue(
                Status{ StatusCode::SUCCESS, "runtime stopped by inner-system" });  // runtime exited
        }
        auto requestID(litebus::os::Join("update-instance-status-request", runtimeID, '-'));
        if (auto timer(timers_.find(requestID)); timer != timers_.end()) {
            (void)litebus::TimerTools::Cancel(timer->second);
            (void)sendCounter_.erase(requestID);
            (void)timers_.erase(requestID);
        }
        if (logMap_.find(runtimeID) != logMap_.end()) {
            (void)logMap_.erase(runtimeID);
        }
    }

    if (auto iter(instanceIDMap_.find(pid)); iter != instanceIDMap_.end()) {
        auto instanceID = instanceIDMap_[pid];
        (void)instanceIDMap_.erase(pid);
        (void)instanceID2PidMap_.erase(instanceID);
    }

    return Status::OK();
}

litebus::Future<Status> HealthCheckActor::NotifyOomKillInstanceInAdvance(const std::string &requestID,
                                                                         const std::string &instanceID,
                                                                         const std::string &runtimeID)
{
    if (auto iter(instanceID2PidMap_.find(instanceID)); iter != instanceID2PidMap_.end()) {
        pid_t pid = instanceID2PidMap_[instanceID];
        oomMap_[pid] = std::make_pair(instanceID, runtimeID);
        (void)SendInstanceStatus(instanceID, runtimeID, -1, requestID);
        YRLOG_DEBUG("{}|{}|Notify OOM Kill instance({}) in advance", requestID, runtimeID, instanceID);
        auto promise = std::make_shared<litebus::Promise<Status>>();
        oomNotifyMap_[requestID] = promise;
        return promise->GetFuture();
    }
    YRLOG_ERROR("{}|{}|failed to find instanceID({}) to pid in map for recording OOM kill in advance", requestID,
                runtimeID, instanceID);
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> HealthCheckActor::DeleteOomNotifyData(const std::string &requestID)
{
    (void)oomNotifyMap_.erase(requestID);
    return Status::OK();
}

void HealthCheckActor::WaitProcessCyclical()
{
    pid_t pid;
    int status = 0; // In normal cases, the value of status is between the values of [0,255].
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        YRLOG_INFO("RecycleSubProcess pid({}), status({}), exitState({}), exitCode({})", pid, status, WIFEXITED(status),
                   WEXITSTATUS(status));
        if (pid2RuntimeIDMap_.find(pid) != pid2RuntimeIDMap_.end() &&
            instanceIDMap_.find(pid) != instanceIDMap_.end()) {
            auto runtimeID = pid2RuntimeIDMap_[pid];
            auto instanceID = instanceIDMap_[pid];
            auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');
            auto exitMsgFuture = SendInstanceStatus(instanceID, runtimeID, status, requestID);
            if (runtimeStatus_.find(runtimeID) != runtimeStatus_.end()) {
                runtimeStatus_[runtimeID]->Associate(exitMsgFuture);
            }
            if (HealthCheckActor::logPrefixExitCallback_ != nullptr) {
                HealthCheckActor::logPrefixExitCallback_(runtimeID);
            }
        } else {
            // Check if the pid corresponds to an RuntimeMemoryExceedLimit(OOM) situation
            if (auto iter(oomMap_.find(pid)); iter != oomMap_.end()) {
                oomMap_.erase(pid); // end of lifecycle
            }

            if (HealthCheckActor::processExitCallback_ != nullptr) {
                HealthCheckActor::processExitCallback_(pid);
            }
        }
    }

    (void)litebus::AsyncAfter(RETRY_CYCLE, GetAID(), &HealthCheckActor::WaitProcessCyclical);
}

litebus::Future<ExceptionInfo> HealthCheckActor::GetRuntimeException(const std::string &runtimeID,
                                                                     const std::string &instanceID, const int status)
{
    // Get Exception Log
    auto path = runtimeLogsPath_ + "/exception/BackTrace_" + runtimeID + ".log";
    if (auto info(GetLogInfoByPath(runtimeID, path)); info.IsSome()) {
        return ExceptionInfo{ info.Get(), static_cast<int32_t>(EXIT_TYPE::EXCEPTION_INFO) };
    }

    // Get OOM Log
    auto bareMentalEnv(litebus::os::GetEnv("YR_BARE_MENTAL"));
    return GetOOMInfo(bareMentalEnv.IsSome() && !bareMentalEnv.Get().empty())
        .Then(litebus::Defer(GetAID(), &HealthCheckActor::GetOOMExceptionInfo, std::placeholders::_1, runtimeID,
                             instanceID, status));
}

litebus::Future<ExceptionInfo> HealthCheckActor::GetOOMExceptionInfo(const litebus::Option<std::string> &info,
                                                                     const std::string &runtimeID,
                                                                     const std::string &instanceID, const int &status)
{
    if (info.IsSome()) {
        return ExceptionInfo{ "runtime(" + runtimeID + ") process may be killed for some reason",
                              static_cast<int32_t>(EXIT_TYPE::OOM_INFO) };
    }
    return litebus::Async(GetAID(), &HealthCheckActor::GetStdLog, runtimeID, instanceID, status);
}

litebus::Future<ExceptionInfo> HealthCheckActor::GetStdLog(const std::string &runtimeID, const std::string &instanceID,
                                                           const int status)
{
    // Get Standard Log
    if (logMap_.find(runtimeID) != logMap_.end()) {
        auto logName = logMap_[runtimeID];
        auto logFile = litebus::os::Join(litebus::os::Join(runtimeLogsPath_, runtimeStdLogDir_), logName + STD_POSTFIX);
        YRLOG_INFO("try get std log of runtime {} from path: {}.", runtimeID, logFile);
        auto msg = StdRedirector::GetStdLog(logFile, runtimeID, ERROR_LEVEL);
        if (!msg.empty()) {
            msg = "instance(" + instanceID + ") runtime(" + runtimeID + ") exit code(" + std::to_string(status) +
            ") with exitState(" + std::to_string(WIFEXITED(status)) + ") exitStatus(" +
            std::to_string(WEXITSTATUS(status)) + ")\n" + msg;
            return ExceptionInfo{ msg, static_cast<int32_t>(EXIT_TYPE::STANDARD_INFO) };
        }
    }
    return ExceptionInfo{ "an unknown error caused the instance exited. exit code:" + std::to_string(status) +
                          " instance:" + instanceID + " runtime:" + runtimeID +
                          " exitState:" + std::to_string(WIFEXITED(status)) + " exitStatus:" +
                          std::to_string(WEXITSTATUS(status)),
                          static_cast<int32_t>(EXIT_TYPE::UNKNOWN_ERROR) };
}

litebus::Option<std::string> HealthCheckActor::GetLogInfoByPath(const std::string &runtimeID, const std::string &path)
{
    auto content = litebus::os::Read(path);
    if (content.IsNone()) {
        YRLOG_ERROR("path: {} is not existed.", path);
        return litebus::None();
    }
    return content.Get();
}

litebus::Future<litebus::Option<std::string>> HealthCheckActor::GetOOMInfo(const bool isBareMental)
{
    std::string command = R"(/bin/bash -c "/usr/bin/dmesg -T | tail -100")";
    if (!isBareMental) {
        // in container
        command = "sudo " + command;
    }
    return AsyncExecuteCommand(command).Then(
        [isBareMental](const CommandExecResult &execResult) -> litebus::Option<std::string> {
            auto result = execResult;
            if (!result.error.empty()) {
                YRLOG_ERROR("failed to get 'dmesg' result, error: {}", result.error);
                return litebus::None();
            }
            if (!isBareMental) {
                std::string sign("killed as a result of limit of /kubepods");
                auto idx(result.output.find(sign));
                if (idx == std::string::npos) {
                    return litebus::None();
                }
                result.output = result.output.substr(idx + sign.length());
            }
            for (const std::string exp : OOM_MSG) {
                if (std::regex_search(result.output, std::regex(exp))) {
                    for (const auto &str : litebus::strings::Split(result.output, "\n")) {
                        YRLOG_INFO("{}", str);
                    }
                    return result.output;
                }
            }
            return litebus::None();
        });
}

litebus::Future<Status> HealthCheckActor::GetRuntimeStatus(const std::string &runtimeID)
{
    if (runtimeStatus_.find(runtimeID) != runtimeStatus_.end()) {
        return runtimeStatus_[runtimeID]->GetFuture();
    }
    return Status::OK();
}

void HealthCheckActor::RemoveRuntimeStatusCache(const std::string &runtimeID)
{
    (void)runtimeStatus_.erase(runtimeID);
}

}  // namespace functionsystem::runtime_manager