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

#ifndef FUNCTIONSYSTEM_META_STORE_META_STORE_CLIENT_H
#define FUNCTIONSYSTEM_META_STORE_META_STORE_CLIENT_H

#include "async/future.hpp"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/key_value/watcher.h"
#include "meta_store_client/maintenance/maintenance_client_strategy.h"
#include "meta_store_client/meta_store_client_mgr.h"
#include "common/rpc/client/grpc_client.h"
#include "election_client.h"
#include "key_value_client.h"
#include "lease_client.h"
#include "maintenance_client.h"
#include "txn_transaction.h"
#include "watch_client.h"

namespace functionsystem {
class MetaStoreClient : public meta_store::KeyValueClient,
                        public meta_store::LeaseClient,
                        public meta_store::WatchClient,
                        public meta_store::ElectionClient,
                        public meta_store::MaintenanceClient,
                        public MetaStoreHealthyObserver {
public:
    MetaStoreClient() = delete;

    explicit MetaStoreClient(const MetaStoreConfig &metaStoreConfig, const GrpcSslConfig &sslConfig = {},
                             const MetaStoreTimeoutOption &timeoutOption = MetaStoreTimeoutOption(),
                             const std::shared_ptr<MetaStoreExplorer> &metaStoreExplorer = nullptr);

    static std::shared_ptr<MetaStoreClient> Create(
        const MetaStoreConfig &metaStoreConfig, const GrpcSslConfig &sslConfig = {},
        const MetaStoreTimeoutOption &timeoutOption = MetaStoreTimeoutOption(), bool startMonitor = false,
        const MetaStoreMonitorParam &param = MetaStoreMonitorParam());

    Status Init();

    void OnGetMetaStoreAddress();

    ~MetaStoreClient() noexcept override;

    litebus::Future<std::shared_ptr<PutResponse>> Put(const std::string &key, const std::string &value,
                                                      const PutOption &option) override;

    litebus::Future<std::shared_ptr<DeleteResponse>> Delete(const std::string &key,
                                                            const DeleteOption &option) override;

    litebus::Future<std::shared_ptr<GetResponse>> Get(const std::string &key, const GetOption &option) override;

    std::shared_ptr<meta_store::TxnTransaction> BeginTransaction() override;

    litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> Commit(const ::etcdserverpb::TxnRequest &request,
                                                                         bool asyncBackup) override;

    litebus::Future<LeaseGrantResponse> Grant(int ttl) override;

    litebus::Future<LeaseRevokeResponse> Revoke(int64_t leaseId) override;

    litebus::Future<LeaseKeepAliveResponse> KeepAliveOnce(int64_t leaseId) override;

    litebus::Future<std::shared_ptr<Watcher>> Watch(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
        const SyncerFunction &syncer) override;

    litebus::Future<std::shared_ptr<Watcher>> GetAndWatch(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer,
        const SyncerFunction &syncer) override;

    litebus::Future<std::shared_ptr<Watcher>> GetAndWatchWithHandler(
        const std::string &key, const WatchOption &option,
        const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer, const SyncerFunction &syncer,
        const ResponseHandler &handler) override;

    litebus::Future<CampaignResponse> Campaign(const std::string &name, int64_t lease,
                                               const std::string &value) override;

    litebus::Future<LeaderResponse> Leader(const std::string &name) override;

    litebus::Future<ResignResponse> Resign(const LeaderKey &leader) override;

    litebus::Future<std::shared_ptr<Observer>> Observe(const std::string &name,
                                                       const std::function<void(LeaderResponse)> &callback) override;
    litebus::Future<bool> IsConnected() override;

    litebus::Future<StatusResponse> HealthCheck() override;

    void BindReconnectedCallBack(const std::function<void(const std::string &)> &callback) override;

    void OnHealthyStatus(const Status &status) override;

    void UpdateMetaStoreAddress(const std::string &address);

    // for test
    [[maybe_unused]] std::shared_ptr<meta_store::MaintenanceClientStrategy> GetMaintenanceClientActor()
    {
        return metaStoreClientMgr_->GetMaintenanceClient();
    }

    std::shared_ptr<meta_store::KvClientStrategy> GetEtcdKvClientStrategy()
    {
        return metaStoreClientMgr_->GetEtcdKvClient();
    }

    const std::string &GetTablePrefix()
    {
        return metaStoreConfig_.etcdTablePrefix;
    }

private:
    MetaStoreConfig metaStoreConfig_;

    GrpcSslConfig sslConfig_;

    MetaStoreTimeoutOption timeoutOption_;

    std::shared_ptr<MetaStoreExplorer> metaStoreExplorer_{ nullptr };

    std::shared_ptr<MetaStoreClientMgr> metaStoreClientMgr_{ nullptr };
};
}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_META_STORE_META_STORE_CLIENT_H
