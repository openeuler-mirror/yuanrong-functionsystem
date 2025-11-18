/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
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

#include "meta_store_client_mgr.h"

#include "async/collect.hpp"
#include "async/defer.hpp"

#include "meta_store_client/election/etcd_election_client_strategy.h"
#include "meta_store_client/election/meta_store_election_client_strategy.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/key_value/meta_store_kv_client_strategy.h"
#include "meta_store_client/key_value/watcher.h"
#include "meta_store_client/lease/etcd_lease_client_strategy.h"
#include "meta_store_client/lease/meta_store_lease_client_strategy.h"
#include "meta_store_client/maintenance/etcd_maintenance_client_strategy.h"
#include "meta_store_client/maintenance/meta_store_maintenance_client_strategy.h"

namespace functionsystem {

MetaStoreClientMgr::MetaStoreClientMgr(const MetaStoreConfig &metaStoreConfig_, const GrpcSslConfig &sslConfig_,
                                       const MetaStoreTimeoutOption &timeoutOption_,
                                       const std::shared_ptr<MetaStoreExplorer> &metaStoreExplorer)
    : metaStoreConfig_(metaStoreConfig_), sslConfig_(sslConfig_), timeoutOption_(timeoutOption_),
      metaStoreExplorer_(metaStoreExplorer)
{
    if (metaStoreExplorer_ == nullptr) {
        metaStoreExplorer_ = std::make_shared<MetaStoreDefaultExplorer>(
            metaStoreConfig_.enableMetaStore ? metaStoreConfig_.metaStoreAddress : metaStoreConfig_.etcdAddress);
    }
}

MetaStoreClientMgr::~MetaStoreClientMgr()
{
    if (etcdElectionClient_ != nullptr) {
        litebus::Terminate(etcdElectionClient_->GetAID());
    }
    if (etcdLeaseClient_ != nullptr) {
        litebus::Terminate(etcdLeaseClient_->GetAID());
    }
    if (etcdKvClient_ != nullptr) {
        litebus::Terminate(etcdKvClient_->GetAID());
    }
    if (etcdMaintenanceClient_ != nullptr) {
        litebus::Terminate(etcdMaintenanceClient_->GetAID());
    }
    if (msElectionClient_ != nullptr) {
        litebus::Terminate(msElectionClient_->GetAID());
    }
    if (msLeaseClient_ != nullptr) {
        litebus::Terminate(msLeaseClient_->GetAID());
    }
    if (msKvClient_ != nullptr) {
        litebus::Terminate(msKvClient_->GetAID());
    }
    if (msMaintenanceClient_ != nullptr) {
        litebus::Terminate(msMaintenanceClient_->GetAID());
    }
    
    if (etcdElectionClient_ != nullptr) {
        litebus::Await(etcdElectionClient_->GetAID());
    }
    if (etcdLeaseClient_ != nullptr) {
        litebus::Await(etcdLeaseClient_->GetAID());
    }
    if (etcdKvClient_ != nullptr) {
        litebus::Await(etcdKvClient_->GetAID());
    }
    if (etcdMaintenanceClient_ != nullptr) {
        litebus::Await(etcdMaintenanceClient_->GetAID());
    }
    if (msElectionClient_ != nullptr) {
        litebus::Await(msElectionClient_->GetAID());
    }
    if (msLeaseClient_ != nullptr) {
        litebus::Await(msLeaseClient_->GetAID());
    }
    if (msKvClient_ != nullptr) {
        litebus::Await(msKvClient_->GetAID());
    }
    if (msMaintenanceClient_ != nullptr) {
        litebus::Await(msMaintenanceClient_->GetAID());
    }
}

Status MetaStoreClientMgr::Init()
{
    YRLOG_DEBUG("start explorer meta-store address from {}", metaStoreConfig_.metaStoreAddress);
    auto opt = metaStoreExplorer_->Explore().Get(1000);
    if (opt.IsNone()) {
        YRLOG_ERROR("failed to explore meta-store address from {}", metaStoreConfig_.metaStoreAddress);
        return Status(StatusCode::FAILED);
    }
    metaStoreConfig_.metaStoreAddress = opt.Get();

    if (!metaStoreConfig_.enableMetaStore) {
        // etcd mode: only init etcd clients
        InitEtcdClients();
    } else {
        // meta store mode: init metastore clients, then init etcd clients if meta store is not in pass through mode
        // and excluded keys(not stored in meta store) are not empty
        InitMetaStoreClients();
        if (!metaStoreConfig_.isMetaStorePassthrough && !metaStoreConfig_.excludedKeys.empty()) {
            InitEtcdClients();
        }
    }
    return Status::OK();
}

litebus::Future<bool> MetaStoreClientMgr::IsConnected()
{
    std::list<litebus::Future<bool>> futures;
    if (!metaStoreConfig_.enableMetaStore) {
        futures.splice(futures.end(), IsEtcdConnected());
    } else {
        futures.splice(futures.end(), IsMetaStoreConnected());
        if (!metaStoreConfig_.isMetaStorePassthrough && !metaStoreConfig_.excludedKeys.empty()) {
            futures.splice(futures.end(), IsEtcdConnected());
        }
    }
    litebus::Promise<bool> promise;
    litebus::Collect<bool>(futures).OnComplete([promise](const litebus::Future<std::list<bool>> &futureList) {
        if (futureList.IsError()) {
            promise.SetValue(false);
            return;
        }
        for (auto result : futureList.Get()) {
            if (!result) {
                promise.SetValue(false);
                return;
            }
        }
        promise.SetValue(true);
    });

    return promise.GetFuture();
}

void MetaStoreClientMgr::OnHealthyStatus(const Status &status)
{
    if (!metaStoreConfig_.enableMetaStore) {
        OnEtcdHealthyStatus(status);
    } else {
        OnMetaStoreHealthyStatus(status);
        if (!metaStoreConfig_.isMetaStorePassthrough && !metaStoreConfig_.excludedKeys.empty()) {
            OnEtcdHealthyStatus(status);
        }
    }
}

std::shared_ptr<meta_store::KvClientStrategy> MetaStoreClientMgr::GetKvClient(const std::string &key)
{
    if (!metaStoreConfig_.enableMetaStore) {
        ASSERT_IF_NULL(etcdKvClient_);
        return etcdKvClient_;
    } else {
        if (!metaStoreConfig_.isMetaStorePassthrough && IsMetaStoreExcludedKey(key)) {
            ASSERT_IF_NULL(etcdKvClient_);
            return etcdKvClient_;
        } else {
            ASSERT_IF_NULL(msKvClient_);
            return msKvClient_;
        }
    }
}

std::shared_ptr<meta_store::LeaseClientStrategy> MetaStoreClientMgr::GetLeaseClient()
{
    if (!metaStoreConfig_.enableMetaStore) {
        ASSERT_IF_NULL(etcdLeaseClient_);
        return etcdLeaseClient_;
    } else {
        ASSERT_IF_NULL(msLeaseClient_);
        return msLeaseClient_;
    }
}

std::shared_ptr<meta_store::ElectionClientStrategy> MetaStoreClientMgr::GetElectionClient()
{
    if (!metaStoreConfig_.enableMetaStore) {
        ASSERT_IF_NULL(etcdElectionClient_);
        return etcdElectionClient_;
    } else {
        ASSERT_IF_NULL(msElectionClient_);
        return msElectionClient_;
    }
}

std::shared_ptr<meta_store::MaintenanceClientStrategy> MetaStoreClientMgr::GetMaintenanceClient()
{
    if (!metaStoreConfig_.enableMetaStore) {
        ASSERT_IF_NULL(etcdMaintenanceClient_);
        return etcdMaintenanceClient_;
    } else {
        ASSERT_IF_NULL(msMaintenanceClient_);
        return msMaintenanceClient_;
    }
}

std::shared_ptr<meta_store::KvClientStrategy> MetaStoreClientMgr::GetEtcdKvClient()
{
    ASSERT_IF_NULL(etcdKvClient_);
    return etcdKvClient_;
}

void MetaStoreClientMgr::InitEtcdClients()
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto address = ShuffleStringByDelimiter(metaStoreConfig_.etcdAddress, ",");
    etcdKvClient_ = std::make_shared<meta_store::EtcdKvClientStrategy>(
        "EtcdKvClientStrategy_" + uuid.ToString(), address, timeoutOption_, sslConfig_,
        metaStoreConfig_.etcdTablePrefix);
    etcdLeaseClient_ = std::make_shared<meta_store::EtcdLeaseClientStrategy>(
        "EtcdLeaseClientStrategy_" + uuid.ToString(), address, sslConfig_, timeoutOption_);
    etcdMaintenanceClient_ = std::make_shared<meta_store::EtcdMaintenanceClientStrategy>(
        "EtcdMaintenanceClientStrategy_" + uuid.ToString(), address, metaStoreExplorer_,
        timeoutOption_, sslConfig_);
    etcdElectionClient_ = std::make_shared<meta_store::EtcdElectionClientStrategy>(
        "EtcdElectionClientStrategy_" + uuid.ToString(), address, timeoutOption_, sslConfig_,
        metaStoreConfig_.etcdTablePrefix);
    (void)litebus::Spawn(etcdKvClient_);
    (void)litebus::Spawn(etcdLeaseClient_);
    (void)litebus::Spawn(etcdElectionClient_);
    (void)litebus::Spawn(etcdMaintenanceClient_);
    litebus::Async(
        etcdMaintenanceClient_->GetAID(), &meta_store::MaintenanceClientStrategy::BindReconnectedCallBack,
        [kvAid(etcdKvClient_->GetAID()), leaseAid(etcdLeaseClient_->GetAID()),
         electionAid(etcdElectionClient_->GetAID()), maintenanceAid(etcdMaintenanceClient_->GetAID())](
            const std::string &address) {
            litebus::Async(kvAid, &meta_store::KvClientStrategy::OnAddressUpdated, address);
            litebus::Async(leaseAid, &meta_store::LeaseClientStrategy::OnAddressUpdated, address);
            litebus::Async(electionAid, &meta_store::ElectionClientStrategy::OnAddressUpdated, address);
            litebus::Async(maintenanceAid, &meta_store::MaintenanceClientStrategy::OnAddressUpdated, address);
        });
}

