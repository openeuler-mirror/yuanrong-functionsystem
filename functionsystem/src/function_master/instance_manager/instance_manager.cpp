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
#include "instance_manager.h"
#include "instance_manager_actor.h"

namespace functionsystem::instance_manager {
litebus::Future<InstanceKeyInfoPair> InstanceManager::GetInstanceInfoByInstanceID(const std::string &instanceID)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &InstanceManagerActor::GetInstanceInfoByID, instanceID);
}

void InstanceManager::OnHealthyStatus(const Status &status)
{
    // Do not care about MetaStore exceptions.
    if (status.IsError()) {
        return;
    }
    ASSERT_IF_NULL(actor_);
    (void)litebus::Async(actor_->GetAID(), &InstanceManagerActor::OnHealthyStatus, status);
}

litebus::Future<Status> InstanceManager::TryCancelSchedule(const std::string &id, const messages::CancelType &type,
                                                           const std::string &reason)
{
    return litebus::Async(actor_->GetAID(), &InstanceManagerActor::TryCancelSchedule, id, type, reason);
}

}  // namespace functionsystem::instance_manager