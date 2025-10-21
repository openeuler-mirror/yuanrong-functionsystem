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
#include "resource_group_ctrl.h"
#include "resource_group_ctrl_actor.h"

namespace functionsystem::local_scheduler {

std::shared_ptr<ResourceGroupCtrl> ResourceGroupCtrl::Init()
{
    auto actor = std::make_shared<ResourceGroupCtrlActor>();
    litebus::Spawn(actor);
    return std::make_shared<ResourceGroupCtrl>(actor);
}

litebus::Future<std::shared_ptr<CreateResourceGroupResponse>> ResourceGroupCtrl::Create(
    const std::string &from, const std::shared_ptr<CreateResourceGroupRequest> &req)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &ResourceGroupCtrlActor::Create, from, req);
}

litebus::Future<KillResponse> ResourceGroupCtrl::Kill(const std::string &from, const std::string &srcTenantID,
                                                       const std::shared_ptr<KillRequest> &killReq)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &ResourceGroupCtrlActor::Kill, from, srcTenantID, killReq);
}
}