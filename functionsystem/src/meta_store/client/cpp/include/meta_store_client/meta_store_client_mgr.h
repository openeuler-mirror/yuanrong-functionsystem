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

#ifndef FUNCTIONSYSTEM_META_STORE_CLIENT_MGR_H
#define FUNCTIONSYSTEM_META_STORE_CLIENT_MGR_H

#include "common/rpc/client/grpc_client.h"
#include "meta_store_client/election/election_client_strategy.h"
#include "meta_store_client/key_value/kv_client_strategy.h"
#include "meta_store_client/lease/lease_client_strategy.h"
#include "meta_store_client/maintenance/maintenance_client_strategy.h"

namespace functionsystem {
class MetaStoreClientMgr {
public:
    explicit MetaStoreClientMgr(const MetaStoreConfig &metaStoreConfig, const GrpcSslConfig &sslConfig = {},
                                const MetaStoreTimeoutOption &timeoutOption = MetaStoreTimeoutOption(),
                                const std::shared_ptr<MetaStoreExplorer> &metaStoreExplorer = nullptr);
    ~MetaStoreClientMgr();

    Status Init();

    litebus::Future<bool> IsConnected();
    void OnHealthyStatus(const Status &status);
    std::shared_ptr<meta_store::KvClientStrategy> GetKvClient(const std::string &key);
    std::shared_ptr<meta_store::LeaseClientStrategy> GetLeaseClient();
    std::shared_ptr<meta_store::ElectionClientStrategy> GetElectionClient();
    std::shared_ptr<meta_store::MaintenanceClientStrategy> GetMaintenanceClient();
    std::shared_ptr<meta_store::KvClientStrategy> GetEtcdKvClient();
    void UpdateMetaStoreAddress(const std::string &address);

private:
    // etcd client
    std::shared_ptr<meta_store::KvClientStrategy> etcdKvClient_{ nullptr };
    std::shared_ptr<meta_store::LeaseClientStrategy> etcdLeaseClient_{ nullptr };
    std::shared_ptr<meta_store::ElectionClientStrategy> etcdElectionClient_{ nullptr };
    std::shared_ptr<meta_store::MaintenanceClientStrategy> etcdMaintenanceClient_{ nullptr };
    // meta store client
    std::shared_ptr<meta_store::KvClientStrategy> msKvClient_{ nullptr };
    std::shared_ptr<meta_store::LeaseClientStrategy> msLeaseClient_{ nullptr };
    std::shared_ptr<meta_store::ElectionClientStrategy> msElectionClient_{ nullptr };
    std::shared_ptr<meta_store::MaintenanceClientStrategy> msMaintenanceClient_{ nullptr };

    MetaStoreConfig metaStoreConfig_;
    GrpcSslConfig sslConfig_;
    MetaStoreTimeoutOption timeoutOption_;
    std::shared_ptr<MetaStoreExplorer> metaStoreExplorer_{ nullptr };

    void InitEtcdClients();
    void InitMetaStoreClients();

    void OnEtcdHealthyStatus(const Status &status);
    void OnMetaStoreHealthyStatus(const Status &status);

    std::list<litebus::Future<bool>> IsEtcdConnected();
    std::list<litebus::Future<bool>> IsMetaStoreConnected();

    bool IsMetaStoreExcludedKey(const std::string &key);
};
}
#endif  // FUNCTIONSYSTEM_META_STORE_CLIENT_MGR_H
