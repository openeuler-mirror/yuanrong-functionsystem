/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_PASSTHROUGH_ACTOR_H
#define FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_PASSTHROUGH_ACTOR_H

#include "meta_store_client/meta_store_client.h"
#include "meta_store/server/src/maintenance_service_actor.h"

namespace functionsystem::meta_store {

class MaintenanceServicePassthroughActor : public MaintenanceServiceActor {
public:
    explicit MaintenanceServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient)
        : MaintenanceServiceActor(), etcdClient_(etcdClient)
    {
    }

    ~MaintenanceServicePassthroughActor() override = default;

    void HealthCheck(const litebus::AID &from, std::string &&name, std::string &&msg) override;

private:
    void OnHealthCheck(const litebus::Future<StatusResponse> &response, const std::string &id,
                       const litebus::AID &from);
    
    std::shared_ptr<MetaStoreClient> etcdClient_;
};

}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_MAINTENANCE_SERVICE_PASSTHROUGH_ACTOR_H