void MetaStoreClientMgr::InitMetaStoreClients()
{
    // when meta-store is enabled and in local mode, init etcd election client because meta-store in local
    // mode only supports kv lease and maintenance interfaces
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    msKvClient_ = std::make_shared<meta_store::MetaStoreKvClientStrategy>("MsKvClientStrategy_" + uuid.ToString(),
        metaStoreConfig_.metaStoreAddress, timeoutOption_, metaStoreConfig_.etcdTablePrefix);
    msLeaseClient_ = std::make_shared<meta_store::MetaStoreLeaseClientStrategy>(
        "MsLeaseClientStrategy_" + uuid.ToString(), metaStoreConfig_.metaStoreAddress, timeoutOption_);
    msMaintenanceClient_ = std::make_shared<meta_store::MetaStoreMaintenanceClientStrategy>(
        "MsMaintenanceClientStrategy_" + uuid.ToString(), metaStoreConfig_.metaStoreAddress, metaStoreExplorer_,
        timeoutOption_);

    if (metaStoreConfig_.isMetaStorePassthrough) {
        msElectionClient_ = std::make_shared<meta_store::MetaStoreElectionClientStrategy>(
            "MsElectionClientStrategy_" + uuid.ToString(), metaStoreConfig_.metaStoreAddress, timeoutOption_,
            metaStoreConfig_.etcdTablePrefix);
    } else {
        // use etcd election client
        msElectionClient_ = std::make_shared<meta_store::EtcdElectionClientStrategy>(
            "ElectionClientStrategy_" + uuid.ToString(), metaStoreConfig_.etcdAddress, timeoutOption_, sslConfig_,
            metaStoreConfig_.etcdTablePrefix);
    }
    (void)litebus::Spawn(msKvClient_);
    (void)litebus::Spawn(msLeaseClient_);
    (void)litebus::Spawn(msMaintenanceClient_);
    (void)litebus::Spawn(msElectionClient_);
    litebus::Async(
        msMaintenanceClient_->GetAID(), &meta_store::MaintenanceClientStrategy::BindReconnectedCallBack,
        [kvAid(msKvClient_->GetAID()), leaseAid(msLeaseClient_->GetAID()), electionAid(msElectionClient_->GetAID()),
         maintenanceAid(msMaintenanceClient_->GetAID())](const std::string &address) {
            litebus::Async(kvAid, &meta_store::KvClientStrategy::OnAddressUpdated, address);
            litebus::Async(leaseAid, &meta_store::LeaseClientStrategy::OnAddressUpdated, address);
            litebus::Async(maintenanceAid, &meta_store::MaintenanceClientStrategy::OnAddressUpdated, address);
            litebus::Async(electionAid, &meta_store::ElectionClientStrategy::OnAddressUpdated, address);
        });
}

