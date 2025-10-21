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

#ifndef COMMON_META_STORE_LEASE_LEASE_CLIENT_STRATEGY_H
#define COMMON_META_STORE_LEASE_LEASE_CLIENT_STRATEGY_H

#include <thread>

#include "actor/actor.hpp"
#include "meta_store_client/meta_store_struct.h"

namespace functionsystem::meta_store {
class LeaseClientStrategy : public litebus::ActorBase {
public:
    LeaseClientStrategy(const std::string &name, const std::string &address,
                        const MetaStoreTimeoutOption &timeoutOption = {});

    ~LeaseClientStrategy() override = default;

    virtual litebus::Future<LeaseGrantResponse> Grant(int ttl) = 0;
    virtual litebus::Future<LeaseRevokeResponse> Revoke(int64_t leaseId) = 0;
    virtual litebus::Future<LeaseKeepAliveResponse> KeepAliveOnce(int64_t leaseId) = 0;
    virtual litebus::Future<bool> IsConnected() = 0;

    void OnHealthyStatus(const Status &status);
    virtual void OnAddressUpdated(const std::string &address) = 0;

protected:
    std::string address_;
    MetaStoreTimeoutOption timeoutOption_;
    Status healthyStatus_ = Status::OK();
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_LEASE_LEASE_CLIENT_STRATEGY_H
