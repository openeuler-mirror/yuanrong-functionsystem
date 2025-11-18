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

#include "etcd_leader_actor.h"

#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

namespace functionsystem::leader {

using namespace functionsystem::explorer;

EtcdLeaderActor::EtcdLeaderActor(const std::string &electionKey, const explorer::ElectionInfo &electionInfo,
                                 const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : LeaderActor("EtcdLeaderActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), {electionKey},
                  electionInfo),
      metaStoreClient_(metaStoreClient)
{
    YRLOG_INFO("start etcd leader actor({})", std::string(GetAID()));
    electionKey_ = electionKeySet_.size() > 0 ? electionKeySet_.begin()->c_str() : "";
}

void EtcdLeaderActor::Init()
{
    // when construct, register callbacks
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        electionKey_ + "-leaderactor", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &EtcdLeaderActor::OnLeaderChange, leaderInfo);
        });
}

void EtcdLeaderActor::Finalize()
{
    ASSERT_IF_NULL(metaStoreClient_);
    (void)metaStoreClient_->Resign(leaderKey_);
    (void)Explorer::GetInstance().RemoveLeaderChangedCallback(electionKey_ + "-leaderactor");
}

void EtcdLeaderActor::OnLeaderChange(const LeaderInfo &leaderInfo)
{
    // normally the leader change event doesn't trigger anything in leader
    if (leaderInfo.address == proposal_) {
        YRLOG_INFO("I am the Leader according to the latest leader observation({})!", leaderInfo.address);
        if (cachedLeaderInfo_.address == proposal_) {
            return;
        }
        if (callbackWhenBecomeLeader_) {
            callbackWhenBecomeLeader_();
        }
    } else {
        if (cachedLeaderInfo_.address == proposal_) {
            YRLOG_INFO("I am no longer the leader according to the latest leader observation({})!", leaderInfo.address);
            if (callbackWhenResign_) {
                callbackWhenResign_();
            }
            return;
        }
        // after update cache, need to check whether we are still in the election process.
        // when campaign succeed, the campaign will stop and wait for the notify event,
        // but the event may not come since the lease contained in the campaign request
        // may not valid anymore
        if (isCampaigning_ == nullptr) {
            YRLOG_INFO("I am not electing, and I({}) am not the chosen leader({}), re-elect now", proposal_,
                       leaderInfo.address);
            litebus::Async(GetAID(), &LeaderActor::Elect);
        }
    }

    cachedLeaderInfo_ = leaderInfo;
}

/**
 * observes and participates in leader election
 *   1. grant a lease
 *   2. try best to keep alive it periodically
 *   3. start campaign and observe
 *
 * note:
 *   when keep alive fails, the leader will resign, the backups do nothing
 */
void EtcdLeaderActor::Elect()
{
    if (isCampaigning_ != nullptr) {
        YRLOG_WARN("an election already started, wait this process finished");
        return;
    }

    isCampaigning_ = std::make_shared<litebus::Promise<bool>>();
    currentLeaseId_ = -1;
    YRLOG_INFO("EtcdLeaderActor on {} begin elect", electionKey_);
    ASSERT_IF_NULL(metaStoreClient_);
    (void)metaStoreClient_->Grant(leaseTTL_)
        .Then(litebus::Defer(GetAID(), &EtcdLeaderActor::OnGrantResponse, std::placeholders::_1))
        .Then(litebus::Defer(GetAID(), &EtcdLeaderActor::KeepAlive, std::placeholders::_1))
        .Then(litebus::Defer(GetAID(), &EtcdLeaderActor::Campaign, std::placeholders::_1))
        .OnComplete(litebus::Defer(GetAID(), &EtcdLeaderActor::OnCampaignResponse, std::placeholders::_1));
}

litebus::Future<int64_t> EtcdLeaderActor::OnGrantResponse(const LeaseGrantResponse &response)
{
    // the first .Then checks grant response
    if (response.status != StatusCode::SUCCESS) {
        // if failed, set it failed, and don't go to the keep alive process
        YRLOG_ERROR("leader-actor({}) failed to grant a lease, grant response status is {}", electionKey_,
                    response.status.ToString());
        currentLeaseId_ = -1;
        litebus::Promise<int64_t> promise;
        promise.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
        return promise.GetFuture();
    }
    YRLOG_INFO("EtcdLeaderActor succeed to grant a lease({})", response.leaseId);
    currentLeaseId_ = response.leaseId;
    return response.leaseId;
}

