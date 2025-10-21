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

#include "local_sched_srv_actor_test_driver.h"

#include <async/async.hpp>

#include "local_scheduler/local_scheduler_service/local_sched_srv_actor.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
litebus::Future<messages::ScheduleResponse> LocalSchedSrvActorTestDriver::Schedule(const litebus::AID &to,
                                                                                   const messages::ScheduleRequest &req)
{
    Send(to, "Schedule", std::move(req.SerializeAsString()));
    scheduleRspPromise_ = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    return scheduleRspPromise_->GetFuture();
}

void LocalSchedSrvActorTestDriver::ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::ScheduleResponse rsp;
    (void)rsp.ParseFromString(msg);

    scheduleRspPromise_->SetValue(rsp);
}

void LocalSchedSrvActorTestDriver::UpdateSchedTopoView(const litebus::AID &to, const messages::ScheduleTopology &topo)
{
    Send(to, "UpdateSchedTopoView", std::move(topo.SerializeAsString()));
}

litebus::Future<litebus::AID> LocalSchedSrvActorTestDriver::GetDomainSchedulerAID(const litebus::AID &to)
{
    return litebus::Async(to, &LocalSchedSrvActor::GetDomainSchedAID);
}

litebus::Future<Status> LocalSchedSrvActorTestDriver::Register(const litebus::AID &to)
{
    return litebus::Async(to, &LocalSchedSrvActor::Register);
}

void LocalSchedSrvActorTestDriver::UpdateMasterInfo(const litebus::AID &to, const explorer::LeaderInfo &leaderInfo)
{
    litebus::Async(to, &LocalSchedSrvActor::UpdateMasterInfo, leaderInfo);
}

litebus::Future<Status> LocalSchedSrvActorTestDriver::NotifyDsHealthy(const litebus::AID &to, bool healthy)
{
    return litebus::Async(to, &LocalSchedSrvActor::NotifyWorkerStatus, healthy);
}

litebus::Future<messages::ScheduleResponse> LocalSchedSrvActorTestDriver::ForwardSchedule(
    const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(to, &LocalSchedSrvActor::ForwardSchedule, req);
}

litebus::Future<Status> LocalSchedSrvActorTestDriver::TryCancelSchedule(
    const litebus::AID &to, const std::shared_ptr<messages::CancelSchedule> &req)
{
    return litebus::Async(to, &LocalSchedSrvActor::TryCancelSchedule, req);
}

void LocalSchedSrvActorTestDriver::Init()
{
    Receive("ResponseSchedule", &LocalSchedSrvActorTestDriver::ResponseSchedule);
}
}  // namespace functionsystem::test
