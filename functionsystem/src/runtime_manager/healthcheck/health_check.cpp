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

#include "health_check.h"

#include "async/async.hpp"
#include "async/uuid_generator.hpp"

namespace functionsystem::runtime_manager {

HealthCheck::HealthCheck(const std::string &name)
{
    actor_ = std::make_shared<HealthCheckActor>(name);
    litebus::Spawn(actor_);
}

HealthCheck::~HealthCheck()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

void HealthCheck::UpdateAgentInfo(const litebus::AID &to) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::UpdateAgentInfo, to);
}

void HealthCheck::RegisterProcessExitCallback(const std::function<void(const pid_t)> &func) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::RegisterProcessExitCallback, func);
}

void HealthCheck::RegisterHandleLogPrefixExit(const std::function<void(const std::string)> &func)  const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::RegisterHandleLogPrefixExit, func);
}

void HealthCheck::AddRuntimeRecord(const litebus::AID &to, const pid_t &pid, const std::string &instanceID,
                                   const std::string &runtimeID, const std::string &stdLogName) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::AddRuntimeRecord, to, pid, instanceID, runtimeID, stdLogName);
}

void HealthCheck::SetConfig(const Flags &flags) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::SetConfig, flags);
}

void HealthCheck::SetMaxSendFrequency(const uint32_t frequency) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::SetMaxSendFrequency, frequency);
}

litebus::Future<Status> HealthCheck::StopHeathCheckByPID(const std::shared_ptr<litebus::Exec> &exec) const
{
    return litebus::Async(actor_->GetAID(), &HealthCheckActor::StopReapProcessByPID, exec);
}

litebus::Future<messages::InstanceStatusInfo> HealthCheck::QueryInstanceStatusInfo(const std::string &instanceID,
                                                                                   const std::string &runtimeID)
{
    return litebus::Async(actor_->GetAID(), &HealthCheckActor::QueryInstanceStatusInfo, instanceID, runtimeID);
}

litebus::Future<Status> HealthCheck::NotifyOomKillInstanceInAdvance(const std::string &requestID,
                                                                    const std::string &instanceID,
                                                                    const std::string &runtimeID)
{
    return litebus::Async(actor_->GetAID(), &HealthCheckActor::NotifyOomKillInstanceInAdvance, requestID, instanceID,
                          runtimeID);
}

litebus::Future<Status> HealthCheck::DeleteOomNotifyData(const std::string &requestID) const
{
    return litebus::Async(actor_->GetAID(), &HealthCheckActor::DeleteOomNotifyData, requestID);
}

litebus::Future<Status> HealthCheck::GetRuntimeStatus(const std::string &runtimeID) const
{
    return litebus::Async(actor_->GetAID(), &HealthCheckActor::GetRuntimeStatus, runtimeID);
}

void HealthCheck::RemoveRuntimeStatusCache(const std::string &runtimeID) const
{
    litebus::Async(actor_->GetAID(), &HealthCheckActor::RemoveRuntimeStatusCache, runtimeID);
}

}  // namespace functionsystem::runtime_manager