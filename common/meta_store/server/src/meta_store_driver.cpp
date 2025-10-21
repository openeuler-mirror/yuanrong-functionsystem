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

Status MetaStoreDriver::Start(const std::string &backupAddress, const MetaStoreTimeoutOption &timeoutOption,
                              const GrpcSslConfig &sslConfig, const MetaStoreBackupOption &backupOption)
{
    litebus::AID backupAID;
    if (!backupAddress.empty()) {
        persistActor_ = std::make_shared<EtcdKvClientStrategy>("Persist", backupAddress, timeoutOption, sslConfig);
        litebus::Spawn(persistActor_);
        backupActor_ = std::make_shared<BackupActor>("BackupActor", persistActor_->GetAID(), backupOption);
        litebus::Spawn(backupActor_);
        backupAID = backupActor_->GetAID();
    }

    kvServiceActor_ = std::make_shared<KvServiceActor>(backupAID);
    litebus::Spawn(kvServiceActor_);

    kvServiceAccessorActor_ = std::make_shared<KvServiceAccessorActor>(kvServiceActor_->GetAID());
    litebus::Spawn(kvServiceAccessorActor_);

    leaseServiceActor_ = std::make_shared<LeaseServiceActor>(kvServiceActor_->GetAID(), backupAID);
    litebus::Spawn(leaseServiceActor_);
    litebus::Async(leaseServiceActor_->GetAID(), &LeaseServiceActor::Start);
    kvServiceActor_->AddLeaseServiceActor(leaseServiceActor_->GetAID());

    maintenanceServiceActor_ = std::make_shared<MaintenanceServiceActor>();
    litebus::Spawn(maintenanceServiceActor_);

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
    if (maintenanceServiceActor_ != nullptr) {
        litebus::Terminate(maintenanceServiceActor_->GetAID());
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
    if (maintenanceServiceActor_ != nullptr) {
        litebus::Await(maintenanceServiceActor_);
    }
    Status::OK();
}
}  // namespace functionsystem::meta_store