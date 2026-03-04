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

#include "lease_actor.h"

#include "async/defer.hpp"
#include "common/metrics/metrics_adapter.h"
#include "meta_store_client/txn_transaction.h"

namespace functionsystem {
const int MSECOND = 1000;
const int DEFAULT_LEASE_INTERVAL = 10000;
const int DEFAULT_LEASE_TIME = 6;

LeaseActor::LeaseActor(const std::string &name, const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : litebus::ActorBase(name), metaClient_(metaStoreClient)
{
}

litebus::Future<Status> LeaseActor::PutWithLease(const std::string &key, const std::string &value, const int ttl)
{
    YRLOG_DEBUG("put into meta store with lease, key: {}, ttl: {}", key, ttl);
    if (ttl < 0) {
        YRLOG_ERROR("failed to put key: {}, ttl is less than zero", key);
        return Status(StatusCode::PARAMETER_ERROR, "ttl is less than zero");
    }

    return CheckLeaseIDExist(key, value, ttl)
        .Then(litebus::Defer(GetAID(), &LeaseActor::Put, std::placeholders::_1, key, value, ttl));
}

litebus::Future<Status> LeaseActor::Put(const Status &status, const std::string &key, const std::string &value,
                                        const int ttl)
{
    if (status.IsError()) {
        YRLOG_WARN("failed to get lease id, key:{}", key);
        auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
        leaseTimerMap_[key] = litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, GetAID(),
                                                  &LeaseActor::RetryPutWithLease, key, value, ttl);
        return status;
    }
    auto leaseID = leaseIDMap_[key];
    auto promise = litebus::Promise<Status>();
    ASSERT_IF_NULL(metaClient_);
    (void)metaClient_->Put(key, value, { leaseID, false })
        .OnComplete(
        litebus::Defer(GetAID(), &LeaseActor::OnPutResponse, std::placeholders::_1, key, value, ttl, promise));
    return promise.GetFuture();
}

void LeaseActor::OnPutResponse(const litebus::Future<std::shared_ptr<PutResponse>> &response, const std::string &key,
                               const std::string &value, int ttl, const litebus::Promise<Status> &promise)
{
    auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
    auto aid = GetAID();
    if (response.IsOK() && response.Get()->status.IsOk()) {
        (void)litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, aid, &LeaseActor::KeepAliveOnce, key,
                                  value, ttl);
        promise.SetValue(Status::OK());
        return;
    }
    if (response.IsError()) {
        YRLOG_ERROR("failed to put key {} with lease using meta client, error: {}", key, response.GetErrorCode());
    } else {
        YRLOG_ERROR("failed to put key {} with lease using meta client, error: {}", key,
                    fmt::underlying(response.Get()->status.StatusCode()));
    }

    promise.SetValue(Status(StatusCode::BP_META_STORAGE_PUT_ERROR, "key: " + key));
    (void)litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, aid, &LeaseActor::RetryPutWithLease, key,
                              value, ttl);
}

litebus::Future<Status> LeaseActor::CheckLeaseIDExist(const std::string &key, const std::string &value, const int ttl)
{
    // Check whether leaseID exists. If key is changed, granting a new lease ID.
    if (auto iter = leaseIDMap_.find(key); iter == leaseIDMap_.end()) {
        if (auto timer = leaseTimerMap_.find(key); timer != leaseTimerMap_.end()) {
            (void)litebus::TimerTools::Cancel(timer->second);
        }
        ASSERT_IF_NULL(metaClient_);
        return metaClient_->Grant(int(ttl / MSECOND))
            .Then(litebus::Defer(GetAID(), &LeaseActor::GrantResponse, std::placeholders::_1, key));
    }
    return Status::OK();
}

litebus::Future<Status> LeaseActor::GrantResponse(const LeaseGrantResponse &rsp, const std::string &key)
{
    if (rsp.status.IsError()) {
        YRLOG_ERROR("failed to grant key {} using meta client, error: {}", key,
                    fmt::underlying(rsp.status.StatusCode()));
        return Status(StatusCode::BP_META_STORAGE_GRANT_ERROR, "key: " + key);
    }
    int64_t leaseID = rsp.leaseId;
    YRLOG_INFO("grant a lease ID {} from meta store", leaseID);
    (void)leaseIDMap_.emplace(key, leaseID);
    return Status::OK();
}