litebus::Future<int64_t> EtcdLeaderActor::KeepAlive(int64_t leaseId)
{
    litebus::Async(GetAID(), &EtcdLeaderActor::DoKeepAlive, leaseId);
    return leaseId;
}

void EtcdLeaderActor::DoKeepAlive(int64_t leaseId)
{
    YRLOG_DEBUG("EtcdLeaderActor({}) is going to keep alive lease(id={}, ttl={}) with interval({})", electionKey_,
                leaseId, leaseTTL_, keepAliveInterval_);
    // if lease changed, abort the last keep alive loop
    if (leaseId != currentLeaseId_) {
        YRLOG_WARN("EtcdLeaderActor({}) is going to keep alive lease({}) and find it is not the latest, aborted",
                   electionKey_, leaseId);
        return;
    }
    ASSERT_IF_NULL(metaStoreClient_);
    (void)metaStoreClient_->KeepAliveOnce(leaseId).OnComplete(
        litebus::Defer(GetAID(), &EtcdLeaderActor::OnKeepAlive, std::placeholders::_1, leaseId));
}

void EtcdLeaderActor::OnKeepAlive(const litebus::Future<LeaseKeepAliveResponse> &response, int64_t leaseId)
{
    if (leaseId != currentLeaseId_) {
        YRLOG_WARN("lease id({}) is not current({}), stop keep alive", leaseId, currentLeaseId_);
        return;
    }

    // check response and resend
    if (response.IsError() || response.Get().status != StatusCode::SUCCESS || response.Get().ttl == 0) {
        YRLOG_ERROR("EtcdLeaderActor({}) failed to keep alive a lease or lease is timeout, status is {}", electionKey_,
                    response.Get().status.ToString());

        currentLeaseId_ = -1;
        if (isCampaigning_ == nullptr) {
            litebus::Async(GetAID(), &LeaderActor::Elect);
        } else {
            isCampaigning_->GetFuture().OnComplete(litebus::Defer(GetAID(), &LeaderActor::Elect));
        }
        return;
    }
    keepAliveTimer_ =
        litebus::AsyncAfter(keepAliveInterval_ * litebus::SECTOMILLI, GetAID(), &EtcdLeaderActor::DoKeepAlive, leaseId);
}

litebus::Future<CampaignResponse> EtcdLeaderActor::Campaign(int64_t leaseID)
{
    YRLOG_INFO("EtcdLeaderActor({}) starts to campaign with lease({})", electionKey_, leaseID);
    ASSERT_IF_NULL(metaStoreClient_);
    return metaStoreClient_->Campaign(electionKey_, leaseID, proposal_);
}

void EtcdLeaderActor::OnCampaignResponse(const litebus::Future<CampaignResponse> &responseFuture)
{
    isCampaigning_->SetValue(true);
    isCampaigning_ = nullptr;

    if (responseFuture.IsError()) {
        YRLOG_ERROR("failed to grant a lease or lease is expired");
        litebus::Async(GetAID(), &LeaderActor::Elect);
        return;
    }

    if (currentLeaseId_ == -1) {
        YRLOG_ERROR("lease is expired, already re-elected");
        return;
    }

    if (!responseFuture.Get().status.IsOk()) {
        YRLOG_ERROR("EtcdLeaderActor({}) campaign failed, status: {}, re-campaign now", electionKey_,
                    responseFuture.GetErrorCode());
        litebus::Async(GetAID(), &LeaderActor::Elect);
        return;
    }
    auto campaignResp = responseFuture.Get();
    leaderKey_ = campaignResp.leader;
    YRLOG_INFO("campaign successfully, leaderKey: key({}), lease({}), waiting for the observation to confirm ",
               leaderKey_.key, leaderKey_.lease);
    explorer::LeaderInfo leaderInfo{ .name = campaignResp.leader.name,
                                     .address = proposal_,
                                     .electRevision = campaignResp.header.revision };
    // Call Campaign success, the Observer not be triggered.
    if (publishLeaderCallBack_) {
        publishLeaderCallBack_(leaderInfo);
    }
}

}  // namespace functionsystem::leader
