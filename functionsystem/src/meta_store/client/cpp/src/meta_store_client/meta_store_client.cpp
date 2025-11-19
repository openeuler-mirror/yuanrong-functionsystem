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

#include "meta_store_client.h"

#include <memory>

#include "common/logs/logging.h"
#include "meta_store_monitor/meta_store_monitor_factory.h"
#include "meta_store_client/election/etcd_election_client_strategy.h"
#include "meta_store_client/election/meta_store_election_client_strategy.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/key_value/meta_store_kv_client_strategy.h"
#include "meta_store_client/key_value/watcher.h"
#include "meta_store_client/lease/etcd_lease_client_strategy.h"
#include "meta_store_client/lease/meta_store_lease_client_strategy.h"
#include "meta_store_client/maintenance/etcd_maintenance_client_strategy.h"
#include "meta_store_client/maintenance/meta_store_maintenance_client_strategy.h"
#include "common/metadata/metadata.h"
#include "utils/string_util.h"

namespace functionsystem {
MetaStoreClient::MetaStoreClient(const MetaStoreConfig &metaStoreConfig, const GrpcSslConfig &sslConfig,
                                 const MetaStoreTimeoutOption &timeoutOption,
                                 const std::shared_ptr<MetaStoreExplorer> &metaStoreExplorer)
    : metaStoreConfig_(metaStoreConfig),
      sslConfig_(sslConfig),
      timeoutOption_(timeoutOption),
      metaStoreExplorer_(metaStoreExplorer)
{
    if (metaStoreExplorer_ == nullptr) {
        metaStoreExplorer_ = std::make_shared<MetaStoreDefaultExplorer>(
            metaStoreConfig_.enableMetaStore ? metaStoreConfig_.metaStoreAddress : metaStoreConfig_.etcdAddress);
    }
}

Status MetaStoreClient::Init()
{
    metaStoreClientMgr_ = std::make_shared<MetaStoreClientMgr>(metaStoreConfig_, sslConfig_, timeoutOption_,
                                                               metaStoreExplorer_);
    return metaStoreClientMgr_->Init();
}

std::shared_ptr<MetaStoreClient> MetaStoreClient::Create(const MetaStoreConfig &metaStoreConfig,
                                                         const GrpcSslConfig &sslConfig,
                                                         const MetaStoreTimeoutOption &timeoutOption, bool startMonitor,
                                                         const MetaStoreMonitorParam &param)
{
    std::shared_ptr<MetaStoreExplorer> metaStoreExplorer;
    metaStoreExplorer = std::make_shared<MetaStoreDefaultExplorer>(metaStoreConfig.metaStoreAddress);

    auto client = std::make_shared<MetaStoreClient>(metaStoreConfig, sslConfig, timeoutOption, metaStoreExplorer);
    if (client->Init().IsError()) {
        std::cerr << "failed to init meta-store client, exit" << std::endl;
        return nullptr;
    }

    if (!startMonitor) {
        return client;
    }
    auto address = (metaStoreConfig.enableMetaStore) ? metaStoreConfig.metaStoreAddress : metaStoreConfig.etcdAddress;
    auto monitor = MetaStoreMonitorFactory::GetInstance().InsertMonitor(address, param, client);
    ASSERT_IF_NULL(monitor);
    monitor->RegisterHealthyObserver(client);
    return client;
}

MetaStoreClient::~MetaStoreClient() noexcept
{
}

litebus::Future<std::shared_ptr<PutResponse>> MetaStoreClient::Put(const std::string &key, const std::string &value,
                                                                   const PutOption &option)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(),
                          &meta_store::KvClientStrategy::Put, key, value, option);
}

litebus::Future<std::shared_ptr<DeleteResponse>> MetaStoreClient::Delete(const std::string &key,
                                                                         const DeleteOption &option)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(),
                          &meta_store::KvClientStrategy::Delete, key, option);
}

litebus::Future<std::shared_ptr<GetResponse>> MetaStoreClient::Get(const std::string &key, const GetOption &option)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(),
                          &meta_store::KvClientStrategy::Get, key, option);
}

std::shared_ptr<meta_store::TxnTransaction> MetaStoreClient::BeginTransaction()
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return std::make_shared<meta_store::TxnTransaction>(metaStoreClientMgr_->GetKvClient("")->GetAID());
}

litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> MetaStoreClient::Commit(
    const ::etcdserverpb::TxnRequest &request, bool asyncBackup)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient("")->GetAID(),
                          &meta_store::KvClientStrategy::CommitWithReq, request, asyncBackup);
}

litebus::Future<std::shared_ptr<TxnResponse>> meta_store::TxnTransaction::Commit() const
{
    return litebus::Async(actorAid_, &meta_store::KvClientStrategy::Commit, compares, thenOps, elseOps);
}

litebus::Future<LeaseGrantResponse> MetaStoreClient::Grant(int ttl)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetLeaseClient()->GetAID(),
                          &meta_store::LeaseClientStrategy::Grant, ttl);
}

litebus::Future<LeaseRevokeResponse> MetaStoreClient::Revoke(int64_t leaseId)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetLeaseClient()->GetAID(),
                          &meta_store::LeaseClientStrategy::Revoke, leaseId);
}

litebus::Future<LeaseKeepAliveResponse> MetaStoreClient::KeepAliveOnce(int64_t leaseId)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetLeaseClient()->GetAID(),
                          &meta_store::LeaseClientStrategy::KeepAliveOnce, leaseId);
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreClient::Watch(
    const std::string &key, const WatchOption &option,
    const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer, const SyncerFunction &syncer)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(), &meta_store::KvClientStrategy::Watch,
                          key, option, observer, syncer, nullptr);
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreClient::GetAndWatch(
    const std::string &key, const WatchOption &option,
    const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer, const SyncerFunction &syncer)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(), &meta_store::KvClientStrategy::GetAndWatch,
                          key, option, observer, syncer, nullptr);
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreClient::GetAndWatchWithHandler(
    const std::string &key, const WatchOption &option,
    const std::function<bool(const std::vector<WatchEvent> &, bool)> &observer, const SyncerFunction &syncer,
    const ResponseHandler &handler)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    meta_store::GetAndWatchHandlers handlers {
        .observer = observer,
        .syncer = syncer,
        .responseHandler = handler
    };
    return litebus::Async(metaStoreClientMgr_->GetKvClient(key)->GetAID(),
                          &meta_store::KvClientStrategy::GetAndWatchWithHandler, key, option, handlers, nullptr);
}

litebus::Future<CampaignResponse> MetaStoreClient::Campaign(const std::string &name, int64_t lease,
                                                            const std::string &value)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetElectionClient()->GetAID(),
                          &meta_store::ElectionClientStrategy::Campaign, name, lease, value);
}

litebus::Future<LeaderResponse> MetaStoreClient::Leader(const std::string &name)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetElectionClient()->GetAID(),
                          &meta_store::ElectionClientStrategy::Leader, name);
}

litebus::Future<ResignResponse> MetaStoreClient::Resign(const LeaderKey &leader)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetElectionClient()->GetAID(),
                          &meta_store::ElectionClientStrategy::Resign, leader);
}

litebus::Future<std::shared_ptr<Observer>> MetaStoreClient::Observe(const std::string &name,
                                                                    const std::function<void(LeaderResponse)> &callback)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetElectionClient()->GetAID(),
                          &meta_store::ElectionClientStrategy::Observe, name, callback);
}

litebus::Future<bool> MetaStoreClient::IsConnected()
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return metaStoreClientMgr_->IsConnected();
}

litebus::Future<StatusResponse> MetaStoreClient::HealthCheck()
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    return litebus::Async(metaStoreClientMgr_->GetMaintenanceClient()->GetAID(),
                          &meta_store::MaintenanceClientStrategy::HealthCheck);
}

void MetaStoreClient::OnHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    metaStoreClientMgr_->OnHealthyStatus(status);
}

void MetaStoreClient::BindReconnectedCallBack(const std::function<void(const std::string &)> &callback)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    litebus::Async(metaStoreClientMgr_->GetMaintenanceClient()->GetAID(),
                   &meta_store::MaintenanceClientStrategy::BindReconnectedCallBack, callback);
}

void MetaStoreClient::UpdateMetaStoreAddress(const std::string &address)
{
    ASSERT_IF_NULL(metaStoreClientMgr_);
    metaStoreClientMgr_->UpdateMetaStoreAddress(address);
}

}  // namespace functionsystem
