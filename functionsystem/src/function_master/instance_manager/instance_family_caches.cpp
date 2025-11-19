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

#include "instance_family_caches.h"

#include <string>
#include <unordered_map>

#include "actor/actor.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/constants/signal.h"
#include "common/resource_view/resource_type.h"
#include "common/types/instance_state.h"
#include "function_master/global_scheduler/global_sched.h"

namespace functionsystem::instance_manager {

InstanceFamilyCaches::InstanceFamilyCaches()
{
    // Use "" as a dummy root
    auto info = std::make_shared<InstanceInfo>();
    info->set_instanceid("");
    family_[""] = InstanceFamilyEntry{ .childrenInstanceID = {}, .info = info };
}

std::list<std::shared_ptr<InstanceInfo>> InstanceFamilyCaches::GetAllDescendantsOf(
    const std::string &instanceID, const bool noDetachedInstance)
{
    if (family_.find(instanceID) == family_.end()) {
        return {};
    }
    std::list<std::shared_ptr<InstanceInfo>> descendants{ family_.at(instanceID).info };
    for (auto iter = descendants.begin(); iter != descendants.end(); iter++) {
        if (*iter == nullptr) {
            continue;
        }
        for (const auto& child : family_[(*iter)->instanceid()].childrenInstanceID) {
            if (auto itChild = family_.find(child);
                itChild != family_.end() && (!itChild->second.info->detached() || !noDetachedInstance)) {
                descendants.emplace_back(itChild->second.info);
            }
        }
    }
    descendants.pop_front();
    return descendants;
}

void InstanceFamilyCaches::RemoveInstance(const std::string &instanceID)
{
    auto itInst = family_.find(instanceID);
    if (itInst == family_.end()) {
        YRLOG_WARN("trying to remove non-exists instance({})", instanceID);
        return;
    }
    auto itRoot = family_.find("");
    if (itRoot == family_.end()) {
        YRLOG_WARN("failed to add instance({}), root instance is not existed", instanceID);
        family_.erase(itInst);
        return;
    }
    auto parentID = itInst->second.info->detached() ? "" : itInst->second.info->parentid();
    auto itParent = family_.find(parentID);
    if (itParent == family_.end()) {
        YRLOG_WARN("trying to remove instance({}), but its parent({}) not exists", instanceID, parentID);
        family_.erase(itInst);
        itRoot->second.childrenInstanceID.erase(instanceID);
        return;
    }
    // when node is remove, add its child node to root
    for (const auto &child : itInst->second.childrenInstanceID) {
        if (family_.find(child) != family_.end()) {
            itRoot->second.childrenInstanceID.insert(child);
        }
    }
    family_.erase(itInst);
    itParent->second.childrenInstanceID.erase(instanceID);
    itRoot->second.childrenInstanceID.erase(instanceID);
}

bool InstanceFamilyCaches::IsInstanceExists(const std::string &instanceID)
{
    return family_.find(instanceID) != family_.end();
}

void InstanceFamilyCaches::AddInstance(const std::shared_ptr<InstanceInfo> info)
{
    // if instance is detached, add it to root node (parent exits, it will not exit)
    auto parentID = info->detached() ? "" : info->parentid();
    auto itParent = family_.find(parentID);
    if (itParent == family_.end()) {
        YRLOG_WARN("trying to add instance({}), but its parent({}) not exists", info->instanceid(), parentID);
        itParent = family_.find("");
        if (itParent == family_.end()) {
            YRLOG_WARN("trying to add instance({}), but root not exists", info->instanceid());
            return;
        }
    }
    itParent->second.childrenInstanceID.insert(info->instanceid());

    auto itInst = family_.find(info->instanceid());
    if (itInst != family_.end()) {
        YRLOG_DEBUG("trying to add existed instance({}) again, update info", info->instanceid());
        itInst->second.info = info;
        return;
    }
    family_.emplace(info->instanceid(), InstanceFamilyEntry{ .childrenInstanceID = {}, .info = info });
}

std::shared_ptr<InstanceInfo> InstanceFamilyCaches::GetInstance(const std::string &instanceID)
{
    if (instanceID.empty()) {
        return nullptr;
    }
    auto iter = family_.find(instanceID);
    if (iter == family_.end()) {
        return nullptr;
    }
    return iter->second.info;
}

void InstanceFamilyCaches::SyncInstances(const std::unordered_map<std::string, std::shared_ptr<InstanceInfo>> &infos)
{
    YRLOG_DEBUG("begin sync {} instance", infos.size());
    for (const auto &iter : infos) {
        auto status = iter.second->instancestatus().code();
        if (status == static_cast<int32_t>(InstanceState::EXITING) ||
            status == static_cast<int32_t>(InstanceState::EXITED) ||
            status == static_cast<int32_t>(InstanceState::FATAL)) {
            YRLOG_WARN("instance({}) is not healthy, status({}), skip add parent", iter.second->instanceid(), status);
            continue;
        }
        family_.emplace(iter.second->instanceid(),
                        InstanceFamilyEntry{ .childrenInstanceID = {}, .info = iter.second });
    }
}

std::unordered_map<std::string, InstanceFamilyEntry> InstanceFamilyCaches::GetFamily() const
{
    auto copiedFamily(family_);
    copiedFamily.erase("");
    return copiedFamily;
}

}  // namespace functionsystem::instance_manager
