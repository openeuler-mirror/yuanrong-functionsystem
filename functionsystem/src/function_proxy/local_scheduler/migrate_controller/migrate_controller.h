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

#ifndef LOCAL_SCHEDULER_MIGRATE_CONTROLLER_H
#define LOCAL_SCHEDULER_MIGRATE_CONTROLLER_H
#include "common/state_machine/instance_listener.h"
#include "migrate_controller_actor.h"
#include "status/status.h"

namespace functionsystem::local_scheduler {
class MigrateController : public InstanceListener {
public:
    void Update(const std::string &instanceID, const resources::InstanceInfo &instanceInfo,
                bool isForceUpdate) override;

    void Delete(const std::string &instanceID) override;

    litebus::Future<KillResponse> SuspendInstance(const std::shared_ptr<KillRequest> &killReq);

    litebus::Future<KillResponse> RecycleInstance(const std::shared_ptr<KillRequest> &killReq);

private:
    std::shared_ptr<MigrateControllerActor> migrateControllerActor_;
};
}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_MIGRATE_CONTROLLER_H