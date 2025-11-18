
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

#ifndef FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_ACTOR_H
#define FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_ACTOR_H

#include "common/heartbeat/heartbeat_client.h"
#include "common/heartbeat/heartbeat_observer.h"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_client.h"

namespace functionsystem::meta_store {

class MaintenanceServiceActor : public litebus::ActorBase, public MetaStoreHealthyObserver {
public:
    explicit MaintenanceServiceActor();

    ~MaintenanceServiceActor() override = default;

    virtual void HealthCheck(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnHealthyStatus(const Status &status) override;

protected:
    void Init() override;

    Status healthyStatus_ = Status::OK();
    std::shared_ptr<HeartbeatObserveDriver> heartbeatObserveDriver_;
};

}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_ACTOR_H