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

#include "common/leader/txn_leader_actor.h"

#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/metrics/metrics_adapter.h"

namespace functionsystem::leader {
TxnLeaderActor::TxnLeaderActor(const std::string &electionKey, const explorer::ElectionInfo &electionInfo,
                               const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : LeaderActor("TxnLeaderActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), { electionKey },
                  electionInfo),
      metaStoreClient_(metaStoreClient)
{
    electionKey_ = !electionKeySet_.empty() ? electionKeySet_.begin()->c_str() : "";
}

void TxnLeaderActor::Init()
{
    YRLOG_INFO("{} | election initialize", electionKey_);
    ASSERT_IF_NULL(metaStoreClient_);
    // new master start, if leader exist, to be slave.
    (void)GetLeader();
}

void TxnLeaderActor::GetLeader()
{
    auto observer = [aid(GetAID()), key(electionKey_)](const std::vector<WatchEvent> &events, bool) -> bool {
        // If Leader changes during the disconnection from the etcd, the historical revision is used for re-watch.
        // Multiple Leader records may exist. The last record is preferentially used.
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (it->eventType == EventType::EVENT_TYPE_DELETE) {
                YRLOG_INFO("{} | leader is deleted, start elect", key);
                // 2. leader delete, do elect immediately
                litebus::Async(aid, &TxnLeaderActor::Elect);
                break;
            } else if (it->eventType == EventType::EVENT_TYPE_PUT) {
                YRLOG_INFO("{} | leader is changed to {}", key, it->kv.value());
                if (!IsAddressesValid(it->kv.value())) {
                    // leader info is invalid
                    YRLOG_WARN("{} | invalid leader info {} in event", key, it->kv.value());
                    metrics::MetricsAdapter::GetInstance().ElectionFiring("invalid leader info " + it->kv.value()
                                                                          + " elected for " + key);
                } else {
                    metrics::MetricsAdapter::GetInstance().ElectionFiringResolved(
                        "leader " + it->kv.value() + " is elected successfully for " + key);
                }
            }
        }
        return true;
    };

    auto syncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &TxnLeaderActor::Sync, getResponse);
    };

    auto handler = [aid(GetAID()), key(electionKey_), delay(keepAliveInterval_)](
                       const std::shared_ptr<GetResponse> &response) -> litebus::Future<Status> {
        if (response->status.IsError()) {
            // 1.1 if network error, delay electing
            YRLOG_ERROR("{} | error to get leader, delay elect", key);
            litebus::AsyncAfter(delay * litebus::SECTOMILLI, aid, &TxnLeaderActor::Elect);
        } else if (response->kvs.empty()) {
            // 1.2 if no leader, do elect immediately
            YRLOG_INFO("{} | no leader, start elect", key);
            litebus::Async(aid, &TxnLeaderActor::Elect);
        } else if (!IsAddressesValid(response->kvs[0].value())) {
            // 1.3 leader info is invalid
            YRLOG_WARN("{} | invalid leader info ({}) existed when initialization", key, response->kvs[0].value());
            metrics::MetricsAdapter::GetInstance().ElectionFiring("invalid leader info " + response->kvs[0].value()
                                                                  + " elected for " + key);
        }
        return Status::OK();
    };
    WatchOption option{};
    option.keepRetry = true;
    (void)metaStoreClient_->GetAndWatchWithHandler(electionKey_, option, observer, syncer, handler)
        .Then([aid(GetAID())](const std::shared_ptr<Watcher> &watcher) -> litebus::Future<Status> {
            return litebus::Async(aid, &TxnLeaderActor::OnWatch, watcher);
        });
}

litebus::Future<Status> TxnLeaderActor::OnWatch(const std::shared_ptr<Watcher> &watcher)
{
    watcher_ = watcher;  // for cancel
    return Status::OK();
}

void TxnLeaderActor::Finalize()
{
    YRLOG_INFO("{} | election finalize", electionKey_);
    leader_ = false;
    campaigning_ = false;

    if (watcher_) {
        watcher_->Close();
    }

    if (leaseID_ != -1) {
        metaStoreClient_->Revoke(leaseID_);  // graceful
    }

    metaStoreClient_ = nullptr;  // must do, trigger destructor
}

void TxnLeaderActor::Elect()
{
    if (campaigning_) {
        YRLOG_WARN("{} | has been electing", electionKey_);
        return;
    }

    leader_ = false;
    campaigning_ = true;
    YRLOG_INFO("{} | start elect", electionKey_);
    metaStoreClient_->Grant(static_cast<int>(leaseTTL_))
        .Then([aid(GetAID())](const LeaseGrantResponse &response) -> litebus::Future<int64_t> {
            return litebus::Async(aid, &TxnLeaderActor::OnGrantLease, response);
        })
        .Then([aid(GetAID())](const int64_t &leaseID) -> litebus::Future<std::shared_ptr<TxnResponse>> {
            return litebus::Async(aid, &TxnLeaderActor::Campaign, leaseID);
        })
        .OnComplete([aid(GetAID())](const litebus::Future<std::shared_ptr<TxnResponse>> &response) {
            (void)litebus::Async(aid, &TxnLeaderActor::OnCampaign, response);
        });
}

