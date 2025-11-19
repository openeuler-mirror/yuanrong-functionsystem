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

#include "watch_service_async_push_actor.h"

#include <async/async.hpp>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "meta_store_common.h"

namespace functionsystem::meta_store {
void WatchServiceAsyncPushActor::AddEventsForWatchId(const litebus::AID &client, int64_t watchId,
                                                     std::shared_ptr<::mvccpb::Event> event)
{
    if (unsyncedEvents_.find(watchId) == unsyncedEvents_.end()) {
        unsyncedEvents_[watchId].first = client;
    }
    auto &dst = unsyncedEvents_[watchId].second;
    dst.emplace_back(event);

    if (dst.size() > maxEventCount_) {
        maxEventCount_ = dst.size();
    }
}

bool WatchServiceAsyncPushActor::AddToUnsyncedEvents(UnsyncedEvents::Ptr response)
{
    for (auto &iter : response->to) {
        AddEventsForWatchId(iter->first, iter->second, response->event);
    }

    for (auto &iter : response->toWithPrevKv) {
        AddEventsForWatchId(iter->first, iter->second, response->eventWithPrevKv);
    }

    if (maxEventCount_ > pushEventThreshold_) {
        auto backup = aboutToPush_;
        aboutToPush_ = true;
        PushGroupedEvents();
        aboutToPush_ = backup;
    } else if (!aboutToPush_) {
        aboutToPush_ = true;
        litebus::Async(GetAID(), &WatchServiceAsyncPushActor::PushGroupedEvents);
    }
    return true;
}

void WatchServiceAsyncPushActor::LogPushedEventCount()
{
    if ((++pushEventCount_) % logEventCountThreshold_ == 0) {
        YRLOG_INFO("Total pushed events: {}", pushEventCount_);
    }
}

void WatchServiceAsyncPushActor::RemoveStaleEventsForSameKey(
    std::vector<std::shared_ptr<::mvccpb::Event>> &events) const
{
    std::unordered_set<std::string> hash;
    hash.reserve(events.size());
    // reserve the newest events and remove the old events
    for (int i = static_cast<int>(events.size()) - 1; i >= 0; --i) {
        const auto &key = events[i]->kv().key();
        auto res = hash.emplace(key);
        if (!res.second) {  // already exists
            events[i] = nullptr;
        }
    }
}

void WatchServiceAsyncPushActor::CheckAndSendEventResponse(const litebus::AID &client, int64_t watchId,
                                                           std::vector<std::shared_ptr<::mvccpb::Event>> &events)
{
    static std::string responseString;
    static std::string groupedResponseString;
    ::etcdserverpb::WatchResponse response;
    RemoveStaleEventsForSameKey(events);
    int64_t currentRevision = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        if (events[i] != nullptr) {
            auto event = *events[i];
            currentRevision = std::max(event.kv().mod_revision(), currentRevision);
            response.mutable_events()->Add(std::move(event));
            events[i] = nullptr;
        }
    }
    if (response.events_size() > 0) {
        response.mutable_header()->set_cluster_id(META_STORE_CLUSTER_ID);
        response.mutable_header()->set_revision(currentRevision);
        response.set_watch_id(watchId);
        response.SerializeToString(&responseString);

        messages::MetaStoreResponse groupedResponse;
        groupedResponse.set_responsemsg(std::move(responseString));
        groupedResponse.SerializeToString(&groupedResponseString);

        Send(client, "OnWatch", std::move(groupedResponseString));
        responseString.clear();
        groupedResponseString.clear();
        LogPushedEventCount();
    }
}

bool WatchServiceAsyncPushActor::PushGroupedEvents()
{
    if (!aboutToPush_) {
        return false;
    }
    for (auto &[watchId, watch] : unsyncedEvents_) {
        CheckAndSendEventResponse(watch.first, watchId, watch.second);
        watch.second.clear();  // clear event ptrs; reserve client and watch ids
    }
    unsyncedEvents_.clear();
    maxEventCount_ = 0;
    aboutToPush_ = false;
    return true;
}
}  // namespace functionsystem::meta_store
