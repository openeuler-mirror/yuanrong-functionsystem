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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_ETCD_ELECTION_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_ELECTION_ETCD_ELECTION_CLIENT_STRATEGY_H

#include "common/rpc/client/grpc_client.h"
#include "election_client_strategy.h"

namespace functionsystem::meta_store {

class EtcdElectionClientStrategy : public ElectionClientStrategy {
public:
    EtcdElectionClientStrategy(const std::string &name, const std::string &address,
                               const MetaStoreTimeoutOption &timeoutOption, const GrpcSslConfig &sslConfig = {},
                               const std::string &etcdTablePrefix = "");

    ~EtcdElectionClientStrategy() override = default;

    litebus::Future<CampaignResponse> Campaign(const std::string &name, int64_t lease,
                                               const std::string &value) override;
    litebus::Future<LeaderResponse> Leader(const std::string &name) override;
    litebus::Future<ResignResponse> Resign(const LeaderKey &leader) override;
    litebus::Future<std::shared_ptr<Observer>> Observe(const std::string &name,
                                                       const std::function<void(LeaderResponse)> &callback) override;
    litebus::Future<bool> IsConnected() override;
    void OnAddressUpdated(const std::string &address) override;

private:
    void DoCampaign(const std::shared_ptr<litebus::Promise<CampaignResponse>> &promise,
                    const v3electionpb::CampaignRequest &request, int retryTimes);

    void DoLeader(const std::shared_ptr<litebus::Promise<LeaderResponse>> &promise,
                  const v3electionpb::LeaderRequest &request, int retryTimes);

    void DoResign(const std::shared_ptr<litebus::Promise<ResignResponse>> &promise,
                  const v3electionpb::ResignRequest &request, int retryTimes);

    std::unique_ptr<GrpcClient<v3electionpb::Election>> electionClient_;
};

}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_ELECTION_ETCD_ELECTION_CLIENT_STRATEGY_H