litebus::Future<Status> LeaseActor::Revoke(const std::string &key)
{
    YRLOG_DEBUG("revoke from meta store, key: {} ", key);
    auto iter = leaseIDMap_.find(key);
    if (iter == leaseIDMap_.end()) {
        YRLOG_ERROR("failed to revoke key {}, lease not found", key);
        return Status(StatusCode::BP_LEASE_ID_NOT_FOUND, "key: " + key);
    }

    (void)litebus::TimerTools::Cancel(leaseTimerMap_[key]);
    (void)leaseTimerMap_.erase(key);
    ASSERT_IF_NULL(metaClient_);
    return metaClient_->Revoke(iter->second)
        .Then(litebus::Defer(GetAID(), &LeaseActor::RevokeResponse, std::placeholders::_1, key));
}

void LeaseActor::KeepAliveOnce(const std::string &key, const std::string &value, const int ttl)
{
    auto timeout = uint32_t(ttl / (DEFAULT_LEASE_TIME * 2));
    ASSERT_IF_NULL(metaClient_);
    (void)metaClient_->KeepAliveOnce(leaseIDMap_[key])
        .After(timeout,
               [](const litebus::Future<LeaseKeepAliveResponse> &future) -> litebus::Future<LeaseKeepAliveResponse> {
                   LeaseKeepAliveResponse response;
                   response.ttl = 0;
                   return response;
               })
        .OnComplete(
        litebus::Defer(GetAID(), &LeaseActor::KeepAliveOnceResponse, std::placeholders::_1, key, value, ttl));
}

void LeaseActor::KeepAliveOnceResponse(const litebus::Future<LeaseKeepAliveResponse> &rsp, const std::string &key,
                                       const std::string &value, const int ttl)
{
    if (rsp.IsOK() && rsp.Get().ttl != 0) {
        YRLOG_DEBUG("keep lease {} once success", leaseIDMap_[key]);
        auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
        leaseTimerMap_[key] = litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, GetAID(),
                                                  &LeaseActor::KeepAliveOnce, key, value, ttl);
        return;
    }
    YRLOG_WARN("lease {} keep alive failed, try to re-put", leaseIDMap_[key]);
    RetryPutWithLease(key, value, ttl);
}

void LeaseActor::RetryPutWithLease(const std::string &key, const std::string &value, const int ttl)
{
    YRLOG_WARN("try to re-put with lease, key:{}", key);
    if (auto iter = leaseTimerMap_.find(key); iter != leaseTimerMap_.end()) {
        (void)litebus::TimerTools::Cancel(leaseTimerMap_[key]);
        (void)leaseTimerMap_.erase(key);
    }
    if (auto iter = leaseIDMap_.find(key); iter != leaseIDMap_.end()) {
        (void)leaseIDMap_.erase(key);
    }
    (void)litebus::Async(GetAID(), &LeaseActor::PutWithLease, key, value, ttl);
}

litebus::Future<Status> LeaseActor::RevokeResponse(const litebus::Future<LeaseRevokeResponse> &rsp,
                                                   const std::string &key)
{
    if (rsp.IsError()) {
        YRLOG_ERROR("failed to revoke key {} using meta client, error: {}", key, rsp.GetErrorCode());
        return Status(StatusCode::BP_META_STORAGE_REVOKE_ERROR, "key: " + key);
    }
    (void)leaseIDMap_.erase(key);
    return Status::OK();
}

// ============================================================================
// TxnWithLease: Transactional put multiple key-values with a shared lease
// ============================================================================

