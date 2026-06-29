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

#include "backup_actor.h"
#include "common/http/http_server.h"
#include "common/utils/module_driver.h"
#include "kv_service_accessor_actor.h"
#include "kv_service_actor.h"
#include "lease_service_actor.h"
#include "litebus.hpp"
#include "maintenance_service_actor.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "passthrough/election_service_passthrough_actor.h"
#include "passthrough/kv_service_passthrough_actor.h"
#include "passthrough/lease_service_passthrough_actor.h"
#include "passthrough/maintenance_service_passthrough_actor.h"
#include "passthrough/watch_service_passthrough_actor.h"

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

    Status StartPassthrough(const std::string &etcdAddress, const MetaStoreTimeoutOption &timeoutOption = {},
                            const GrpcSslConfig &sslConfig = {}, const MetaStoreMonitorParam &param = {});

    Status Stop() override;

    void Await() override;

    void StartHttpServer(const std::string &ip);

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

    // election only support etcd passthrough
    std::shared_ptr<ElectionServicePassthroughActor> electionServiceActor_ = nullptr;
    std::shared_ptr<HttpServer> httpServer_ = nullptr;
};  // class MetaStoreDriver
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_DRIVER_H