litebus::Future<int64_t> TxnLeaderActor::OnGrantLease(const LeaseGrantResponse &response)
{
    if (response.status != StatusCode::SUCCESS) {
        // if failed, set it failed, and don't go to the keep alive process
        YRLOG_ERROR("{} | failed to grant a lease: {}", electionKey_, response.status.ToString());
        return litebus::Status(litebus::Status::KERROR);
    }

    leaseID_ = response.leaseId;
    YRLOG_INFO("{} | succeed to grant a lease({})", electionKey_, response.leaseId);
    (void)litebus::AsyncAfter(keepAliveInterval_ * litebus::SECTOMILLI, GetAID(), &TxnLeaderActor::KeepAlive, leaseID_);

    return response.leaseId;
}

litebus::Future<std::shared_ptr<TxnResponse>> TxnLeaderActor::Campaign(int64_t leaseID)
{
    YRLOG_INFO("{} | starts to campaign with lease({})", electionKey_, leaseID);
    std::shared_ptr<meta_store::TxnTransaction> transaction = metaStoreClient_->BeginTransaction();
    transaction->If(meta_store::TxnCompare::OfCreateVersion(electionKey_, meta_store::CompareOperator::EQUAL, 0));

    PutOption putOption{ .leaseId = leaseID, .prevKv = false, .asyncBackup = false };
    transaction->Then(meta_store::TxnOperation::Create(electionKey_, proposal_, putOption));

    return transaction->Commit();
}

void TxnLeaderActor::OnCampaign(const litebus::Future<std::shared_ptr<TxnResponse>> &response)
{
    campaigning_ = false;
    if (response.IsOK() && response.Get()->success) {
        YRLOG_INFO("{} | success to campaign", electionKey_);
        leader_ = true;
        return;
    }

    YRLOG_ERROR("{} | failed to campaign", electionKey_);
    leader_ = false;
    if (leaseID_ != -1) {
        metaStoreClient_->Revoke(leaseID_);  // graceful
    }

    // make sure there's a leader, or do elect again.
    metaStoreClient_->Get(electionKey_, { .prefix = false, .keysOnly = false })
        .Then([aid(GetAID()), electionKey(electionKey_),
               delay(keepAliveInterval_)](const std::shared_ptr<GetResponse> &res) -> litebus::Future<Status> {
            if (res->status.IsError()) {
                // 3.1 if network error, delay electing
                litebus::AsyncAfter(delay * litebus::SECTOMILLI, aid, &TxnLeaderActor::Elect);
            } else if (res->kvs.empty()) {
                YRLOG_WARN("{} | no leader elected after election, start elect", electionKey);
                // 3.2 campaign fail and no leader, do elect immediately
                litebus::Async(aid, &TxnLeaderActor::Elect);
                metrics::MetricsAdapter::GetInstance().ElectionFiring("No leader elected for " + electionKey);
            } else if (!IsAddressesValid(res->kvs[0].value())) {
                // 3.3 leader info is invalid
                YRLOG_WARN("{} | invalid leader info ({}) elected after election", electionKey, res->kvs[0].value());
                metrics::MetricsAdapter::GetInstance().ElectionFiring("invalid leader info " + res->kvs[0].value()
                                                                      + " elected for " + electionKey);
            } else {
                metrics::MetricsAdapter::GetInstance().ElectionFiringResolved(
                    "leader " + res->kvs[0].value() + " is elected successfully for " + electionKey);
            }
            return Status::OK();
        });
}

void TxnLeaderActor::KeepAlive(int64_t leaseID)
{
    if (leaseID_ != -1 && leaseID_ != leaseID) {
        YRLOG_ERROR("{} | lease: {} not match current lease: {}.", electionKey_, leaseID, leaseID_);
        return;
    }

    if (!campaigning_ && !leader_) {
        YRLOG_WARN("{} | not leader, do not keep alive lease.", electionKey_);
        return;
    }

    metaStoreClient_->KeepAliveOnce(leaseID).Then([key(electionKey_), aid(GetAID()), delay(keepAliveInterval_)](
                                                      const LeaseKeepAliveResponse &response) -> Status {
        if (response.status.IsError()) {
            // 4. keep alive leader lease fail, delay electing
            YRLOG_ERROR("{} | keep alive lease error, delay electing.", key);
            litebus::AsyncAfter(delay * litebus::SECTOMILLI, aid, &TxnLeaderActor::Elect);
        }
        return Status::OK();
    });

    (void)litebus::AsyncAfter(keepAliveInterval_ * litebus::SECTOMILLI, GetAID(), &TxnLeaderActor::KeepAlive, leaseID);
}

litebus::Future<SyncResult> TxnLeaderActor::Sync(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse->status.IsError()) {
        YRLOG_ERROR("failed to get leader key({}) from meta storage, for txn leader", electionKey_);

        // leader delete, do elect immediately
        litebus::Async(GetAID(), &TxnLeaderActor::Elect);
        return SyncResult{ getResponse->status };
    }

    if (getResponse->kvs.empty()) {
        YRLOG_WARN("get no result with leader key({}) from meta storage, for txn leader, revision is {}", electionKey_,
                   getResponse->header.revision);

        // leader delete, do elect immediately
        litebus::Async(GetAID(), &TxnLeaderActor::Elect);
        return SyncResult{ Status::OK() };
    }

    return SyncResult{ Status::OK() };
}
}  // namespace functionsystem::leader
