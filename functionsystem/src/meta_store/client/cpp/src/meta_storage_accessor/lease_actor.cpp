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
}  // namespace functionsystem