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

#ifndef FUNCTION_MASTER_META_STORE_DRIVER_H
#define FUNCTION_MASTER_META_STORE_DRIVER_H

#include "common/http/http_server.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "common/utils/module_driver.h"
#include "backup_actor.h"
#include "kv_service_accessor_actor.h"
#include "kv_service_actor.h"
#include "lease_service_actor.h"
#include "maintenance_service_actor.h"
#include "litebus.hpp"

namespace functionsystem::meta_store {

class MetaStoreDriver : public ModuleDriver {
public:
    MetaStoreDriver(){};

    ~MetaStoreDriver() override = default;

    Status Start() override;

    struct StartParams {
        std::string backupAddress;
        MetaStoreTimeoutOption timeoutOption = {};
        GrpcSslConfig sslConfig = {};
        MetaStoreBackupOption backupOption = {};
        bool isExplore = false;
        MetaStoreMonitorParam monitorParam = {};
    };

    Status Start(const StartParams& params);

    Status Stop() override;

    void Await() override;

private:
    class MetaStoreApiRouteRegister : public ApiRouterRegister {
    public:
        explicit MetaStoreApiRouteRegister(const std::string &address);
    };

    std::shared_ptr<EtcdKvClientStrategy> persistActor_ = nullptr;
    std::shared_ptr<BackupActor> backupActor_ = nullptr;
    std::shared_ptr<KvServiceActor> kvServiceActor_ = nullptr;
    std::shared_ptr<KvServiceAccessorActor> kvServiceAccessorActor_ = nullptr;
    std::shared_ptr<LeaseServiceActor> leaseServiceActor_ = nullptr;
    std::shared_ptr<MaintenanceServiceActor> maintenanceServiceActor_ = nullptr;
};  // class MetaStoreDriver
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_DRIVER_H