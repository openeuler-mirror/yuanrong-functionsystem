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

#ifndef COMMON_META_STORAGE_ACCESSOR_LEASE_ACTOR_H
#define COMMON_META_STORAGE_ACCESSOR_LEASE_ACTOR_H

#include "actor/actor.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "meta_store_client/meta_store_client.h"
#include "common/status/status.h"
#include <utility>
#include <vector>

namespace functionsystem {

class LeaseActor : public litebus::ActorBase {
public:
    explicit LeaseActor(const std::string &name, const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    ~LeaseActor() override = default;

    /**
     * Put a key-value with TTL asynchronous. The key-value will be deleted if the meta storage doesn't receive
     * keepalive message within the TTL.
     * @param key the key of BusProxy.
     * @param value the value to update.
     * @param ttl time to live value, millisecond.
     * @return
     */
    litebus::Future<Status> PutWithLease(const std::string &key, const std::string &value, const int ttl);

    /**
     * Revoke the lease ID according the BusProxy key.
     * @param key the key of BusProxy.
     * @return
     */
    litebus::Future<Status> Revoke(const std::string &key);

    /**
     * Transactional put multiple key-values with a shared lease.
     * All keys will be written atomically in a single transaction and share one lease.
     * @param groupKey Identifier for this group of keys (e.g., instanceID), used for revoke.
     * @param kvs Vector of key-value pairs to write.
     * @param ttl Time to live in milliseconds.
     * @return
     */
    litebus::Future<Status> TxnWithLease(
        const std::string& groupKey,
        const std::vector<std::pair<std::string, std::string>>& kvs,
        const int ttl);

    /**
     * Revoke the lease for a group of keys.
     * @param groupKey The group key used in TxnWithLease.
     * @return
     */
    litebus::Future<Status> RevokeGroup(const std::string& groupKey);

protected:
    void KeepAliveOnce(const std::string &key, const std::string &value, const int ttl);

    void KeepAliveOnceResponse(const litebus::Future<LeaseKeepAliveResponse> &rsp, const std::string &key,
                               const std::string &value, const int ttl);

    litebus::Future<Status> RevokeResponse(const litebus::Future<LeaseRevokeResponse> &rsp, const std::string &key);

    litebus::Future<Status> CheckLeaseIDExist(const std::string &key, const std::string &value, const int ttl);

    litebus::Future<Status> GrantResponse(const LeaseGrantResponse &rsp, const std::string &key);

    litebus::Future<Status> Put(const Status &status, const std::string &key, const std::string &value, const int ttl);

    void RetryPutWithLease(const std::string &key, const std::string &value, const int ttl);

    // Group lease methods (for TxnWithLease)
    void KeepAliveGroupOnce(const std::string& groupKey, int64_t leaseID, const int ttl);

    void KeepAliveGroupOnceResponse(const litebus::Future<LeaseKeepAliveResponse>& rsp,
                                    const std::string& groupKey, int64_t leaseID, const int ttl);

    void RetryTxnWithLease(
        const std::string& groupKey,
        const std::vector<std::pair<std::string, std::string>>& kvs,
        const int ttl);

    litebus::Future<Status> DoTxnWithLease(const Status& status, const std::string& groupKey);

    void OnTxnResponse(
        const litebus::Future<std::shared_ptr<TxnResponse>>& response,
        const std::string& groupKey,
        const litebus::Promise<Status>& promise);

private:
    void OnPutResponse(const litebus::Future<std::shared_ptr<PutResponse>> &response, const std::string &key,
                       const std::string &value, int ttl, const litebus::Promise<Status> &promise);

    std::shared_ptr<MetaStoreClient> metaClient_;

    // The map of key and lease ID (for single-key PutWithLease).
    std::unordered_map<std::string, int64_t> leaseIDMap_;

    std::unordered_map<std::string, litebus::Timer> leaseTimerMap_;

    // Group lease maps (for TxnWithLease).
    // groupKey (e.g., instanceID) → shared leaseID for multiple keys.
    std::unordered_map<std::string, int64_t> groupLeaseIDMap_;

    std::unordered_map<std::string, litebus::Timer> groupLeaseTimerMap_;

    // Store original KVs for retry (groupKey → kvs).
    std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> groupKVsMap_;

    // Store TTL for retry (groupKey → ttl).
    std::unordered_map<std::string, int> groupTTLMap_;
};

}  // namespace functionsystem

#endif  // COMMON_META_STORAGE_ACCESSOR_LEASE_ACTOR_H
