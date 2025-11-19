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

#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_H

#include <utility>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "meta_store_monitor/meta_store_healthy_observer.h"

namespace functionsystem::instance_manager {
using InstanceKeyInfoPair = std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>>;
class InstanceManager : public MetaStoreHealthyObserver {
public:
    explicit InstanceManager(litebus::ActorReference actor) : actor_(std::move(actor)){};

    ~InstanceManager() override = default;

    virtual litebus::Future<std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>>>
        GetInstanceInfoByInstanceID(const std::string &instanceID);

    void OnHealthyStatus(const Status &status) override;

    litebus::Future<Status> TryCancelSchedule(const std::string &id, const messages::CancelType &type,
                                              const std::string &reason);
private:
    litebus::ActorReference actor_{ nullptr };
};  // class InstanceManager
}  // namespace functionsystem::instance_manager

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_H
