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

#ifndef COMMON_EXPLORER_TXN_EXPLORER_ACTOR_H
#define COMMON_EXPLORER_TXN_EXPLORER_ACTOR_H

#include "common/explorer/explorer_actor.h"
#include "meta_store_client/meta_store_client.h"

namespace functionsystem::explorer {

class TxnExplorerActor : public ExplorerActor {
public:
    TxnExplorerActor(const std::string &electionKey, const ElectionInfo &electionInfo,
                     const litebus::Option<LeaderInfo> &leaderInfo,
                     const std::shared_ptr<MetaStoreClient> &metaStoreClient);

    ~TxnExplorerActor() override = default;

    void Observe() override;

    void FastPublish(const LeaderInfo &leaderInfo) override;

protected:
    void Finalize() override;

private:
    void OnWatchEvent(const WatchEvent &event);

    Status OnWatch(const std::shared_ptr<Watcher> &watcher);

    litebus::Future<SyncResult> Sync(const std::shared_ptr<GetResponse> &getResponse);

private:
    std::shared_ptr<MetaStoreClient> metaStoreClient_{ nullptr };
    std::shared_ptr<Watcher> watcher_{ nullptr };
    // txn explorer only supports one election key now
    std::string electionKey_;
};
}  // namespace functionsystem::explorer

#endif  // COMMON_EXPLORER_TXN_EXPLORER_ACTOR_H