litebus::Future<Status> LeaseActor::TxnWithLease(
    const std::string& groupKey,
    const std::vector<std::pair<std::string, std::string>>& kvs,
    const int ttl)
{
    YRLOG_INFO("TxnWithLease: groupKey={}, keys={}, ttl={}", groupKey, kvs.size(), ttl);
    if (ttl < 0) {
        YRLOG_ERROR("failed to txn with lease, groupKey: {}, ttl is less than zero", groupKey);
        return Status(StatusCode::PARAMETER_ERROR, "ttl is less than zero");
    }

    if (kvs.empty()) {
        YRLOG_WARN("TxnWithLease: groupKey={}, no keys to write", groupKey);
        return Status::OK();
    }

    // Save kvs and ttl for retry
    groupKVsMap_[groupKey] = kvs;
    groupTTLMap_[groupKey] = ttl;

    // Check whether leaseID exists for this group
    if (auto iter = groupLeaseIDMap_.find(groupKey); iter == groupLeaseIDMap_.end()) {
        // Grant a new lease
        ASSERT_IF_NULL(metaClient_);
        return metaClient_->Grant(int(ttl / MSECOND))
            .Then(litebus::Defer(GetAID(), [this, groupKey](const LeaseGrantResponse& rsp) -> Status {
                if (rsp.status.IsError()) {
                    YRLOG_ERROR("failed to grant lease for group {}: {}", groupKey, rsp.status.GetMessage());
                    return Status(StatusCode::BP_META_STORAGE_GRANT_ERROR, "groupKey: " + groupKey);
                }

                int64_t leaseID = rsp.leaseId;
                YRLOG_INFO("grant a lease ID {} for group {}", leaseID, groupKey);
                groupLeaseIDMap_[groupKey] = leaseID;
                return Status::OK();
            }))
            .Then(litebus::Defer(GetAID(), &LeaseActor::DoTxnWithLease, std::placeholders::_1, groupKey));
    }

    // Already have leaseID, directly do txn
    return DoTxnWithLease(Status::OK(), groupKey);
}

litebus::Future<Status> LeaseActor::DoTxnWithLease(const Status& status, const std::string& groupKey)
{
    if (status.IsError()) {
        YRLOG_WARN("failed to get lease id for group {}, retry later", groupKey);
        auto ttl = groupTTLMap_[groupKey];
        auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
        groupLeaseTimerMap_[groupKey] = litebus::AsyncAfter(
            interval ? interval : DEFAULT_LEASE_INTERVAL, GetAID(),
            &LeaseActor::RetryTxnWithLease, groupKey, groupKVsMap_[groupKey], ttl);
        return status;
    }

    auto leaseIDIter = groupLeaseIDMap_.find(groupKey);
    if (leaseIDIter == groupLeaseIDMap_.end()) {
        YRLOG_ERROR("leaseID not found for group {}", groupKey);
        return Status(StatusCode::BP_LEASE_ID_NOT_FOUND, "groupKey: " + groupKey);
    }

    int64_t leaseID = leaseIDIter->second;
    const auto& kvs = groupKVsMap_[groupKey];

    ASSERT_IF_NULL(metaClient_);
    auto txn = metaClient_->BeginTransaction();
    PutOption opt{.leaseId = leaseID};

    for (const auto& [key, value] : kvs) {
        txn->Then(meta_store::TxnOperation::Create(key, value, opt));
    }

    auto promise = litebus::Promise<Status>();
    (void)txn->Commit()
        .OnComplete(
            litebus::Defer(GetAID(), &LeaseActor::OnTxnResponse, std::placeholders::_1, groupKey, promise));
    return promise.GetFuture();
}

void LeaseActor::OnTxnResponse(
    const litebus::Future<std::shared_ptr<TxnResponse>>& response,
    const std::string& groupKey,
    const litebus::Promise<Status>& promise)
{
    auto ttl = groupTTLMap_[groupKey];
    auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
    auto aid = GetAID();

    if (response.IsOK() && response.Get()->status.IsOk()) {
        // Success: start KeepAlive
        int64_t leaseID = groupLeaseIDMap_[groupKey];
        (void)litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, aid,
            &LeaseActor::KeepAliveGroupOnce, groupKey, leaseID, ttl);
        promise.SetValue(Status::OK());
        YRLOG_INFO("TxnWithLease success: groupKey={}, leaseID={}, keys={}",
                   groupKey, leaseID, groupKVsMap_[groupKey].size());
        return;
    }

    if (response.IsError()) {
        YRLOG_ERROR("Txn commit failed for group {}, error: {}", groupKey, response.GetErrorCode());
    } else {
        YRLOG_ERROR("Txn commit failed for group {}, status: {}",
                   groupKey, fmt::underlying(response.Get()->status.StatusCode()));
    }

    promise.SetValue(Status(StatusCode::BP_META_STORAGE_PUT_ERROR, "groupKey: " + groupKey));

    // Retry
    (void)litebus::AsyncAfter(interval ? interval : DEFAULT_LEASE_INTERVAL, aid,
        &LeaseActor::RetryTxnWithLease, groupKey, groupKVsMap_[groupKey], ttl);
}

