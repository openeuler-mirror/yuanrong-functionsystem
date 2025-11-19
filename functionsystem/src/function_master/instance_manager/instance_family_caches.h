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

#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_FAMILY_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_FAMILY_H

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/resource_view/resource_type.h"

namespace functionsystem::instance_manager {
using InstanceInfo = resource_view::InstanceInfo;

struct InstanceFamilyEntry {
    std::unordered_set<std::string> childrenInstanceID{};
    std::shared_ptr<InstanceInfo> info{ nullptr };
};

/// InstanceFamily is used in InstanceManagerActor, so this is also no lock
class InstanceFamilyCaches {
public:
    /// When construct the caches, it will automatically make a dummy root node ("")
    InstanceFamilyCaches();

    /// recursively GetAllDescendantsOf @param instanceID
    /// if @param noDetachedInstance is false, detached children will also be added into the result, or only
    /// attached instance will be got.
    /// This is guaranteed BFS
    std::list<std::shared_ptr<InstanceInfo>> GetAllDescendantsOf(const std::string &instanceID,
                                                                 bool noDetachedInstance = true);

    /// RemoveInstance @param instanceID in family, and also remove the children from its father, but won't affect it's
    /// own children
    void RemoveInstance(const std::string &instanceID);

    /// Check if an instance @param instanceID exists in cache
    bool IsInstanceExists(const std::string &instanceID);

    /// Add an instance, if exists, update the info only (will never update its parent)
    void AddInstance(std::shared_ptr<InstanceInfo> info);

    /**
     *
     * get instance by instanceID
     *
     * @param instanceID instanceID
     * @return instance info
     */
    std::shared_ptr<InstanceInfo> GetInstance(const std::string &instanceID);

    /// sync all instance after restart, add all instances as a parent
    void SyncInstances(const std::unordered_map<std::string, std::shared_ptr<InstanceInfo>> &infos);

    /// Get a copy of current family tree
    /// ! Should Only Use For Test
    std::unordered_map<std::string, InstanceFamilyEntry> GetFamily() const;

private:
    std::unordered_map<std::string, InstanceFamilyEntry> family_;
};  // class InstanceFamily

}  // namespace functionsystem::instance_manager

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_FAMILY_H
