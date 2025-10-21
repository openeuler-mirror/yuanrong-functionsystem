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

#include "local_sched_mgr.h"

#include <litebus.hpp>

#include "common/constants/actor_name.h"

namespace functionsystem::global_scheduler {

LocalSchedMgr::LocalSchedMgr()
{
    localSchedMgrActor_ = std::make_shared<LocalSchedMgrActor>(LOCAL_SCHED_MGR_ACTOR_NAME);
}

LocalSchedMgr::LocalSchedMgr(std::shared_ptr<LocalSchedMgrActor> localSchedMgrActor)
    : localSchedMgrActor_(std::move(localSchedMgrActor))
{
}

LocalSchedMgr::~LocalSchedMgr()
{
}

void LocalSchedMgr::Start()
{
    ASSERT_IF_NULL(localSchedMgrActor_);
    (void)litebus::Spawn(localSchedMgrActor_);
}

void LocalSchedMgr::Stop()
{
    litebus::Terminate(localSchedMgrActor_->GetAID());
    litebus::Await(localSchedMgrActor_->GetAID());
}

Status LocalSchedMgr::AddLocalSchedCallback(const CallbackAddFunc &func) const
{
    return litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::AddLocalSchedCallback, func).Get();
}

litebus::Future<Status> LocalSchedMgr::DelLocalSchedCallback(const CallbackDelFunc &func) const
{
    return litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::DelLocalSchedCallback, func);
}

void LocalSchedMgr::UpdateSchedTopoView(const std::string &address, const messages::ScheduleTopology &topology)
{
    litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::UpdateSchedTopoView, address, topology);
}

void LocalSchedMgr::Registered(const litebus::AID &dst,
                               const litebus::Option<messages::ScheduleTopology> &topology) const
{
    litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::Registered, dst, topology);
}

void LocalSchedMgr::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::UpdateLeaderInfo, leaderInfo);
}

litebus::Future<Status> LocalSchedMgr::EvictAgentOnLocal(const std::string &address,
                                                         const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    return litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, address, req);
}

void LocalSchedMgr::OnLocalAbnormal(const std::string &localID, const std::string &address)
{
    litebus::Async(localSchedMgrActor_->GetAID(), &LocalSchedMgrActor::OnLocalAbnormal, localID, address);
}

}  // namespace functionsystem::global_scheduler
