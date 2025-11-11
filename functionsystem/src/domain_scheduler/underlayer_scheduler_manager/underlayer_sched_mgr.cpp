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
#include "underlayer_sched_mgr.h"

#include <async/async.hpp>

#include "underlayer_sched_mgr_actor.h"
namespace functionsystem::domain_scheduler {
litebus::Future<std::shared_ptr<messages::ScheduleResponse>> UnderlayerSchedMgr::DispatchSchedule(
    const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::DispatchSchedule, selectedName, req);
}

void UnderlayerSchedMgr::UpdateUnderlayerTopo(const messages::ScheduleTopology &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::UpdateUnderlayerTopo, req);
}

litebus::Future<bool> UnderlayerSchedMgr::IsRegistered(const std::string &name)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::IsRegistered, name);
}

void UnderlayerSchedMgr::SetDomainLevel(bool isHeader)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::SetDomainLevel, isHeader);
}

litebus::Future<std::shared_ptr<messages::OnReserves>> UnderlayerSchedMgr::Reserves(
    const std::string &selectedName, const std::shared_ptr<messages::Reserves> &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::Reserves, selectedName, req);
}

litebus::Future<Status> UnderlayerSchedMgr::UnReserve(const std::string &selectedName,
                                                      const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::UnReserve, selectedName, req);
}

litebus::Future<Status> UnderlayerSchedMgr::Bind(
    const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::Bind, selectedName, req);
}

litebus::Future<Status> UnderlayerSchedMgr::UnBind(const std::string &selectedName,
                                                   const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(aid_, &UnderlayerSchedMgrActor::UnBind, selectedName, req);
}

void UnderlayerSchedMgr::SetScalerAddress(const std::string &address)
{
    litebus::Async(aid_, &UnderlayerSchedMgrActor::SetScalerAddress, address);
}
} // namespace functionsystem::domain_scheduler