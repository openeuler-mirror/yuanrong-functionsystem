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

#include "common/explorer/txn_explorer_actor.h"

#include <async/async.hpp>
#include <async/defer.hpp>

namespace functionsystem::explorer {

TxnExplorerActor::TxnExplorerActor(const std::string &electionKey, const ElectionInfo &electionInfo,
                                   const litebus::Option<LeaderInfo> &leaderInfo,
                                   const std::shared_ptr<MetaStoreClient> &metaStoreClient)
    : ExplorerActor("TxnExplorerActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString(), {electionKey},
        electionInfo, leaderInfo),
      metaStoreClient_(metaStoreClient)
{
}

void TxnExplorerActor::Finalize()
{
    YRLOG_INFO("{} | Clear explorer actor", electionKey_);
    if (watcher_) {
        watcher_->Close();
    }

    ExplorerActor::Finalize();
}

void TxnExplorerActor::Observe()
{
    electionKey_ = !electionKeySet_.empty() ? electionKeySet_.begin()->c_str() : "";
    YRLOG_INFO("{} | start to watch leader", electionKey_);
    auto observer = [aid(GetAID())](const std::vector<WatchEvent> &events, bool) -> bool {
        // If Leader changes during the disconnection from the etcd, the historical revision is used for re-watch.
        // Multiple Leader records may exist. The last record is preferentially used.
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (it->eventType != EventType::EVENT_TYPE_PUT) {
                continue;
            }

            litebus::Async(aid, &TxnExplorerActor::OnWatchEvent, *it);
        }
        return true;
    };

    auto syncer = [aid(GetAID())](const std::shared_ptr<GetResponse> &response) -> litebus::Future<SyncResult> {
        return litebus::Async(aid, &TxnExplorerActor::Sync, response);
    };
    RETURN_IF_NULL(metaStoreClient_);
    (void)metaStoreClient_
        ->GetAndWatch(electionKey_, { .prefix = false, .prevKv = false, .revision = 0 }, observer, syncer)
        .Then(std::function<litebus::Future<Status>(const std::shared_ptr<Watcher> &)>(
            [aid(GetAID())](const std::shared_ptr<Watcher> &watcher) -> litebus::Future<Status> {
                return litebus::Async(aid, &TxnExplorerActor::OnWatch, watcher);
            }));
}

Status TxnExplorerActor::OnWatch(const std::shared_ptr<Watcher> &watcher)
{
    watcher_ = watcher;  // for cancel
    return Status::OK();
}

void TxnExplorerActor::OnWatchEvent(const WatchEvent &event)
{
    if (event.kv.mod_revision() != 0 && event.kv.mod_revision() <= electRevision_) {
        YRLOG_ERROR("{} | Receive old LeaderInfo: {} before the current revision: {}", electionKey_,
                    event.kv.mod_revision(), electRevision_);
        // The last record is preferentially used.
        return;
    }

    const auto &leaderInfo =
        LeaderInfo{ .name = event.kv.key(), .address = event.kv.value(), .electRevision = event.kv.mod_revision() };
    YRLOG_DEBUG("{} | Update leader: {}, address: {}.", electionKey_, leaderInfo.name, leaderInfo.address);
    electRevision_ = leaderInfo.electRevision;
    cachedLeaderInfo_ = leaderInfo;
    for (auto &[id, callback] : callbacks_) {
        YRLOG_DEBUG("{} | Trigger callback({}) with leader: {}", electionKey_, id, leaderInfo.name);
        callback(cachedLeaderInfo_);
    }
}

void TxnExplorerActor::FastPublish(const LeaderInfo &leaderInfo)
{
}

litebus::Future<SyncResult> TxnExplorerActor::Sync(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse->status.IsError()) {
        YRLOG_ERROR("failed to get key({}) from meta storage, for txn explorer", electionKey_);
        return SyncResult{ getResponse->status };
    }

    if (getResponse->kvs.empty()) {
        YRLOG_WARN("get no result with key({}) from meta storage, for txn explorer, revision is {}", electionKey_,
                   getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }

    WatchEvent event;
    event.kv = getResponse->kvs.front();
    OnWatchEvent(event);
    return SyncResult{ Status::OK() };
}

}  // namespace functionsystem::explorer