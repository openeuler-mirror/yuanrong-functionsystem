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

#ifndef COMMON_LEADER_TXN_LEADER_ACTOR_H
#define COMMON_LEADER_TXN_LEADER_ACTOR_H

#include "common/leader/leader_actor.h"
#include "meta_store_client/meta_store_client.h"

namespace functionsystem::leader {
class TxnLeaderActor : public LeaderActor {
public:
    TxnLeaderActor(const std::string &electionKey, const explorer::ElectionInfo &electionInfo,
                   const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    ~TxnLeaderActor() override = default;

    void Elect() override;

protected:
    void Init() override;
    void Finalize() override;

private:
    void GetLeader();

    litebus::Future<Status> OnWatch(const std::shared_ptr<Watcher> &watcher);

    litebus::Future<int64_t> OnGrantLease(const LeaseGrantResponse &response);

    void KeepAlive(int64_t leaseID);

    litebus::Future<std::shared_ptr<TxnResponse>> Campaign(int64_t leaseID);

    void OnCampaign(const litebus::Future<std::shared_ptr<TxnResponse>> &response);

    litebus::Future<SyncResult> Sync(const std::shared_ptr<GetResponse> &getResponse);

private:
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };

    std::shared_ptr<Watcher> watcher_{ nullptr };

    litebus::Timer keepAliveTimer_;

    int64_t leaseID_ = -1;

    bool campaigning_ = false;
    bool leader_ = false;

    // txn election only supports one election key now
    std::string electionKey_;
};
}  // namespace functionsystem::leader

#endif  // COMMON_LEADER_TXN_LEADER_ACTOR_H
