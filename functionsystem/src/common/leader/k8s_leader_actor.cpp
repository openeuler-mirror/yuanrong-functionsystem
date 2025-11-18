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


#include "k8s_leader_actor.h"

#include <async/asyncafter.hpp>
#include <async/defer.hpp>


namespace functionsystem::leader {

constexpr int64_t MILLISECONDS_PRE_SECOND = 1000;
const uint32_t MIN_RENEW_LEASE_INTERNAL = 5;

void K8sLeaderActor::Init()
{
    // reduce the error logs for calling k8s api
    if (electRenewInterval_ < MIN_RENEW_LEASE_INTERNAL) {
        electRenewInterval_ = MIN_RENEW_LEASE_INTERNAL;
    }
}

void K8sLeaderActor::Finalize()
{
    YRLOG_INFO("start to finalize k8s leader actor");
    litebus::TimerTools::Cancel(timer_);
    Release();
}

void K8sLeaderActor::Elect()
{
    if (isCampaigning_ != nullptr) {
        YRLOG_WARN("an election already started, wait this process finished");
        return;
    }
    isCampaigning_ = std::make_shared<litebus::Promise<bool>>();
    TryAcquireOrNew().OnComplete(litebus::Defer(GetAID(), &K8sLeaderActor::CheckElectResult, std::placeholders::_1));
}

void K8sLeaderActor::CheckElectResult(const litebus::Future<Status> &result)
{
    RETURN_IF_NULL(isCampaigning_);
    isCampaigning_->SetValue(true);
    isCampaigning_ = nullptr;
    if (IsLeader()) {
        timer_ = litebus::AsyncAfter(electRenewInterval_ * litebus::SECTOMILLI, GetAID(), &K8sLeaderActor::Elect);
    } else {
        timer_ = litebus::AsyncAfter(keepAliveInterval_ * litebus::SECTOMILLI, GetAID(), &K8sLeaderActor::Elect);
    }
}

litebus::Future<Status> K8sLeaderActor::TryAcquireOrNew()
{
    auto record = std::make_shared<resource_lock::LeaderElectionRecord>();
    record->holderIdentity = proposal_;
    record->acquireTime = std::chrono::system_clock::now();
    record->renewTime = std::chrono::system_clock::now();
    record->leaseDurationSeconds = leaseTTL_;
    auto promise = std::make_shared<litebus::Promise<Status>>();
    if (IsLeader() && IsLeaderValid()) {
        record->acquireTime = observedRecord_->acquireTime;
        record->leaseTransitions = observedRecord_->leaseTransitions;
        resourceLock_->Update(record).OnComplete(
            [aid(GetAID()), promise, record](const litebus::Future<std::shared_ptr<V1Lease>> &lease) {
                if (lease.IsOK()) {
                    litebus::Async(aid, &K8sLeaderActor::SetObservedRecord, lease.Get(), promise);
                    return;
                }
                YRLOG_WARN("failed to update lock optimistically, err is {}, falling back to slow path",
                           lease.GetErrorCode());
                litebus::Async(aid, &K8sLeaderActor::DoTryAcquireOrNew, record, promise);
            });
    } else {
        litebus::Async(GetAID(), &K8sLeaderActor::DoTryAcquireOrNew, record, promise);
    }
    return promise->GetFuture();
}

void K8sLeaderActor::DoTryAcquireOrNew(const std::shared_ptr<resource_lock::LeaderElectionRecord> &record,
                                       const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    (void)resourceLock_->Get().OnComplete(
        litebus::Defer(GetAID(), &K8sLeaderActor::CheckNeedCreateLock, std::placeholders::_1, record, promise));
}

void K8sLeaderActor::CheckNeedCreateLock(const litebus::Future<std::shared_ptr<V1Lease>> future,
                                         const std::shared_ptr<resource_lock::LeaderElectionRecord> &record,
                                         const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    if (future.IsError() || future.Get() == nullptr) {
        YRLOG_INFO("try to create lock");
        resourceLock_->Create(record).OnComplete(
            [aid(GetAID()), promise, record](const litebus::Future<std::shared_ptr<V1Lease>> &lease) {
                if (lease.IsError() || lease.Get() == nullptr) {
                    promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                    return;
                }
                litebus::Async(aid, &K8sLeaderActor::SetObservedRecord, lease.Get(), promise);
            });
        return;
    }
    auto oldLease = future.Get();
    SetObservedRecord(oldLease, std::make_shared<litebus::Promise<Status>>());
    if (IsLeaderValid() && !IsLeader()) {
        YRLOG_DEBUG("lock is held by {} and has not yet expired", GetLeader());
        promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
        return;
    }
    if (IsLeader()) {
        record->acquireTime = observedRecord_->acquireTime;
        record->leaseTransitions = observedRecord_->leaseTransitions;
    } else {
        record->leaseTransitions = observedRecord_->leaseTransitions + 1;
    }
    // update the lock itself
    resourceLock_->Update(record).OnComplete(
        [aid(GetAID()), promise, record](const litebus::Future<std::shared_ptr<V1Lease>> &lease) {
            if (lease.IsError() || lease.Get() == nullptr) {
                promise->SetFailed(static_cast<int32_t>(StatusCode::FAILED));
                return;
            }
            litebus::Async(aid, &K8sLeaderActor::SetObservedRecord, lease.Get(), promise);
        });
}

void K8sLeaderActor::Release()
{
    if (!IsLeader()) {
        return;
    }
    auto record = std::make_shared<resource_lock::LeaderElectionRecord>();
    record->holderIdentity = "";
    record->acquireTime = std::chrono::system_clock::now();
    record->renewTime = std::chrono::system_clock::now();
    record->leaseDurationSeconds = 1;
    record->leaseTransitions = observedRecord_->leaseTransitions;
    resourceLock_->Update(record);
}

void K8sLeaderActor::SetObservedRecord(const std::shared_ptr<V1Lease> &lease,
                                       const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    resourceLock_->SetLease(lease);
    if (lease != nullptr && lease->SpecIsSet()) {
        observedRecord_ = resourceLock_->LeaseSpecToLeaderElection(lease->GetSpec());
        observedTime_ =
            std::chrono::duration_cast<std::chrono::milliseconds>(observedRecord_->renewTime.time_since_epoch())
                .count();
        cachedLeaderInfo_.address = observedRecord_->holderIdentity;
    }
    promise->SetValue(Status::OK());
}

bool K8sLeaderActor::IsLeader()
{
    if (observedRecord_ == nullptr) {
        return false;
    }
    return observedRecord_->holderIdentity == proposal_;
}

std::string K8sLeaderActor::GetLeader()
{
    if (observedRecord_ == nullptr) {
        return "";
    }
    return observedRecord_->holderIdentity;
}

bool K8sLeaderActor::IsLeaderValid()
{
    if (observedRecord_ == nullptr || observedRecord_->holderIdentity.empty()) {
        return false;
    }
    int64_t milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    return (milliseconds - observedTime_) < leaseTTL_ * MILLISECONDS_PRE_SECOND;
}
}
