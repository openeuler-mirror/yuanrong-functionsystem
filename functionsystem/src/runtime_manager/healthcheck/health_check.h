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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_HEALTHCHECK_HEALTH_CHECK_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_HEALTHCHECK_HEALTH_CHECK_H

#include <actor/actor.hpp>
#include <exec/exec.hpp>

#include "healthcheck_actor.h"
#include "runtime_manager/config/flags.h"
#include "common/constants/actor_name.h"

namespace functionsystem::runtime_manager {

class HealthCheck {
public:
    explicit HealthCheck(const std::string &name = RUNTIME_MANAGER_HEALTH_CHECK_ACTOR_NAME);

    ~HealthCheck();

    void UpdateAgentInfo(const litebus::AID &to) const;

    void RegisterProcessExitCallback(const std::function<void(const pid_t)> &func) const;

    void RegisterHandleLogPrefixExit(const std::function<void(const std::string)> &func) const;

    /**
     * Start health check actor to reap child process
     * @param to where to inform reap child status
     * @param execPtr the information of process
     * @param instanceID the runtime instance ID
     * @param runtimeID the runtime ID
     */
    void AddRuntimeRecord(const litebus::AID &to, const pid_t &pid, const std::string &instanceID,
                          const std::string &runtimeID, const std::string &stdLogName) const;

    /**
     * Set flags to HealthCheckActor
     * @param flags
     * @return
     */
    void SetConfig(const Flags &flags) const;

    /**
     * Set max resend time HealthCheckActor
     * @param time resend frequency
     * @return
     */
    void SetMaxSendFrequency(const uint32_t frequency) const;

    /**
     * Stop health check for process by pid
     * @param pid process id
     * @return
     */
    litebus::Future<Status> StopHeathCheckByPID(const std::shared_ptr<litebus::Exec> &exec) const;

    litebus::Future<Status> GetRuntimeStatus(const std::string &runtimeID) const;

    void RemoveRuntimeStatusCache(const std::string &runtimeID) const;

    litebus::Future<messages::InstanceStatusInfo> QueryInstanceStatusInfo(const std::string &instanceID,
                                                                          const std::string &runtimeID);
    /**
     * In advance, notify HealthCheckActor the instance is killed by the OOM(RuntimeMemoryExceedLimit).
     */
    litebus::Future<Status> NotifyOomKillInstanceInAdvance(const std::string &requestID, const std::string &instanceID,
                                                           const std::string &runtimeID);

    litebus::Future<Status> DeleteOomNotifyData(const std::string &requestID) const;

private:
    std::shared_ptr<HealthCheckActor> actor_;
};
}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_HEALTHCHECK_HEALTH_CHECK_CLIENT_H
