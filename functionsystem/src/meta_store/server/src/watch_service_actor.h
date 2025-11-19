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

#ifndef FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ACTOR_H
#define FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>

#include "common/proto/pb/message_pb.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "watch_service_async_push_actor.h"

namespace functionsystem::meta_store {
class WatchServiceActor : public litebus::ActorBase {
public:
    explicit WatchServiceActor(const std::string &name);

    ~WatchServiceActor() override = default;

    virtual litebus::Future<Status> Create(const litebus::AID &from, const std::string &uuid,
                          std::shared_ptr<::etcdserverpb::WatchCreateRequest> request);

    void ReceiveCancel(const litebus::AID &from, std::string &&name, std::string &&msg);

    virtual bool Cancel(const litebus::AID &from, int64_t watchId, const std::string &msg);

    virtual void OnPut(const ::mvccpb::KeyValue &kv, const ::mvccpb::KeyValue &prevKv);

    virtual void OnDeleteList(std::shared_ptr<std::vector<::mvccpb::KeyValue>> kvs);

    virtual void OnDelete(const ::mvccpb::KeyValue &prevKv);

    virtual litebus::Future<std::shared_ptr<::etcdserverpb::WatchResponse>> CreateWatch(
        const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request);

    void SendResponse(const litebus::AID &from, std::string method, const messages::MetaStoreResponse &resp);

protected:
    using WatchClientInfo = std::pair<litebus::AID, int64_t>;
    struct Observer {
        using Ptr = std::shared_ptr<Observer>;
        std::shared_ptr<WatchClientInfo> clientInfo;  // client, watchId
        std::shared_ptr<::etcdserverpb::WatchCreateRequest> request;
    };

    std::unordered_map<int64_t, Observer::Ptr> strictObserversById_;
    std::unordered_map<std::string, std::vector<Observer::Ptr>> strictObserversByKey_;

    void Init() override;

    void Finalize() override;

    void Exited(const litebus::AID &aid) override;

    int64_t index_{ 0 };  // +1 when create a new Watcher.

    std::shared_ptr<WatchServiceAsyncPushActor> asyncPushActor_;

protected:
    struct RangeObserverCache {
        RangeObserverCache() = default;
        explicit RangeObserverCache(const std::string &prefix);
        void UpdateResponseWithCache(UnsyncedEvents::Ptr response) const;
        void AddObserver(Observer::Ptr observer);

        std::string keyPrefix;
        std::string keyPrefixEnd;
        std::vector<std::shared_ptr<WatchClientInfo>> to;
        std::vector<std::shared_ptr<WatchClientInfo>> toWithPrevKv;
    };

    std::unordered_map<std::string, RangeObserverCache> rangeObserverCaches_;

    UnsyncedEvents::Ptr BuildUnsyncedEventsForPut(const mvccpb::KeyValue &kv, const mvccpb::KeyValue &prevKv) const;
    UnsyncedEvents::Ptr BuildUnsyncedEventsForDelete(const mvccpb::KeyValue &prevKv) const;

    void CheckIfValidRangeCacheAndUpdateResponse(const mvccpb::KeyValue &kv, UnsyncedEvents::Ptr response);

    std::shared_ptr<::etcdserverpb::WatchResponse> CreateInternal(
        const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request);

    // only for test
    std::unordered_map<litebus::AID, uint32_t> GetWatchCount()
    {
        return watchKeyCount_;
    }

    std::unordered_map<litebus::AID, uint32_t> watchKeyCount_;

    bool IsRangeObserver(std::shared_ptr<::etcdserverpb::WatchCreateRequest> request);

    litebus::AID RemoveRangeObserverById(int64_t watchId, std::vector<std::shared_ptr<WatchClientInfo>> &vec,
                                         const litebus::AID &from);

    int64_t RemoveRangeObserverByAid(const litebus::AID &aid, std::vector<std::shared_ptr<WatchClientInfo>> &vec);

    bool RemoveObserverById(int64_t watchId, const litebus::AID &from = litebus::AID());

    void RemoveStrictObserverById(const std::string &key, int64_t watchId);

    void AddObserverToResponse(UnsyncedEvents::Ptr response, Observer::Ptr observer);

protected:
    litebus::Future<bool> AddToUnsyncedEvents(UnsyncedEvents::Ptr response);

    static bool IsPrefix(const std::string &key, const RangeObserverCache &cache);

    static void SetPrevKv(const mvccpb::KeyValue &prevKv, std::shared_ptr<mvccpb::Event> event);
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_WATCH_SERVICE_ACTOR_H