void MetaStoreClientMgr::OnEtcdHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(etcdKvClient_);
    (void)litebus::Async(etcdKvClient_->GetAID(), &meta_store::KvClientStrategy::OnHealthyStatus, status);
    ASSERT_IF_NULL(etcdElectionClient_);
    (void)litebus::Async(etcdElectionClient_->GetAID(), &meta_store::ElectionClientStrategy::OnHealthyStatus, status);
    ASSERT_IF_NULL(etcdLeaseClient_);
    (void)litebus::Async(etcdLeaseClient_->GetAID(), &meta_store::LeaseClientStrategy::OnHealthyStatus, status);
}

void MetaStoreClientMgr::OnMetaStoreHealthyStatus(const Status &status)
{
    ASSERT_IF_NULL(msKvClient_);
    (void)litebus::Async(msKvClient_->GetAID(), &meta_store::KvClientStrategy::OnHealthyStatus, status);
    ASSERT_IF_NULL(msElectionClient_);
    (void)litebus::Async(msElectionClient_->GetAID(), &meta_store::ElectionClientStrategy::OnHealthyStatus, status);
    ASSERT_IF_NULL(msLeaseClient_);
    (void)litebus::Async(msLeaseClient_->GetAID(), &meta_store::LeaseClientStrategy::OnHealthyStatus, status);
}

