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

#include "watch_service_passthrough_actor.h"

#include "async/defer.hpp"

namespace functionsystem::meta_store {

WatchServicePassthroughActor::WatchServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient)
    : WatchServiceActor("WatchServiceActor"), etcdClient_(etcdClient)
{
}

litebus::Future<std::shared_ptr<::etcdserverpb::WatchResponse>> WatchServicePassthroughActor::CreateWatch(
    const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request)
{
    // all passthrough watch is consider as strict watch in watch service
    auto internalRequest = std::make_shared<::etcdserverpb::WatchCreateRequest>(*request);
    internalRequest->clear_range_end();
    auto response = WatchServiceActor::CreateInternal(from, internalRequest);

    auto syncer = [aid(GetAID()), from, watchID(response->watch_id())](const std::shared_ptr<GetResponse> &) {
        return litebus::Async(aid, &WatchServicePassthroughActor::WatchSyncer, from, watchID);
    };
    auto eventsObserver = [aid(GetAID()), watchID(response->watch_id())](const std::vector<WatchEvent> &events, bool) {
        litebus::Async(aid, &WatchServicePassthroughActor::HandleWatchEvents, events, watchID);
        return true;
    };

    WatchOption watchOption = { .prefix = IsRangeObserver(request),
                                .prevKv = request->prev_kv(),
                                .revision = request->start_revision(),
                                .keepRetry = true };
    litebus::Promise<std::shared_ptr<::etcdserverpb::WatchResponse>> promise;
    YRLOG_DEBUG("start watch for key({}), isPrefix: {}, from revision: {}, watch({})", request->key(),
                watchOption.prefix, watchOption.revision, response->watch_id());
    etcdClient_->Watch(request->key(), watchOption, eventsObserver, syncer)
        .OnComplete(litebus::Defer(GetAID(), &WatchServicePassthroughActor::OnWatch, std::placeholders::_1,
                                   response->watch_id(), response, promise));
    return promise.GetFuture();
}

void WatchServicePassthroughActor::OnWatch(
    const litebus::Future<std::shared_ptr<Watcher>> &watcher, int64_t watchId,
    const std::shared_ptr<::etcdserverpb::WatchResponse> &response,
    const litebus::Promise<std::shared_ptr<::etcdserverpb::WatchResponse>> &promise)
{
    if (watcher.IsError()) {
        YRLOG_ERROR("failed to watch for watcher({})", watchId);
        RemoveObserverById(watchId);
        return;
    }

    // execute patent's CreateInternal, add watcher of etcd
    watchers_[watchId] = watcher.Get();
    promise.SetValue(response);
}

void WatchServicePassthroughActor::HandleWatchEvents(const std::vector<WatchEvent> &events, int64_t watchId)
{
    auto observer = strictObserversById_.find(watchId);
    if (observer == strictObserversById_.end()) {
        YRLOG_ERROR("failed to find observer for watcher({})", watchId);
        return;
    }

    for (const auto &event : events) {
        UnsyncedEvents::Ptr response;
        switch (event.eventType) {
            case EVENT_TYPE_PUT:
                YRLOG_DEBUG("watcher({}) receive etcd put event, key: {}", watchId, event.kv.key());
                response = BuildUnsyncedEventsForPut(event.kv, event.prevKv);
                break;
            case EVENT_TYPE_DELETE:
                YRLOG_DEBUG("watcher({}) receive etcd delete event, key: {}", watchId, event.kv.key());
                response = BuildUnsyncedEventsForDelete(event.kv);
                break;
            default:
                YRLOG_WARN("unknown event type {}", static_cast<int>(event.eventType));
                break;
        }
        if (response != nullptr) {
            AddObserverToResponse(response, observer->second);
            AddToUnsyncedEvents(response);
        }
    }
}

litebus::Future<SyncResult> WatchServicePassthroughActor::WatchSyncer(const litebus::AID &from, int64_t watchId)
{
    if (auto iter = watchers_.find(watchId); iter != watchers_.end()) {
        YRLOG_INFO("syncer cancel watcher({})", watchId);
        if (iter->second != nullptr) {
            // send cancel to etcd server
            iter->second->Close();
        }
        watchers_.erase(iter);
        // send cancel to metastore client
        WatchServiceActor::Cancel(from, watchId, "etcd compacted, need re-watch");

        RemoveObserverById(watchId, from);
    }

    // don't re-watch
    return litebus::Status(litebus::Status::KERROR);
}

bool WatchServicePassthroughActor::Cancel(const litebus::AID &from, int64_t watchId, const std::string &msg)
{
    if (const auto iter = watchers_.find(watchId); iter != watchers_.end()) {
        if (iter->second != nullptr) {
            // send cancel to etcd server
            iter->second->Close();
        }
        watchers_.erase(iter);
    }

    // send cancel to metastore client
    return WatchServiceActor::Cancel(from, watchId, msg);
}
}  // namespace functionsystem::meta_store