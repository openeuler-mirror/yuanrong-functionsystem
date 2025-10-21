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
#include "local_group_ctrl.h"
#include "local_group_ctrl_actor.h"
namespace functionsystem::local_scheduler {
litebus::Future<std::shared_ptr<CreateResponses>> LocalGroupCtrl::GroupSchedule(
    const std::string &from, const std::shared_ptr<CreateRequests> &req)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &LocalGroupCtrlActor::GroupSchedule, from, req);
}

void LocalGroupCtrl::OnHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &LocalGroupCtrlActor::OnHealthyStatus, status);
}
}  // namespace functionsystem::local_scheduler