std::list<litebus::Future<bool>> MetaStoreClientMgr::IsEtcdConnected()
{
    std::list<litebus::Future<bool>> futures;

    ASSERT_IF_NULL(etcdMaintenanceClient_);
    futures.push_back(
        litebus::Async(etcdMaintenanceClient_->GetAID(), &meta_store::MaintenanceClientStrategy::IsConnected));
    ASSERT_IF_NULL(etcdKvClient_);
    futures.push_back(litebus::Async(etcdKvClient_->GetAID(), &meta_store::KvClientStrategy::IsConnected));
    ASSERT_IF_NULL(etcdElectionClient_);
    futures.push_back(litebus::Async(etcdElectionClient_->GetAID(),
                                     &meta_store::ElectionClientStrategy::IsConnected));
    ASSERT_IF_NULL(etcdLeaseClient_);
    futures.push_back(litebus::Async(etcdLeaseClient_->GetAID(), &meta_store::LeaseClientStrategy::IsConnected));
    return futures;
}

std::list<litebus::Future<bool>> MetaStoreClientMgr::IsMetaStoreConnected()
{
    std::list<litebus::Future<bool>> futures;

    ASSERT_IF_NULL(msMaintenanceClient_);
    futures.push_back(
        litebus::Async(msMaintenanceClient_->GetAID(), &meta_store::MaintenanceClientStrategy::IsConnected));
    ASSERT_IF_NULL(msKvClient_);
    futures.push_back(litebus::Async(msKvClient_->GetAID(), &meta_store::KvClientStrategy::IsConnected));
    ASSERT_IF_NULL(msElectionClient_);
    futures.push_back(litebus::Async(msElectionClient_->GetAID(),
                                     &meta_store::ElectionClientStrategy::IsConnected));
    ASSERT_IF_NULL(msLeaseClient_);
    futures.push_back(litebus::Async(msLeaseClient_->GetAID(), &meta_store::LeaseClientStrategy::IsConnected));
    return futures;
}

bool MetaStoreClientMgr::IsMetaStoreExcludedKey(const std::string &key)
{
    if (key.empty()) {
        return false;
    }
    for (const auto &it : metaStoreConfig_.excludedKeys) {
        if (litebus::strings::StartsWithPrefix(key, it)) {
            return true;
        }
    }
    return false;
}

void MetaStoreClientMgr::UpdateMetaStoreAddress(const std::string &address)
{
    if (metaStoreConfig_.enableMetaStore && !metaStoreConfig_.isMetaStorePassthrough) {
        ASSERT_IF_NULL(metaStoreExplorer_);
        metaStoreExplorer_->UpdateAddress(address);
        ASSERT_IF_NULL(msKvClient_);
        litebus::Async(msKvClient_->GetAID(), &meta_store::KvClientStrategy::OnAddressUpdated, address);
        ASSERT_IF_NULL(msLeaseClient_);
        litebus::Async(msLeaseClient_->GetAID(), &meta_store::LeaseClientStrategy::OnAddressUpdated, address);
        ASSERT_IF_NULL(msMaintenanceClient_);
        litebus::Async(msMaintenanceClient_->GetAID(), &meta_store::MaintenanceClientStrategy::OnAddressUpdated,
                       address);
        ASSERT_IF_NULL(msElectionClient_);
        litebus::Async(msElectionClient_->GetAID(), &meta_store::ElectionClientStrategy::OnAddressUpdated, address);
    }
}
}
