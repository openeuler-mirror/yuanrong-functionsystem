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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_META_STORE_ELECTION_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_ELECTION_META_STORE_ELECTION_CLIENT_STRATEGY_H

#include "common/proto/pb/message_pb.h"
#include "common/utils/request_sync_helper.h"
#include "election_client_strategy.h"
#include "meta_store_observer.h"

namespace functionsystem::meta_store {

class MetaStoreElectionClientStrategy : public ElectionClientStrategy {
public:
    MetaStoreElectionClientStrategy(const std::string &name, const std::string &address,
                                    const MetaStoreTimeoutOption &timeoutOption,
                                    const std::string &etcdTablePrefix = "");

    ~MetaStoreElectionClientStrategy() override = default;

    litebus::Future<CampaignResponse> Campaign(const std::string &name, int64_t lease,
                                               const std::string &value) override;
    litebus::Future<LeaderResponse> Leader(const std::string &name) override;
    litebus::Future<ResignResponse> Resign(const LeaderKey &leader) override;
    litebus::Future<std::shared_ptr<Observer>> Observe(const std::string &name,
                                                       const std::function<void(LeaderResponse)> &callback) override;
    litebus::Future<bool> IsConnected() override;

    void OnAddressUpdated(const std::string &address) override;

    void CancelObserve(uint64_t observeID);

protected:
    void Init() override;

private:
    void OnCampaign(const litebus::AID &, std::string &&, std::string &&msg);
    void OnLeader(const litebus::AID &, std::string &&, std::string &&msg);
    void OnResign(const litebus::AID &, std::string &&, std::string &&msg);
    void OnObserve(const litebus::AID &from, std::string &&, std::string &&msg);

    void OnObserveCreated(const messages::MetaStore::ObserveResponse &response, const std::string &uuid,
                          const litebus::AID &from);
    void OnObserveEvent(const messages::MetaStore::ObserveResponse &response);
    void OnObserveCancel(uint64_t observeID);

    void ReconnectSuccess();

    std::shared_ptr<litebus::AID> electionServiceAid_;

    std::vector<std::shared_ptr<MetaStoreObserver>> observers_;
    std::unordered_map<std::string, std::shared_ptr<MetaStoreObserver>> pendingObservers_;
    std::unordered_map<uint64_t, std::shared_ptr<MetaStoreObserver>> readyObservers_;

    BACK_OFF_RETRY_HELPER(MetaStoreElectionClientStrategy, CampaignResponse, campaignHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreElectionClientStrategy, LeaderResponse, leaderHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreElectionClientStrategy, ResignResponse, resignHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreElectionClientStrategy, bool, observeHelper_)
};

}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_ELECTION_ETCD_ELECTION_CLIENT_STRATEGY_H
