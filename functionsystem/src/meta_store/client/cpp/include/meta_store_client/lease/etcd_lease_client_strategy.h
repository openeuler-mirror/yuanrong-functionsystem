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

#ifndef COMMON_META_STORE_CLIENT_LEASE_ETCD_LEASE_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_LEASE_ETCD_LEASE_CLIENT_STRATEGY_H

#include "meta_store_client/lease/lease_client_strategy.h"
#include "common/rpc/client/grpc_client.h"

namespace functionsystem::meta_store {

class EtcdLeaseClientStrategy : public LeaseClientStrategy {
public:
    EtcdLeaseClientStrategy(const std::string &name, const std::string &address, const GrpcSslConfig &sslConfig = {},
                            const MetaStoreTimeoutOption &timeoutOption = {});

    ~EtcdLeaseClientStrategy() override = default;

    litebus::Future<LeaseGrantResponse> Grant(int ttl) override;
    litebus::Future<LeaseRevokeResponse> Revoke(int64_t leaseId) override;
    litebus::Future<LeaseKeepAliveResponse> KeepAliveOnce(int64_t leaseId) override;
    litebus::Future<bool> IsConnected() override;
    void OnAddressUpdated(const std::string &address) override;

protected:
    void Finalize() override;

private:
    std::unique_ptr<GrpcClient<etcdserverpb::Lease>> leaseClient_;
    std::unique_ptr<::grpc::ClientContext> leaseContext_;
    std::shared_ptr<
        ::grpc::ClientReaderWriter<etcdserverpb::LeaseKeepAliveRequest, etcdserverpb::LeaseKeepAliveResponse>>
        leaseStream_;
    std::atomic<bool> running_ = true;
    std::unique_ptr<std::thread> leaseStreamReadLoopThread_;
    std::unordered_map<int64_t, std::vector<litebus::Promise<LeaseKeepAliveResponse>>> keepAliveQueue_;

    void OnKeepAliveLease(const etcdserverpb::LeaseKeepAliveResponse &response);
    void LeaseStreamReadLoop(int64_t leaseId);
    bool ReconnectKeepAliveLease();
    void DoGrant(const std::shared_ptr<litebus::Promise<LeaseGrantResponse>> &promise,
                 const etcdserverpb::LeaseGrantRequest &request, int retryTimes);
    void DoRevoke(const std::shared_ptr<litebus::Promise<LeaseRevokeResponse>> &promise,
                  const etcdserverpb::LeaseRevokeRequest &request, int retryTimes);
    bool ClearLeaseKeepAliveQueue(const std::string &errMsg);
};

}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_LEASE_ETCD_LEASE_CLIENT_STRATEGY_H
