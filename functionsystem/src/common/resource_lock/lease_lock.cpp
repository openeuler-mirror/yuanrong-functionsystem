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

#include "lease_lock.h"

namespace functionsystem::resource_lock {

using V1ObjectMeta = functionsystem::kube_client::model::V1ObjectMeta;
using functionsystem::kube_client::utility::Datetime;

static std::string timePointToFormatTime(const std::chrono::time_point<std::chrono::system_clock> &time)
{
    auto tp = std::chrono::system_clock::to_time_t(time);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tp), "%Y-%m-%dT%H:%M:%S.000000Z");
    return ss.str();
}

static std::chrono::time_point<std::chrono::system_clock> formatTimeToTimePoint(const std::string &time)
{
    std::stringstream ss;
    ss << time;
    std::tm tm{};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S.000000Z");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}


std::shared_ptr<V1LeaseSpec> LeaseLock::LeaderElectionToLeaseSpec(const std::shared_ptr<LeaderElectionRecord> &record)
{
    std::shared_ptr<V1LeaseSpec> leaseSpec = std::make_shared<V1LeaseSpec>();
    if (record == nullptr) {
        return leaseSpec;
    }
    leaseSpec->SetHolderIdentity(record->holderIdentity);
    leaseSpec->SetLeaseTransitions(record->leaseTransitions);
    leaseSpec->SetLeaseDurationSeconds(record->leaseDurationSeconds);
    leaseSpec->SetAcquireTime(Datetime::FromString(timePointToFormatTime(record->acquireTime)));
    leaseSpec->SetRenewTime(Datetime::FromString(timePointToFormatTime(record->renewTime)));
    return leaseSpec;
}

std::shared_ptr<LeaderElectionRecord> LeaseLock::LeaseSpecToLeaderElection(const std::shared_ptr<V1LeaseSpec> &spec)
{
    std::shared_ptr<LeaderElectionRecord> record = std::make_shared<LeaderElectionRecord>();
    if (spec == nullptr) {
        return record;
    }
    record->holderIdentity = spec->GetHolderIdentity();
    record->leaseDurationSeconds = spec->GetLeaseDurationSeconds();
    record->leaseTransitions = spec->GetLeaseTransitions();
    record->acquireTime = formatTimeToTimePoint(spec->GetAcquireTime().ToString());
    record->renewTime = formatTimeToTimePoint(spec->GetRenewTime().ToString());
    return record;
}

litebus::Future<std::shared_ptr<V1Lease>> LeaseLock::Get()
{
    ASSERT_IF_NULL(kubeClient_);
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    kubeClient_->ReadNamespacedLease(leaseName_, k8sNamespace_)
        .OnComplete([promise](const litebus::Future<std::shared_ptr<V1Lease>> &result) {
            if (result.IsError()) {
                YRLOG_WARN("failed to get lease, code({})", result.GetErrorCode());
                promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                return;
            }
            promise->SetValue(result.Get());
        });
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<V1Lease>> LeaseLock::Create(const std::shared_ptr<LeaderElectionRecord> &record)
{
    ASSERT_IF_NULL(kubeClient_);
    std::shared_ptr<V1Lease> v1Lease = std::make_shared<V1Lease>();
    std::shared_ptr<V1ObjectMeta> metaData = std::make_shared<V1ObjectMeta>();
    metaData->SetName(leaseName_);
    metaData->SetRNamespace(k8sNamespace_);
    v1Lease->SetMetadata(metaData);
    v1Lease->SetSpec(LeaderElectionToLeaseSpec(record));
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    kubeClient_->CreateNamespacedLease(k8sNamespace_, v1Lease)
        .OnComplete([promise](const litebus::Future<std::shared_ptr<V1Lease>> &result) {
            if (result.IsError()) {
                YRLOG_WARN("failed to create lease, code({})", result.GetErrorCode());
                promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                return;
            }
            YRLOG_INFO("success to create lease");
            promise->SetValue(result.Get());
        });
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<V1Lease>> LeaseLock::Update(const std::shared_ptr<LeaderElectionRecord> &record)
{
    ASSERT_IF_NULL(kubeClient_);
    if (lease_ == nullptr) {
        YRLOG_ERROR("lease not initialized, call get or create first");
        return nullptr;
    }
    lease_->SetSpec(LeaderElectionToLeaseSpec(record));
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
    return kubeClient_->ReplaceNamespacedLease(leaseName_, k8sNamespace_, lease_);
}

void LeaseLock::SetLease(const std::shared_ptr<V1Lease> &lease)
{
    lease_ = lease;
}

bool LeaseLock::IsHold(const std::shared_ptr<LeaderElectionRecord> &record)
{
    if (record == nullptr) {
        return false;
    }
    return record->holderIdentity == identity_;
}
}
