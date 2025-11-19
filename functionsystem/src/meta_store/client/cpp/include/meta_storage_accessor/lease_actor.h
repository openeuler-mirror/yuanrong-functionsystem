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
#include "meta_store_client/meta_store_client.h"
#include "common/status/status.h"

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

protected:
    void KeepAliveOnce(const std::string &key, const std::string &value, const int ttl);

    void KeepAliveOnceResponse(const litebus::Future<LeaseKeepAliveResponse> &rsp, const std::string &key,
                               const std::string &value, const int ttl);

    litebus::Future<Status> RevokeResponse(const litebus::Future<LeaseRevokeResponse> &rsp, const std::string &key);

    litebus::Future<Status> CheckLeaseIDExist(const std::string &key, const std::string &value, const int ttl);

    litebus::Future<Status> GrantResponse(const LeaseGrantResponse &rsp, const std::string &key);

    litebus::Future<Status> Put(const Status &status, const std::string &key, const std::string &value, const int ttl);

    void RetryPutWithLease(const std::string &key, const std::string &value, const int ttl);

private:
    void OnPutResponse(const litebus::Future<std::shared_ptr<PutResponse>> &response, const std::string &key,
                       const std::string &value, int ttl, const litebus::Promise<Status> &promise);

    std::shared_ptr<MetaStoreClient> metaClient_;

    // The map of key and lease ID.
    std::unordered_map<std::string, int64_t> leaseIDMap_;

    std::unordered_map<std::string, litebus::Timer> leaseTimerMap_;
};

}  // namespace functionsystem

#endif  // COMMON_META_STORAGE_ACCESSOR_LEASE_ACTOR_H
