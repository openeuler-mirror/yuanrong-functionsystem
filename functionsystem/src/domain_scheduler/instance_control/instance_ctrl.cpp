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

#include "instance_ctrl.h"

#include <async/async.hpp>

#include "domain_scheduler/instance_control/instance_ctrl_actor.h"

namespace functionsystem::domain_scheduler {
InstanceCtrl::InstanceCtrl(const litebus::AID &aid) : aid_(aid)
{
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrl::Schedule(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    return litebus::Async(aid_, &InstanceCtrlActor::Schedule, req);
}

void InstanceCtrl::UpdateMaxSchedRetryTimes(const uint32_t &retrys)
{
    litebus::Async(aid_, &InstanceCtrlActor::UpdateMaxSchedRetryTimes, retrys);
}

void InstanceCtrl::SetDomainLevel(bool isHeader)
{
    litebus::Async(aid_, &InstanceCtrlActor::SetDomainLevel, isHeader);
}

void InstanceCtrl::SetScalerAddress(const std::string &address)
{
    litebus::Async(aid_, &InstanceCtrlActor::SetScalerAddress, address);
}

void InstanceCtrl::TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    litebus::Async(aid_, &InstanceCtrlActor::TryCancelSchedule, cancelRequest);
}

litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> InstanceCtrl::GetSchedulerQueue()
{
    return litebus::Async(aid_, &InstanceCtrlActor::GetSchedulerQueue);
}

}  // namespace functionsystem::domain_scheduler
