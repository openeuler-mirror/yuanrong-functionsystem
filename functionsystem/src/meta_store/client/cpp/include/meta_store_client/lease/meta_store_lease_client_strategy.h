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

#ifndef COMMON_META_STORE_CLIENT_LEASE_META_STORE_LEASE_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_LEASE_META_STORE_LEASE_CLIENT_STRATEGY_H

#include "meta_store_client/lease/lease_client_strategy.h"
#include "common/utils/request_sync_helper.h"

namespace functionsystem::meta_store {

class MetaStoreLeaseClientStrategy : public LeaseClientStrategy {
public:
    MetaStoreLeaseClientStrategy(const std::string &name, const std::string &address,
                                 const MetaStoreTimeoutOption &timeoutOption = {});

    ~MetaStoreLeaseClientStrategy() override = default;

    litebus::Future<LeaseGrantResponse> Grant(int ttl) override;
    litebus::Future<LeaseRevokeResponse> Revoke(int64_t leaseId) override;
    litebus::Future<LeaseKeepAliveResponse> KeepAliveOnce(int64_t leaseId) override;
    litebus::Future<bool> IsConnected() override;
    void OnAddressUpdated(const std::string &address) override;

    void RevokeCallback(const litebus::AID &from, std::string &&name, std::string &&msg);
    void KeepAliveOnceCallback(const litebus::AID &from, std::string &&name, std::string &&msg);
    void GrantCallback(const litebus::AID &from, std::string &&name, std::string &&msg);

protected:
    void Init() override;

private:
    std::shared_ptr<litebus::AID> leaseServiceAid_;

    BACK_OFF_RETRY_HELPER(MetaStoreLeaseClientStrategy, LeaseGrantResponse, grantHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreLeaseClientStrategy, LeaseRevokeResponse, revokeHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreLeaseClientStrategy, LeaseKeepAliveResponse, keepAliveOnceHelper_)
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_LEASE_META_STORE_LEASE_CLIENT_STRATEGY_H
