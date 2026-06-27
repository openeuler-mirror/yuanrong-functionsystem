/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_MASTER_META_STORE_WATCH_SERVICE_PASSTHROUGH_ACTOR_H
#define FUNCTION_MASTER_META_STORE_WATCH_SERVICE_PASSTHROUGH_ACTOR_H

#include "meta_store_client/meta_store_client.h"
#include "meta_store/server/src/watch_service_actor.h"

namespace functionsystem::meta_store {

class WatchServicePassthroughActor : public WatchServiceActor {
public:
    explicit WatchServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient);

    ~WatchServicePassthroughActor() override = default;

    litebus::Future<std::shared_ptr<::etcdserverpb::WatchResponse>> CreateWatch(
        const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request) override;

    bool Cancel(const litebus::AID &from, int64_t watchId, const std::string &msg) override;

    void OnWatch(const litebus::Future<std::shared_ptr<Watcher>> &watcher, int64_t watchId,
                 const std::shared_ptr<::etcdserverpb::WatchResponse> &response,
                 const litebus::Promise<std::shared_ptr<::etcdserverpb::WatchResponse>> &promise);

    void HandleWatchEvents(const std::vector<WatchEvent> &events, int64_t watchId);

    litebus::Future<SyncResult> WatchSyncer(const litebus::AID &from, int64_t watchId);

private:
    std::shared_ptr<MetaStoreClient> etcdClient_;
    std::unordered_map<uint64_t, std::shared_ptr<Watcher>> watchers_;
};

}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_WATCH_SERVICE_PASSTHROUGH_ACTOR_H