litebus::Future<Status> LeaseActor::RevokeGroup(const std::string& groupKey)
{
    YRLOG_INFO("RevokeGroup: groupKey={}", groupKey);

    auto iter = groupLeaseIDMap_.find(groupKey);
    if (iter == groupLeaseIDMap_.end()) {
        YRLOG_WARN("Group {} not found in lease map", groupKey);
        return Status::OK();
    }

    // Cancel timer
    auto timerIter = groupLeaseTimerMap_.find(groupKey);
    if (timerIter != groupLeaseTimerMap_.end()) {
        (void)litebus::TimerTools::Cancel(timerIter->second);
        groupLeaseTimerMap_.erase(timerIter);
    }

    int64_t leaseID = iter->second;
    groupLeaseIDMap_.erase(iter);

    // Clean up maps
    groupKVsMap_.erase(groupKey);
    groupTTLMap_.erase(groupKey);

    ASSERT_IF_NULL(metaClient_);
    return metaClient_->Revoke(leaseID)
        .Then(litebus::Defer(GetAID(), [groupKey](const LeaseRevokeResponse& rsp) -> Status {
            if (rsp.status.IsError()) {
                YRLOG_ERROR("failed to revoke lease for group {}: {}", groupKey, rsp.status.GetMessage());
                return Status(StatusCode::BP_META_STORAGE_REVOKE_ERROR, "groupKey: " + groupKey);
            }
            YRLOG_INFO("Successfully revoked group {}", groupKey);
            return Status::OK();
        }));
}

void LeaseActor::KeepAliveGroupOnce(const std::string& groupKey, int64_t leaseID, const int ttl)
{
    auto timeout = uint32_t(ttl / (DEFAULT_LEASE_TIME * 2));
    ASSERT_IF_NULL(metaClient_);
    (void)metaClient_->KeepAliveOnce(leaseID)
        .After(timeout,
               [](const litebus::Future<LeaseKeepAliveResponse>& future) -> litebus::Future<LeaseKeepAliveResponse> {
                   LeaseKeepAliveResponse response;
                   response.ttl = 0;
                   return response;
               })
        .OnComplete(
            litebus::Defer(GetAID(), &LeaseActor::KeepAliveGroupOnceResponse, std::placeholders::_1,
                          groupKey, leaseID, ttl));
}

void LeaseActor::KeepAliveGroupOnceResponse(
    const litebus::Future<LeaseKeepAliveResponse>& rsp,
    const std::string& groupKey, int64_t leaseID, const int ttl)
{
    if (rsp.IsOK() && rsp.Get().ttl != 0) {
        YRLOG_DEBUG("keep lease {} once success for group {}", leaseID, groupKey);
        auto interval = uint32_t(ttl / DEFAULT_LEASE_TIME);
        groupLeaseTimerMap_[groupKey] = litebus::AsyncAfter(
            interval ? interval : DEFAULT_LEASE_INTERVAL, GetAID(),
            &LeaseActor::KeepAliveGroupOnce, groupKey, leaseID, ttl);
        return;
    }
    YRLOG_WARN("lease {} keep alive failed for group {}, try to re-txn", leaseID, groupKey);
    RetryTxnWithLease(groupKey, groupKVsMap_[groupKey], ttl);
}

void LeaseActor::RetryTxnWithLease(
    const std::string& groupKey,
    const std::vector<std::pair<std::string, std::string>>& kvs,
    const int ttl)
{
    YRLOG_WARN("try to re-txn with lease, groupKey:{}", groupKey);

    // Cancel timer
    auto timerIter = groupLeaseTimerMap_.find(groupKey);
    if (timerIter != groupLeaseTimerMap_.end()) {
        (void)litebus::TimerTools::Cancel(timerIter->second);
        groupLeaseTimerMap_.erase(timerIter);
    }

    // Clean up leaseID and retry
    groupLeaseIDMap_.erase(groupKey);

    // Save kvs and ttl again
    groupKVsMap_[groupKey] = kvs;
    groupTTLMap_[groupKey] = ttl;

    (void)litebus::Async(GetAID(), &LeaseActor::TxnWithLease, groupKey, kvs, ttl);
}

}  // namespace functionsystem