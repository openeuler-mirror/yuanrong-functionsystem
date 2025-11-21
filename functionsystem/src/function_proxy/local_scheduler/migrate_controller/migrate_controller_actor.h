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

#ifndef LOCAL_SCHEDULER_MIGRATE_CONTROLLER_ACTOR_H
#define LOCAL_SCHEDULER_MIGRATE_CONTROLLER_ACTOR_H

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "common/observer/control_plane_observer/control_plane_observer.h"
#include "local_scheduler/instance_control/instance_ctrl.h"
#include "proto/pb/posix/resource.pb.h"
#include "status/status.h"

namespace functionsystem::local_scheduler {
class MigrateControllerActor : public litebus::ActorBase {
public:
    MigrateControllerActor(const std::string &name, const std::string &self,
                           const litebus::AID &observer_id)
        : ActorBase(name),
          self_(self),
          observerId_(observer_id)
    {
    }

    void Update(const std::string &instanceID, const resources::InstanceInfo &instanceInfo,
                bool isForceUpdate);

    void Delete(const std::string &instanceID);

    void CallQueueChangeCallBack(const std::string &instanceID, const int utilization);

    void CheckPointRespCallback(const std::string &instanceID,
                                const std::shared_ptr<runtime::CheckpointResponse> &status);

    void StoreInstState(const std::string &instanceID, const int32_t &state);
    void DelInstState(const std::string &instanceID);

    litebus::Future<KillResponse> SuspendInstance(const std::shared_ptr<KillRequest> &killReq);

    litebus::Future<KillResponse> RecycleInstance(const std::shared_ptr<KillRequest> &killReq);

private:
    bool IsInstHibernate(const resources::InstanceInfo &instanceInfo);

    uint32_t GetInstanceIdleTime(const std::string &instanceID, const resources::InstanceInfo &info);

    std::string self_;
    std::unordered_map<std::string, int32_t> instanceStateMap_;
    std::unordered_map<std::string, int32_t> idleTimerMap_;
    litebus::AID observerId_;
    uint32_t systemIdleToSuspend = 0;
    std::shared_ptr<InstanceCtrl> instanceCtrl_ = nullptr;
};
}

#endif // LOCAL_SCHEDULER_MIGRATE_CONTROLLER_ACTOR_H