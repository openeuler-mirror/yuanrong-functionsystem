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

#include "domain_sched_mgr.h"

#include <litebus.hpp>

#include "common/constants/actor_name.h"

namespace functionsystem::global_scheduler {

DomainSchedMgr::DomainSchedMgr(const uint32_t heartbeatTimeoutMs)
{
    domainSchedMgrActor_ =
        std::make_shared<DomainSchedMgrActor>(DOMAIN_SCHED_MGR_ACTOR_NAME, heartbeatTimeoutMs);
}

DomainSchedMgr::DomainSchedMgr(std::shared_ptr<DomainSchedMgrActor> domainSchedMgrActor)
    : domainSchedMgrActor_(std::move(domainSchedMgrActor))
{
    ASSERT_IF_NULL(domainSchedMgrActor_);
}

void DomainSchedMgr::Start()
{
    ASSERT_IF_NULL(domainSchedMgrActor_);
    (void)litebus::Spawn(domainSchedMgrActor_);
}

void DomainSchedMgr::Stop()
{
    litebus::Terminate(domainSchedMgrActor_->GetAID());
    litebus::Await(domainSchedMgrActor_->GetAID());
}

void DomainSchedMgr::UpdateSchedTopoView(const std::string &name, const std::string &address,
                                         const messages::ScheduleTopology &topology) const
{
    litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::UpdateSchedTopoView, name, address, topology);
}

void DomainSchedMgr::Registered(const litebus::AID &dst,
                                const litebus::Option<messages::ScheduleTopology> &topology) const
{
    litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::Registered, dst, topology);
}

Status DomainSchedMgr::AddDomainSchedCallback(const functionsystem::global_scheduler::CallbackAddFunc &func) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::AddDomainSchedCallback, func).Get();
}

Status DomainSchedMgr::DelDomainSchedCallback(const functionsystem::global_scheduler::CallbackDelFunc &func) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::DelDomainSchedCallback, func).Get();
}

Status DomainSchedMgr::DelLocalSchedCallback(const functionsystem::global_scheduler::CallbackDelFunc &func) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::DelLocalSchedCallback, func).Get();
}

Status DomainSchedMgr::NotifyWorkerStatusCallback(const CallbackWorkerFunc &func) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::NotifyWorkerStatusCallback, func).Get();
}

litebus::Future<Status> DomainSchedMgr::Connect(const std::string &name, const std::string &address) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::Connect, name, address);
}

void DomainSchedMgr::Disconnect() const
{
    litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::Disconnect);
}

litebus::Future<Status> DomainSchedMgr::Schedule(const std::string &name, const std::string &address,
                                                 const std::shared_ptr<messages::ScheduleRequest> &req,
                                                 uint32_t retryCycle) const
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::Schedule, name, address, req,
                          retryCycle);
}

void DomainSchedMgr::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::UpdateLeaderInfo, leaderInfo);
}

litebus::Future<messages::QueryAgentInfoResponse> DomainSchedMgr::QueryAgentInfo(
    const std::string &name, const std::string &address, const std::shared_ptr<messages::QueryAgentInfoRequest> &req)
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::QueryAgentInfo, name, address, req);
}

litebus::Future<messages::QueryInstancesInfoResponse> DomainSchedMgr::GetSchedulingQueue(
    const std::string &name, const std::string &address,
    const std::shared_ptr<messages::QueryInstancesInfoRequest> &req)
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::GetSchedulingQueue, name, address, req);
}

litebus::Future<messages::QueryResourcesInfoResponse> DomainSchedMgr::QueryResourcesInfo(
    const std::string &name, const std::string &address,
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    return litebus::Async(domainSchedMgrActor_->GetAID(), &DomainSchedMgrActor::QueryResourcesInfo, name, address, req);
}
}  // namespace functionsystem::global_scheduler
