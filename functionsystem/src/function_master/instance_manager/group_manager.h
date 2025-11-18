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
#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_H
#include "async/future.hpp"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"

namespace functionsystem::instance_manager {
class GroupManager {
public:
    explicit GroupManager(const litebus::ActorReference &actor) : actor_(actor){};
    virtual ~GroupManager() = default;

    /// instance abnormal, kill all other instances
    virtual litebus::Future<Status> OnInstanceAbnormal(
        const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

    /// local abnormal, kill all other instances
    virtual litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal);

    virtual litebus::Future<Status> OnInstancePut(const std::string &instanceKey,
                                                  const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

    virtual litebus::Future<Status> OnInstanceDelete(const std::string &instanceKey,
                                                     const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

private:
    litebus::ActorReference actor_;
};
}  // namespace functionsystem::instance_manager

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_H
