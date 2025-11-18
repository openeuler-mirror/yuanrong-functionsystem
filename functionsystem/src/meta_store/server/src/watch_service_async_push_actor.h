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

#ifndef FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ASYNC_PUSH_ACTOR_H
#define FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ASYNC_PUSH_ACTOR_H

#include <actor/actor.hpp>
#include <unordered_map>

#include "common/utils/meta_store_kv_operation.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
struct UnsyncedEvents {
    using Ptr = std::shared_ptr<UnsyncedEvents>;
    std::vector<std::shared_ptr<std::pair<litebus::AID, int64_t>>> to;
    std::shared_ptr<::mvccpb::Event> event;

    std::vector<std::shared_ptr<std::pair<litebus::AID, int64_t>>> toWithPrevKv;
    std::shared_ptr<::mvccpb::Event> eventWithPrevKv;
};

class WatchServiceAsyncPushActor : public litebus::ActorBase {
public:
    explicit WatchServiceAsyncPushActor(const std::string &name) : ActorBase(name)
    {
    }

    ~WatchServiceAsyncPushActor() override = default;

    bool AddToUnsyncedEvents(UnsyncedEvents::Ptr response);

    bool PushGroupedEvents();

private:
    void AddEventsForWatchId(const litebus::AID &client, int64_t watchId, std::shared_ptr<::mvccpb::Event> events);
    void RemoveStaleEventsForSameKey(std::vector<std::shared_ptr<::mvccpb::Event>> &events) const;
    void CheckAndSendEventResponse(const litebus::AID &client, int64_t watchId,
                                   std::vector<std::shared_ptr<::mvccpb::Event>> &events);
    void LogPushedEventCount();

private:
    // unsyncedEvents_[watchid] = (client, events)
    std::unordered_map<int64_t, std::pair<litebus::AID, std::vector<std::shared_ptr<::mvccpb::Event>>>> unsyncedEvents_;
    bool aboutToPush_ = false;
    static const uint64_t logEventCountThreshold_ = 1000;
    uint64_t pushEventCount_ = 0;

    size_t maxEventCount_ = 0;
    static const size_t pushEventThreshold_ = 200;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ASYNC_PUSH_ACTOR_H
