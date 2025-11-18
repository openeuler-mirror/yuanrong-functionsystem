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
#include "domain_group_ctrl.h"

#include "common/status/status.h"
#include "domain_group_control/domain_group_ctrl_actor.h"

namespace functionsystem::domain_scheduler {
void DomainGroupCtrl::TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &DomainGroupCtrlActor::TryCancelSchedule, cancelRequest);
}

litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> DomainGroupCtrl::GetRequests()
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &DomainGroupCtrlActor::GetRequests);
}

}  // namespace functionsystem::domain_scheduler