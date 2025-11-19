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

#include "common/explorer/etcd_explorer_actor.h"

#include <async/async.hpp>
#include <async/defer.hpp>

namespace functionsystem::explorer {

void EtcdExplorerActor::Finalize()
{
    YRLOG_INFO("clear explorer_actor");
    if (observer_) {
        observer_->Shutdown();
    }

    ExplorerActor::Finalize();
}

void EtcdExplorerActor::Observe()
{
    electionKey_ = electionKeySet_.size() > 0 ? electionKeySet_.begin()->c_str() : "";
    YRLOG_INFO("EtcdExplorerActor({}) starts to observe", electionKey_);
    ASSERT_IF_NULL(metaStoreClient_);
    (void)metaStoreClient_
        ->Observe(electionKey_,
                  [aid(GetAID())](const LeaderResponse &response) {
                      litebus::Async(aid, &EtcdExplorerActor::OnObserveEvent, response);
                  })
        .Then(litebus::Defer(GetAID(), &EtcdExplorerActor::UpdateObserver, std::placeholders::_1));
}

Status EtcdExplorerActor::UpdateObserver(const std::shared_ptr<Observer> &observer)
{
    observer_ = observer;
    return Status::OK();
}

void EtcdExplorerActor::OnObserveEvent(const LeaderResponse &response)
{
    YRLOG_DEBUG("Receive observe event : ({}, {}, {}), will trigger callbacks", response.status.ToString(),
                response.kv.first, response.kv.second);

    UpdateLeaderInfo(LeaderInfo{
        .name = response.kv.first, .address = response.kv.second, .electRevision = response.header.revision });
}

void EtcdExplorerActor::UpdateLeaderInfo(const LeaderInfo &leaderInfo)
{
    auto revision = leaderInfo.electRevision;
    if (revision != 0 && revision < electRevision_) {
        YRLOG_WARN("receive old event, revision is {}, current revision is {}", revision, electRevision_);
        return;
    }
    if (revision != 0) {
        electRevision_ = revision;
    }
    // 1. update cache
    cachedLeaderInfo_ = leaderInfo;
    // 2. trigger callbacks
    for (auto &cb : callbacks_) {
        YRLOG_DEBUG("ExplorerActor({}) triggers callback({}) with leader name({}) address({})", electionKey_, cb.first,
                    leaderInfo.name, leaderInfo.address);
        cb.second(cachedLeaderInfo_);
    }
}

void EtcdExplorerActor::FastPublish(const LeaderInfo &leaderInfo)
{
    YRLOG_INFO("fast publish leader name({}) address({}) revision({})", leaderInfo.name, leaderInfo.address,
               leaderInfo.electRevision);
    UpdateLeaderInfo(leaderInfo);
}

}  // namespace functionsystem::explorer