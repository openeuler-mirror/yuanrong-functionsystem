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

#include "meta_store_driver.h"

#include "meta_store_monitor/meta_store_monitor_factory.h"

namespace functionsystem::meta_store {

Status MetaStoreDriver::Start()
{
    kvServiceActor_ = std::make_shared<KvServiceActor>();
    litebus::Spawn(kvServiceActor_);

    kvServiceAccessorActor_ = std::make_shared<KvServiceAccessorActor>(kvServiceActor_->GetAID());
    litebus::Spawn(kvServiceAccessorActor_);

    leaseServiceActor_ = std::make_shared<LeaseServiceActor>(kvServiceActor_->GetAID());
    litebus::Spawn(leaseServiceActor_);
    litebus::Async(leaseServiceActor_->GetAID(), &LeaseServiceActor::Start);
    kvServiceActor_->AddLeaseServiceActor(leaseServiceActor_->GetAID());

    maintenanceServiceActor_ = std::make_shared<MaintenanceServiceActor>();
    litebus::Spawn(maintenanceServiceActor_);

    return Status::OK();
}

Status MetaStoreDriver::Start(const StartParams& params)
{
    litebus::AID backupAID;
    if (!params.backupAddress.empty()) {
        MetaStoreConfig metaStoreConfig;
        metaStoreConfig.etcdAddress = params.backupAddress;
        auto etcdClient = MetaStoreClient::Create(metaStoreConfig, params.sslConfig,
            params.timeoutOption, true, params.monitorParam);
        if (etcdClient == nullptr) {
            return Status(StatusCode::FAILED);
        }
        backupActor_ = std::make_shared<BackupActor>("BackupActor",
            etcdClient->GetEtcdKvClientStrategy()->GetAID(), params.backupOption);
        litebus::Spawn(backupActor_);
        backupAID = backupActor_->GetAID();
    }

    kvServiceActor_ = std::make_shared<KvServiceActor>(backupAID, params.isExplore);
    litebus::Spawn(kvServiceActor_);

    kvServiceAccessorActor_ = std::make_shared<KvServiceAccessorActor>(kvServiceActor_->GetAID());
    litebus::Spawn(kvServiceAccessorActor_);

    leaseServiceActor_ = std::make_shared<LeaseServiceActor>(kvServiceActor_->GetAID(), params.isExplore, backupAID);
    litebus::Spawn(leaseServiceActor_);
    litebus::Async(leaseServiceActor_->GetAID(), &LeaseServiceActor::Start);
    kvServiceActor_->AddLeaseServiceActor(leaseServiceActor_->GetAID());

    maintenanceServiceActor_ = std::make_shared<MaintenanceServiceActor>();
    litebus::Spawn(maintenanceServiceActor_);

    if (!params.backupAddress.empty()) {
        auto monitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(params.backupAddress);
        if (monitor != nullptr) {
            monitor->StartMonitor();
        } else {
            YRLOG_WARN("failed to get monitor of address {}.", params.backupAddress);
        }
    }
    return Status::OK();
}

Status MetaStoreDriver::StartPassthrough(const std::string &etcdAddress, const MetaStoreTimeoutOption &timeoutOption,
                                         const GrpcSslConfig &sslConfig, const MetaStoreMonitorParam &param)
{
    MetaStoreConfig metaStoreConfig;
    metaStoreConfig.etcdAddress = etcdAddress;
    auto etcdClient = MetaStoreClient::Create(metaStoreConfig, sslConfig, timeoutOption, true, param);
    if (etcdClient == nullptr) {
        return Status(StatusCode::FAILED);
    }
    
    kvServiceActor_ = std::make_shared<KvServicePassthroughActor>(etcdClient);
    litebus::Spawn(kvServiceActor_);

    kvServiceAccessorActor_ = std::make_shared<KvServiceAccessorActor>(kvServiceActor_->GetAID());
    litebus::Spawn(kvServiceAccessorActor_);

    leaseServiceActor_ = std::make_shared<LeaseServicePassthroughActor>(etcdClient);
    litebus::Spawn(leaseServiceActor_);
    litebus::Async(leaseServiceActor_->GetAID(), &LeaseServiceActor::Start);

    electionServiceActor_ = std::make_shared<ElectionServicePassthroughActor>(etcdClient);
    litebus::Spawn(electionServiceActor_);

    maintenanceServiceActor_ = std::make_shared<MaintenanceServicePassthroughActor>(etcdClient);
    litebus::Spawn(maintenanceServiceActor_);

    auto monitor = MetaStoreMonitorFactory::GetInstance().GetMonitor(etcdAddress);
    if (monitor != nullptr) {
        monitor->RegisterHealthyObserver(kvServiceActor_);
        monitor->RegisterHealthyObserver(leaseServiceActor_);
        monitor->RegisterHealthyObserver(electionServiceActor_);
        monitor->RegisterHealthyObserver(maintenanceServiceActor_);
    } else {
        YRLOG_WARN("failed to get monitor of address {}", etcdAddress);
    }

    return Status::OK();
}

Status MetaStoreDriver::Stop()
{
    if (kvServiceActor_ != nullptr) {
        litebus::Terminate(kvServiceActor_->GetAID());
    }
    if (kvServiceAccessorActor_ != nullptr) {
        litebus::Terminate(kvServiceAccessorActor_->GetAID());
    }
    if (leaseServiceActor_ != nullptr) {
        litebus::Terminate(leaseServiceActor_->GetAID());
    }
    if (backupActor_ != nullptr) {
        litebus::Terminate(backupActor_->GetAID());
    }
    if (persistActor_ != nullptr) {
        litebus::Terminate(persistActor_->GetAID());
    }
    if (electionServiceActor_ != nullptr) {
        litebus::Terminate(electionServiceActor_->GetAID());
    }
    if (maintenanceServiceActor_ != nullptr) {
        litebus::Terminate(maintenanceServiceActor_->GetAID());
    }
    if (httpServer_ != nullptr) {
        litebus::Terminate(httpServer_->GetAID());
    }
    return Status::OK();
};

void MetaStoreDriver::Await()
{
    if (kvServiceActor_ != nullptr) {
        litebus::Await(kvServiceActor_);
    }
    if (kvServiceAccessorActor_ != nullptr) {
        litebus::Await(kvServiceAccessorActor_);
    }
    if (leaseServiceActor_ != nullptr) {
        litebus::Await(leaseServiceActor_);
    }
    if (backupActor_ != nullptr) {
        litebus::Await(backupActor_);
    }
    if (persistActor_ != nullptr) {
        litebus::Await(persistActor_);
    }
    if (electionServiceActor_ != nullptr) {
        litebus::Await(electionServiceActor_);
    }
    if (maintenanceServiceActor_ != nullptr) {
        litebus::Await(maintenanceServiceActor_);
    }
    if (httpServer_ != nullptr) {
        litebus::Await(httpServer_);
    }
    Status::OK();
}

void MetaStoreDriver::StartHttpServer(const std::string &address)
{
    httpServer_ = std::make_shared<HttpServer>("metastore");
    auto apiRouteRegister_ = std::make_shared<MetaStoreApiRouteRegister>(address);
    if (auto registerStatus(httpServer_->RegisterRoute(apiRouteRegister_)); registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register meta-store api router failed.");
        return;
    }
    YRLOG_INFO("start meta-store http server on {}", address);
    litebus::Spawn(httpServer_);
}

MetaStoreDriver::MetaStoreApiRouteRegister::MetaStoreApiRouteRegister(const std::string &address)
{
    RegisterHandler("/explore", [address](const HttpRequest &request) -> litebus::Future<HttpResponse> {
        return litebus::http::Ok(address);
    });
}
}  // namespace functionsystem::meta_store