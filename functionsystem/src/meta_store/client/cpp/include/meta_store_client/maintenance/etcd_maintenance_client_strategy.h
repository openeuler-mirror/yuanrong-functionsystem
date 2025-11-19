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

#ifndef COMMON_META_STORE_CLIENT_MAINTENANCE_ETCD_MAINTENANCE_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_MAINTENANCE_ETCD_MAINTENANCE_CLIENT_STRATEGY_H
#include "common/rpc/client/grpc_client.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "maintenance_client_strategy.h"

namespace functionsystem::meta_store {

class EtcdMaintenanceClientStrategy : public MaintenanceClientStrategy,
                                      public std::enable_shared_from_this<EtcdMaintenanceClientStrategy> {
public:
    EtcdMaintenanceClientStrategy(const std::string &name, const std::string &address,
                                  const std::shared_ptr<MetaStoreExplorer> &explorer,
                                  const MetaStoreTimeoutOption &timeoutOption, const GrpcSslConfig &sslConfig = {});

    ~EtcdMaintenanceClientStrategy() override = default;

    litebus::Future<StatusResponse> HealthCheck() override;
    litebus::Future<bool> IsConnected() override;
    void CheckChannelAndWaitForReconnect() override;
    void OnAddressUpdated(const std::string &address) override;

private:
    std::unique_ptr<GrpcClient<etcdserverpb::Maintenance>> client_;
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_MAINTENANCE_ETCD_MAINTENANCE_CLIENT_STRATEGY_H
