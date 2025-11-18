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

#include "watch_service_actor.h"

#include <async/async.hpp>
#include <async/defer.hpp>

#include "common/logs/logging.h"
#include "meta_store_client/utils/string_util.h"
#include "meta_store_common.h"

namespace functionsystem::meta_store {
WatchServiceActor::RangeObserverCache::RangeObserverCache(const std::string &prefix)
    : keyPrefix(prefix), keyPrefixEnd(StringPlusOne(prefix))
{
}

void WatchServiceActor::RangeObserverCache::UpdateResponseWithCache(UnsyncedEvents::Ptr response) const
{
    response->to.reserve(response->to.size() + to.size());
    response->to.insert(response->to.end(), to.begin(), to.end());
    response->toWithPrevKv.reserve(response->toWithPrevKv.size() + toWithPrevKv.size());
    response->toWithPrevKv.insert(response->toWithPrevKv.end(), toWithPrevKv.begin(), toWithPrevKv.end());
}

void WatchServiceActor::RangeObserverCache::AddObserver(Observer::Ptr observer)
{
    if (observer->request->prev_kv()) {
        toWithPrevKv.emplace_back(observer->clientInfo);
    } else {
        to.emplace_back(observer->clientInfo);
    }
}

WatchServiceActor::WatchServiceActor(const std::string &name) : ActorBase(name)
{
    asyncPushActor_ = std::make_shared<WatchServiceAsyncPushActor>(
        "WatchServiceAsyncPushActor-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
}

void WatchServiceActor::Init()
{
    (void)litebus::Spawn(asyncPushActor_);
    Receive("Cancel", &WatchServiceActor::ReceiveCancel);
}

void WatchServiceActor::Finalize()
{
    if (asyncPushActor_ != nullptr) {
        litebus::Terminate(asyncPushActor_->GetAID());
        litebus::Await(asyncPushActor_->GetAID());
    }
}

void WatchServiceActor::Exited(const litebus::AID &aid)
{
    YRLOG_DEBUG("start exit for client {}", aid.HashString());
    for (auto it = strictObserversById_.begin(); it != strictObserversById_.end();) {
        auto &observer = it->second;
        if (observer->clientInfo->first == aid) {
            auto watchId = observer->clientInfo->second;
            auto &key = observer->request->key();
            YRLOG_WARN("strict client({}) disconnect, watchid {}", aid.HashString(), watchId);
            RemoveStrictObserverById(key, watchId);
            litebus::Async(GetAID(), &WatchServiceActor::Cancel, aid, watchId, "client disconnected");
            it = strictObserversById_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &p : rangeObserverCaches_) {
        int64_t watchId = -1;
        if (watchId = RemoveRangeObserverByAid(aid, p.second.to); watchId != -1) {
            litebus::Async(GetAID(), &WatchServiceActor::Cancel, aid, watchId, "client disconnected");
        }
        if (watchId = RemoveRangeObserverByAid(aid, p.second.toWithPrevKv); watchId != -1) {
            litebus::Async(GetAID(), &WatchServiceActor::Cancel, aid, watchId, "client disconnected");
        }
    }
}

void WatchServiceActor::SendResponse(const litebus::AID &from, std::string method,
                                     const messages::MetaStoreResponse &resp)
{
    Send(from, std::move(method), resp.SerializeAsString());
}

litebus::AID WatchServiceActor::RemoveRangeObserverById(int64_t watchId,
                                                        std::vector<std::shared_ptr<WatchClientInfo>> &vec,
                                                        const litebus::AID &from)
{
    auto it = std::find_if(vec.begin(), vec.end(), [watchId](const std::shared_ptr<WatchClientInfo> &clientInfo) {
        return (watchId == clientInfo->second);
    });
    if (it != vec.end()) {
        auto aid = (*it)->first;
        if (from.OK() && aid.HashString() != from.HashString()) {
            YRLOG_WARN("try to remove observer, client({}), watchid {}, from different client {}, ignore",
                       aid.HashString(), watchId, from.HashString());
            return litebus::AID();
        }
        YRLOG_WARN("remove observer, client({}), watchid {}", aid.HashString(), watchId);
        std::swap(*it, vec.back());
        vec.pop_back();
        return aid;
    }
    return litebus::AID();
}

int64_t WatchServiceActor::RemoveRangeObserverByAid(const litebus::AID &aid,
                                                    std::vector<std::shared_ptr<WatchClientInfo>> &vec)
{
    auto it = std::find_if(vec.begin(), vec.end(), [&aid](const std::shared_ptr<WatchClientInfo> &clientInfo) {
        return (aid == clientInfo->first);
    });
    if (it != vec.end()) {
        auto watchId = (*it)->second;
        YRLOG_WARN("remove observer, client({}), watchid {}", aid.HashString(), watchId);
        std::swap(*it, vec.back());
        vec.pop_back();
        return watchId;
    }
    return -1;
}

bool WatchServiceActor::RemoveObserverById(int64_t watchId, const litebus::AID &from)
{
    if (auto it = strictObserversById_.find(watchId); it != strictObserversById_.end()) {
        auto &observer = it->second;
        auto &client = observer->clientInfo->first;
        auto watchId = observer->clientInfo->second;
        auto &key = observer->request->key();
        if (from.OK() && from.HashString() != client.HashString()) {
            YRLOG_WARN("cancel strict client({}), watchid {}, from different client({}), ignore", client.HashString(),
                       watchId, from.HashString());
            return false;
        }

        YRLOG_WARN("cancel strict client({}), watchid {}", client.HashString(), watchId);
        RemoveStrictObserverById(key, watchId);
        strictObserversById_.erase(it);
        return true;  // if matched, do not need to iterate range observers
    }

    for (auto &p : rangeObserverCaches_) {
        litebus::AID aid;
        if (aid = RemoveRangeObserverById(watchId, p.second.to, from); aid.OK()) {
            return true;
        }
        if (aid = RemoveRangeObserverById(watchId, p.second.toWithPrevKv, from); aid.OK()) {
            return true;
        }
    }
    return false;
}

void WatchServiceActor::RemoveStrictObserverById(const std::string &key, int64_t watchId)
{
    if (auto iter = strictObserversByKey_.find(key); iter != strictObserversByKey_.end()) {
        auto &vec = iter->second;
        for (auto &obs : vec) {
            if (obs->clientInfo->second == watchId) {
                std::swap(obs, vec.back());  // swap the target element to the back
                vec.pop_back();              // erase
                break;
            }
        }
        if (vec.empty()) {
            strictObserversByKey_.erase(iter);
        }
    }
}

bool WatchServiceActor::IsRangeObserver(std::shared_ptr<::etcdserverpb::WatchCreateRequest> request)
{
    return !request->range_end().empty();
}

std::shared_ptr<::etcdserverpb::WatchResponse> WatchServiceActor::CreateInternal(
    const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request)
{
    bool isRangeObserver = IsRangeObserver(request);
    YRLOG_DEBUG("start process create for client {}, key: {}, watch id: {}, is range: {}", from.HashString(),
                request->key(), index_, isRangeObserver);
    auto observer = std::make_shared<Observer>();
    observer->clientInfo = std::make_shared<WatchClientInfo>(from, index_);
    observer->request = request;
    if (isRangeObserver) {
        rangeObserverCaches_.emplace(request->key(), RangeObserverCache(request->key()));
        auto &cache = rangeObserverCaches_[request->key()];
        cache.AddObserver(observer);
        YRLOG_DEBUG("update range cache for {}, watcher size: ({}, {})", request->key(), cache.to.size(),
                    cache.toWithPrevKv.size());
    } else {
        strictObserversById_.emplace(index_, observer);
        strictObserversByKey_[request->key()].emplace_back(observer);
    }

    auto response = std::make_shared<::etcdserverpb::WatchResponse>();
    response->set_watch_id(index_);
    response->set_created(true);

    index_++;  // next watch id
    if (watchKeyCount_.find(from) == watchKeyCount_.end()) {
        Link(from);  // create link
    }
    watchKeyCount_[from]++;
    return response;
}

litebus::Future<Status> WatchServiceActor::Create(const litebus::AID &from, const std::string &uuid,
                                                  std::shared_ptr<::etcdserverpb::WatchCreateRequest> request)
{
    return CreateWatch(from, request)
        .Then([from, aid(GetAID()), uuid](const std::shared_ptr<::etcdserverpb::WatchResponse> &response) -> Status {
            ASSERT_IF_NULL(response);
            messages::MetaStoreResponse res;
            res.set_responseid(uuid);
            res.set_responsemsg(response->SerializeAsString());
            YRLOG_DEBUG("{}|success to create watch for key", uuid);
            litebus::Async(aid, &WatchServiceActor::SendResponse, from, "OnWatch", res);
            return Status::OK();
        });
}

void WatchServiceActor::ReceiveCancel(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    req.ParseFromString(msg);

    etcdserverpb::WatchRequest request;
    request.ParseFromString(req.requestmsg());
    auto watchId = request.cancel_request().watch_id();
    if (!RemoveObserverById(watchId, from)) {
        YRLOG_WARN("failed to cancel watcher({}) from client({})", watchId, from.HashString());
        return;
    }
    Cancel(from, watchId, "by user");
}

bool WatchServiceActor::Cancel(const litebus::AID &from, int64_t watchId, const std::string &msg)
{
    YRLOG_DEBUG("start process cancel for client {}, watchid: {}", from.HashString(), watchId);

    ::etcdserverpb::WatchResponse response;
    response.set_watch_id(watchId);
    response.set_cancel_reason(msg);
    response.set_canceled(true);
    messages::MetaStoreResponse res;
    res.set_responsemsg(response.SerializeAsString());
    // ensure previous events are pushed before cancel response
    (void)litebus::Async(asyncPushActor_->GetAID(), &WatchServiceAsyncPushActor::PushGroupedEvents)
        .OnComplete(litebus::Defer(GetAID(), &WatchServiceActor::SendResponse, from, "OnWatch", res));

    if (watchKeyCount_.find(from) != watchKeyCount_.end() && --watchKeyCount_[from] == 0) {
        watchKeyCount_.erase(from);
    }
    return true;
}

bool WatchServiceActor::IsPrefix(const std::string &key, const RangeObserverCache &cache)
{
    return (key >= cache.keyPrefix && key < cache.keyPrefixEnd);
}

UnsyncedEvents::Ptr WatchServiceActor::BuildUnsyncedEventsForPut(const mvccpb::KeyValue &kv,
                                                                 const mvccpb::KeyValue &prevKv) const
{
    auto response = std::make_shared<UnsyncedEvents>();
    auto event = std::make_shared<mvccpb::Event>();
    event->set_type(::mvccpb::Event_EventType::Event_EventType_PUT);
    *event->mutable_kv() = kv;

    auto eventWithPrevKv = std::make_shared<mvccpb::Event>();
    eventWithPrevKv->set_type(::mvccpb::Event_EventType::Event_EventType_PUT);
    *eventWithPrevKv->mutable_kv() = kv;
    SetPrevKv(prevKv, eventWithPrevKv);

    response->event = event;
    response->eventWithPrevKv = eventWithPrevKv;
    return response;
}

UnsyncedEvents::Ptr WatchServiceActor::BuildUnsyncedEventsForDelete(const mvccpb::KeyValue &prevKv) const
{
    auto response = std::make_shared<UnsyncedEvents>();
    auto event = std::make_shared<mvccpb::Event>();
    event->set_type(::mvccpb::Event_EventType::Event_EventType_DELETE);
    event->mutable_kv()->set_key(prevKv.key());
    event->mutable_kv()->set_mod_revision(prevKv.mod_revision() + 1);

    auto eventWithPrevKv = std::make_shared<mvccpb::Event>();
    eventWithPrevKv->set_type(::mvccpb::Event_EventType::Event_EventType_DELETE);
    eventWithPrevKv->mutable_kv()->set_key(prevKv.key());
    SetPrevKv(prevKv, eventWithPrevKv);

    response->event = event;
    response->eventWithPrevKv = eventWithPrevKv;
    return response;
}

void WatchServiceActor::CheckIfValidRangeCacheAndUpdateResponse(const mvccpb::KeyValue &kv,
                                                                UnsyncedEvents::Ptr response)
{
    for (const auto &cache : rangeObserverCaches_) {
        if (IsPrefix(kv.key(), cache.second)) {
            YRLOG_DEBUG("Hit range cache for prefix {}, watcher size: ({}, {})", cache.second.keyPrefix,
                        cache.second.to.size(), cache.second.toWithPrevKv.size());
            cache.second.UpdateResponseWithCache(response);
        }
    }
}

void WatchServiceActor::AddObserverToResponse(UnsyncedEvents::Ptr response, Observer::Ptr observer)
{
    if (observer->request->prev_kv()) {
        response->toWithPrevKv.emplace_back(observer->clientInfo);
    } else {
        response->to.emplace_back(observer->clientInfo);
    }
}

void WatchServiceActor::OnPut(const mvccpb::KeyValue &kv, const mvccpb::KeyValue &prevKv)
{
    auto response = BuildUnsyncedEventsForPut(kv, prevKv);
    CheckIfValidRangeCacheAndUpdateResponse(kv, response);

    if (auto iter = strictObserversByKey_.find(kv.key()); iter != strictObserversByKey_.end()) {
        YRLOG_DEBUG("find {} strict observers for key {}", iter->second.size(), kv.key());
        for (auto &obs : iter->second) {
            AddObserverToResponse(response, obs);
        }
    }
    (void)AddToUnsyncedEvents(response);
}

void WatchServiceActor::OnDeleteList(std::shared_ptr<std::vector<::mvccpb::KeyValue>> kvs)
{
    if (kvs == nullptr) {
        return;
    }
    for (const auto &preKv : *kvs) {
        OnDelete(preKv);
    }
}

void WatchServiceActor::OnDelete(const mvccpb::KeyValue &prevKv)
{
    auto response = BuildUnsyncedEventsForDelete(prevKv);
    CheckIfValidRangeCacheAndUpdateResponse(prevKv, response);

    if (auto iter = strictObserversByKey_.find(prevKv.key()); iter != strictObserversByKey_.end()) {
        YRLOG_DEBUG("find {} strict observers for key {}", iter->second.size(), prevKv.key());
        for (auto &obs : iter->second) {
            AddObserverToResponse(response, obs);
        }
    }
    (void)AddToUnsyncedEvents(response);
}

void WatchServiceActor::SetPrevKv(const mvccpb::KeyValue &prevKv, std::shared_ptr<mvccpb::Event> event)
{
    mvccpb::KeyValue *mutablePrevKv = event->mutable_prev_kv();
    *mutablePrevKv = prevKv;
}

litebus::Future<bool> WatchServiceActor::AddToUnsyncedEvents(UnsyncedEvents::Ptr response)
{
    bool noEvents = (response->to.empty() || response->event == nullptr);
    bool noEventsWithPrevKv = (response->toWithPrevKv.empty() || response->eventWithPrevKv == nullptr);
    if (noEvents && noEventsWithPrevKv) {
        return false;
    }
    return litebus::Async(asyncPushActor_->GetAID(), &WatchServiceAsyncPushActor::AddToUnsyncedEvents, response);
}

// will be overridden by child
litebus::Future<std::shared_ptr<::etcdserverpb::WatchResponse>> WatchServiceActor::CreateWatch(
    const litebus::AID &from, std::shared_ptr<::etcdserverpb::WatchCreateRequest> request)
{
    return CreateInternal(from, request);
}
}  // namespace functionsystem::meta_store
