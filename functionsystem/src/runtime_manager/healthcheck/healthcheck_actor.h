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

#ifndef RUNTIME_MANAGER_HEALTHCHECK_HEALTHCHECK_ACTOR_HEALTHCHECK_ACTOR_H
#define RUNTIME_MANAGER_HEALTHCHECK_HEALTHCHECK_ACTOR_HEALTHCHECK_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <exec/exec.hpp>

#include "common/constants/constants.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/proc_fs_tools.h"
#include "runtime_manager/config/flags.h"

namespace functionsystem::runtime_manager {

struct ExceptionInfo {
    std::string message;
    int32_t type;
};

const uint32_t MAX_RETRY_TIMES = 10;
class HealthCheckActor : public litebus::ActorBase {
public:
    explicit HealthCheckActor(const std::string &name);

    ~HealthCheckActor() override = default;

    void AddRuntimeRecord(const litebus::AID &to, const pid_t &pid, const std::string &instanceID,
                          const std::string &runtimeID, const std::string &stdLogName);

    void CheckHealthResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    void SetConfig(const Flags &flags);

    void SetMaxSendFrequency(const uint32_t frequency);

    void UpdateAgentInfo(const litebus::AID &to);

    void RegisterProcessExitCallback(const std::function<void(const pid_t)> &func);

    void RegisterHandleLogPrefixExit(const std::function<void(const std::string)> &func);

    void ReapProcess();

    litebus::Future<Status> SendInstanceStatus(const std::string &instanceID, const std::string &runtimeID,
                                                    const int status, const std::string &requestID);

    Status StopReapProcessByPID(const std::shared_ptr<litebus::Exec> &exec);

    litebus::Future<Status> GetRuntimeStatus(const std::string &runtimeID);

    void RemoveRuntimeStatusCache(const std::string &runtimeID);

    litebus::Future<messages::InstanceStatusInfo> QueryInstanceStatusInfo(const std::string &instanceID,
                                                                          const std::string &runtimeID);

    litebus::Future<Status> NotifyOomKillInstanceInAdvance(const std::string &requestID, const std::string &instanceID,
                                                           const std::string &runtimeID);

    litebus::Future<Status> DeleteOomNotifyData(const std::string &requestID);

    litebus::AID functionAgentAID_;
    std::function<void(const pid_t)> processExitCallback_;
    std::function<void(const std::string)> logPrefixExitCallback_ = nullptr;
    std::unordered_map<pid_t, std::string> pid2RuntimeIDMap_;
    std::unordered_map<pid_t, std::string> instanceIDMap_;
    std::unordered_map<std::string, std::string> logMap_;
    std::unordered_map<std::string, pid_t> instanceID2PidMap_;

    void WaitProcessCyclical();

protected:
    void Init() override;
    void Finalize() override;

private:
    litebus::Future<ExceptionInfo> GetRuntimeException(const std::string &runtimeID, const std::string &instanceID,
                                                       const int status);
    litebus::Future<ExceptionInfo> GetOOMExceptionInfo(const litebus::Option<std::string> &info,
                                                       const std::string &runtimeID, const std::string &instanceID,
                                                       const int &status);
    void StartUpdateInstanceStatus(const std::shared_ptr<messages::UpdateInstanceStatusRequest> &req,
                                   const litebus::AID &to, const std::string &runtimeID, const int status);
    void UpdateInstanceStatus(const std::shared_ptr<messages::UpdateInstanceStatusRequest> &req, const litebus::AID &to,
                              const std::string &runtimeID, const int status);
    litebus::Option<std::string> GetLogInfoByPath(const std::string &runtimeID, const std::string &path);
    litebus::Future<litebus::Option<std::string>> GetOOMInfo(const bool isBareMental);
    litebus::Future<ExceptionInfo> GetStdLog(const std::string &runtimeID, const std::string &instanceID,
                                             const int status);
    std::unordered_map<std::string, litebus::Timer> timers_;
    std::unordered_map<std::string, uint32_t> sendCounter_;
    uint32_t sendFrequency_ = MAX_RETRY_TIMES;
    std::string runtimeLogsPath_;
    std::string runtimeStdLogDir_;

    bool oomKillEnable_;
    // value: <instanceID, runtimeID>
    std::unordered_map<pid_t, std::pair<std::string, std::string>> oomMap_;
    // key: requestID
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> oomNotifyMap_;
    // key: runtimeID
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> runtimeStatus_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_HEALTHCHECK_HEALTHCHECK_ACTOR_HEALTHCHECK_ACTOR_H
