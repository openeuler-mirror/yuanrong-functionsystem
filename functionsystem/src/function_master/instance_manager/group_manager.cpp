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

#include "group_manager.h"

#include "async/async.hpp"
#include "group_manager_actor.h"

namespace functionsystem::instance_manager {

litebus::Future<Status> GroupManager::OnInstanceAbnormal(
    const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &GroupManagerActor::OnInstanceAbnormal, instanceKey, instanceInfo);
}

/// local abnormal, kill all other instances
litebus::Future<Status> GroupManager::OnLocalAbnormal(const std::string &abnormalLocal)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &GroupManagerActor::OnLocalAbnormal, abnormalLocal);
}

litebus::Future<Status> GroupManager::OnInstancePut(const std::string &instanceKey,
                                                    const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &GroupManagerActor::OnInstancePut, instanceKey, instanceInfo);
}

litebus::Future<Status> GroupManager::OnInstanceDelete(const std::string &instanceKey,
                                                       const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &GroupManagerActor::OnInstanceDelete, instanceKey, instanceInfo);
}

}  // namespace functionsystem::instance_manager