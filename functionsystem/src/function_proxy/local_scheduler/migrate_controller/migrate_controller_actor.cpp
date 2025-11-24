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

#include "migrate_controller_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "metadata/constants.h"
#include "utils/struct_transfer.h"

using namespace functionsystem::function_proxy;

namespace functionsystem::local_scheduler {
void MigrateControllerActor::Update(const std::string &instanceID,
                                    const resources::InstanceInfo &instanceInfo,
                                    bool isForceUpdate)
{
    auto owner = instanceInfo.functionproxyid();
    if (!IsInstHibernate(instanceInfo)) {
        YRLOG_DEBUG("InstanceID:{} owner:{} is not hibernate instance, ignore it", instanceID, owner);
        return;
    }
    if (IsDriver(instanceInfo)) {
        YRLOG_DEBUG("InstanceID:{} owner:{} is driver instance, ignore it", instanceID, owner);
        return;
    }
    if (owner != self_) {
        YRLOG_DEBUG("InstanceID:{} owner:{} is not belong to self({}), ignore it", instanceID, owner, self_);
        return;
    }
    auto state = instanceInfo.instancestatus().code();
    StoreInstState(instanceID, state);
}

void MigrateControllerActor::Delete(const std::string &instanceID)
{
    DelInstState(instanceID);
}

litebus::Future<KillResponse> MigrateControllerActor::SuspendInstance(const std::shared_ptr<KillRequest> &killReq)
{
    return KillResponse{};
}

litebus::Future<KillResponse> MigrateControllerActor::RecycleInstance(const std::shared_ptr<KillRequest> &killReq)
{
    return KillResponse{};
}

bool MigrateControllerActor::IsInstHibernate(
    const resources::InstanceInfo &instanceInfo)
{
    auto iter = instanceInfo.createoptions().find(ENABLE_SUSPEND_RESUME);
    if (iter != instanceInfo.createoptions().end()) {
        return iter->second == "true";
    }
    return false;
}

void MigrateControllerActor::StoreInstState(const std::string &instanceID, const int32_t &state)
{
    if (instanceStateMap_.find(instanceID) != instanceStateMap_.end()) {
        instanceStateMap_[instanceID] = state;
        YRLOG_DEBUG("InstanceID:{} state changed to:{}", instanceID, static_cast<uint32_t>(state));
        return;
    }
    instanceStateMap_[instanceID] = state;
    YRLOG_INFO("InstanceID:{} added to migrate monitor map, state:{}", instanceID, static_cast<uint32_t>(state));
}

void MigrateControllerActor::DelInstState(const std::string &instanceID)
{
    if (instanceStateMap_.find(instanceID) == instanceStateMap_.end()) {
        YRLOG_DEBUG("InstanceID:{} is not in monitor map", instanceID);
        return;
    }
    instanceStateMap_.erase(instanceID);
    YRLOG_INFO("InstanceID:{} removed from migrate monitor map", instanceID);
}
}