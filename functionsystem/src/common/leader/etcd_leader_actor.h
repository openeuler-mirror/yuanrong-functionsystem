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

#ifndef COMMON_LEADER_ETCD_LEADER_ACTOR_H
#define COMMON_LEADER_ETCD_LEADER_ACTOR_H

#include "meta_store_client/meta_store_client.h"
#include "leader_actor.h"

namespace functionsystem::leader {

/**
 * LeaderActor will do election jobs, mainly
 *  - grant a lease, keep alive it, and campagin
 *  - observe current leader
 *
 * usage example:
 *
 * {
 *   auto la = std::make_shared<LeaderActor>("key1", "x.x.x.x:ppp", clientPtr, 60, 20);
 *   la->RegisterCallbackWhenBecomeLeader([aid(GetAID())](LeaderInfo& lf) { litebus::Async(aid, &AA::func, lf); });
 *   la->RegisterCallbackWhenResign([aid(GetAID())](LeaderInfo& lf) { BUS_EXIT("I am not leader anymore~") });
 *   la->Elect();
 * }
 *
 **/
class EtcdLeaderActor : public LeaderActor {
public:
    // when leaseTTL expires, the leadership will be ressigned automatically
    EtcdLeaderActor(const std::string &electionKey, const explorer::ElectionInfo &electionInfo,
                    const std::shared_ptr<MetaStoreClient> &metaStoreClient);
    ~EtcdLeaderActor() override = default;

    /**
     * observes and participates in leader election
     * 1. grant a lease
     * 2. try best to keep alive it periodically
     * 3. start campaign and observe
     *
     * note:
     *   when keep alive fails, the leader will resign, the backups do nothing
     */
    void Elect() override;

protected:
    void Init() override;
    void Finalize() override;

private:
    // op on grant response
    litebus::Future<int64_t> OnGrantResponse(const LeaseGrantResponse &response);

    // start campaign
    // when campaign success, will trigger campaignSuccessCallback, which means current master becomes the leader
    // otherwise the campaign will be called inside periodically, there is no stop condition
    litebus::Future<CampaignResponse> Campaign(int64_t leaseId);
    void OnCampaignResponse(const litebus::Future<CampaignResponse> &responseFuture);

    // Keep alive the lease
    litebus::Future<int64_t> KeepAlive(int64_t leaseId);
    void DoKeepAlive(int64_t leaseId);

    void OnLeaderChange(const explorer::LeaderInfo &leaderInfo);

    void OnKeepAlive(const litebus::Future<LeaseKeepAliveResponse> &response, int64_t leaseId);

    // meta store client
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };

    LeaderKey leaderKey_;

    int64_t currentLeaseId_ = -1;

    litebus::Timer keepAliveTimer_;

    // etcd election only supports one election key now
    std::string electionKey_;
};
}  // namespace functionsystem::leader

#endif  // COMMON_LEADER_ETCD_LEADER_ACTOR_H
