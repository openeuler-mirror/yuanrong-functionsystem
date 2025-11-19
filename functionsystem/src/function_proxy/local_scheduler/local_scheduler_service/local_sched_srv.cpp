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

#include "local_sched_srv.h"

#include <async/async.hpp>

#include "common/constants/actor_name.h"
#include "local_sched_srv_actor.h"

namespace functionsystem::local_scheduler {
std::unique_ptr<LocalSchedSrv> LocalSchedSrv::Create(const LocalSchedSrvActor::Param &param)
{
    auto localSchedSrvActor = std::make_shared<LocalSchedSrvActor>(param);
    return std::make_unique<LocalSchedSrv>(std::move(localSchedSrvActor));
}

LocalSchedSrv::LocalSchedSrv(std::shared_ptr<LocalSchedSrvActor> &&actor)
    : ActorDriver(actor), actor_(std::move(actor))
{
}

LocalSchedSrv::~LocalSchedSrv()
{
}

void LocalSchedSrv::Start(const std::shared_ptr<InstanceCtrl> &instanceCtrl,
                          const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
{
    BindInstanceCtrl(instanceCtrl);
    BindResourceView(resourceViewMgr);
    litebus::Spawn(actor_);
}

litebus::Future<messages::ScheduleResponse> LocalSchedSrv::ForwardSchedule(
    const std::shared_ptr<messages::ScheduleRequest> &req) const
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::ForwardSchedule, req);
}

litebus::Future<messages::ForwardKillResponse> LocalSchedSrv::ForwardKillToInstanceManager(
    const std::shared_ptr<messages::ForwardKillRequest> &req)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::ForwardKillToInstanceManager, req);
}

void LocalSchedSrv::BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
{
    ASSERT_IF_NULL(instanceCtrl);
    ASSERT_IF_NULL(actor_);
    actor_->BindInstanceCtrl(instanceCtrl);
}

void LocalSchedSrv::BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
{
    ASSERT_IF_NULL(resourceViewMgr);
    ASSERT_IF_NULL(actor_);
    actor_->BindResourceView(resourceViewMgr);
}

litebus::Future<Status> LocalSchedSrv::NotifyDsHealthy(bool healthy) const
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::NotifyWorkerStatus, healthy);
}

void LocalSchedSrv::NotifyEvictResult(const std::shared_ptr<messages::EvictAgentResult> &req)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::NotifyEvictResult, req);
}

void LocalSchedSrv::DeletePod(const std::string &agentID, const std::string &reqID, const std::string &msg)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::DeletePod, agentID, reqID, msg);
}

void LocalSchedSrv::BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr)
{
    ASSERT_IF_NULL(functionAgentMgr);
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::BindFunctionAgentMgr, functionAgentMgr);
}

litebus::Future<messages::GroupResponse> LocalSchedSrv::ForwardGroupSchedule(
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::ForwardGroupSchedule, groupInfo);
}

litebus::Future<Status> LocalSchedSrv::KillGroup(const std::shared_ptr<messages::KillGroup> &killReq)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::KillGroup, killReq);
}

litebus::Future<Status> LocalSchedSrv::TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::TryCancelSchedule, cancelRequest);
}

litebus::Future<Status> LocalSchedSrv::GracefulShutdown()
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::GracefulShutdown);
}

litebus::Future<Status> LocalSchedSrv::IsRegisteredToGlobal()
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::IsRegisteredToGlobal);
}

litebus::Future<std::string> LocalSchedSrv::QueryMasterIP()
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::QueryMasterIP);
}

void LocalSchedSrv::BindSubscriptionMgr(const std::shared_ptr<SubscriptionMgr> &subscriptionMgr)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &LocalSchedSrvActor::BindSubscriptionMgr, subscriptionMgr);
}

}  // namespace functionsystem::local_scheduler
