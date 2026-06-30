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

#include "election_service_passthrough_actor.h"

#include "async/defer.hpp"
#include "meta_store_client/utils/etcd_util.h"

namespace functionsystem::meta_store {
ElectionServicePassthroughActor::ElectionServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &client)
    : ActorBase("ElectionServiceActor"), etcdClient_(client)
{
}

void ElectionServicePassthroughActor::Init()
{
    YRLOG_INFO("Init election service actor");
    Receive("Campaign", &ElectionServicePassthroughActor::Campaign);
    Receive("Leader", &ElectionServicePassthroughActor::Leader);
    Receive("Resign", &ElectionServicePassthroughActor::Resign);
    Receive("Observe", &ElectionServicePassthroughActor::Observe);
    Receive("CancelObserve", &ElectionServicePassthroughActor::CancelObserve);
}

void ElectionServicePassthroughActor::Exited(const litebus::AID &from)
{
    YRLOG_WARN("client({}) disconnect", from.HashString());
    for (const auto &observerPair : observersByID_) {
        if (observerPair.second->client == from) {
            litebus::Async(GetAID(), &ElectionServicePassthroughActor::CancelObserveWithID, "", "client disconnect",
                           observerPair.first);
            break;
        }
    }
}

void ElectionServicePassthroughActor::Campaign(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Campaign MetaStore Response");

    if (healthyStatus_.IsError()) {
        CampaignResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call campaign: " + healthyStatus_.GetMessage());
        return OnCampaign(response, req.requestid(), from);
    }

    ::v3electionpb::CampaignRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   "failed to parse Campaign CampaignRequest; " + req.requestid());

    YRLOG_DEBUG("{}|receive campaign request, name: {}", req.requestid(), request.name());
    etcdClient_->Campaign(request.name(), request.lease(), request.value())
        .OnComplete(litebus::Defer(GetAID(), &ElectionServicePassthroughActor::OnCampaign, std::placeholders::_1,
                                   req.requestid(), from));
}

void ElectionServicePassthroughActor::OnCampaign(const litebus::Future<CampaignResponse> &response,
                                                 const std::string &id, const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to campaign, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "OnCampaign", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to campaign, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "OnCampaign", res.SerializeAsString());
        return;
    }

    // trans to v3electionpb::CampaignResponse
    ::v3electionpb::CampaignResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    ret.mutable_leader()->set_name(response.Get().leader.name);
    ret.mutable_leader()->set_key(response.Get().leader.key);
    ret.mutable_leader()->set_rev(response.Get().leader.rev);
    ret.mutable_leader()->set_lease(response.Get().leader.lease);
    res.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|send campaign response to {}", id, std::string(aid));
    Send(aid, "OnCampaign", res.SerializeAsString());
}

void ElectionServicePassthroughActor::Leader(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Leader MetaStoreResponse");

    if (healthyStatus_.IsError()) {
        LeaderResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallback] failed to call Leader: " + healthyStatus_.GetMessage());
        return OnLeader(response, req.requestid(), from);
    }

    ::v3electionpb::LeaderRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   "failed to parde Leader LeaderRequest: " + req.requestid());

    YRLOG_DEBUG("{}|receive leader request, name: {}", req.requestid(), request.name());
    etcdClient_->Leader(request.name())
        .OnComplete(litebus::Defer(GetAID(), &ElectionServicePassthroughActor::OnLeader, std::placeholders::_1,
                                   req.requestid(), from));
}

void ElectionServicePassthroughActor::OnLeader(const litebus::Future<LeaderResponse> &response, const std::string &id,
                                               const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to get leader, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "OnLeader", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to get leader, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "OnLeader", res.SerializeAsString());
        return;
    }

    // trans to v3electionpb::LeaderResponse
    ::v3electionpb::LeaderResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    ret.mutable_kv()->set_key(response.Get().kv.first);
    ret.mutable_kv()->set_value(response.Get().kv.second);
    res.set_responsemsg(ret.SerializeAsString());
    YRLOG_DEBUG("{}|send leader response to {}", id, std::string(aid));
    Send(aid, "OnLeader", res.SerializeAsString());
}

void ElectionServicePassthroughActor::Resign(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Resign MetaStoreResponse");

    if (healthyStatus_.IsError()) {
        ResignResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallback] failed to call resign: " + healthyStatus_.GetMessage());
        return OnResign(response, req.requestid(), from);
    }

    ::v3electionpb::ResignRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   "failed to parse Resign ResignRequest: " + req.requestid());

    YRLOG_DEBUG("{}|receive resign request, name: {}", req.requestid(), request.leader().name());
    LeaderKey key;
    key.name = request.leader().name();
    key.key = request.leader().key();
    key.rev = request.leader().rev();
    key.lease = request.leader().lease();
    etcdClient_->Resign(key).OnComplete(litebus::Defer(GetAID(), &ElectionServicePassthroughActor::OnResign,
                                                       std::placeholders::_1, req.requestid(), from));
}

void ElectionServicePassthroughActor::OnResign(const litebus::Future<ResignResponse> &response, const std::string &id,
                                               const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to resign, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "OnResign", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to resign, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "OnResign", res.SerializeAsString());
        return;
    }

    // trans to v3relectionpb::ResignResponse
    ::v3electionpb::ResignResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    res.set_responsemsg(ret.SerializeAsString());
    YRLOG_DEBUG("{}|send resign response to {}", id, std::string(aid));
    Send(aid, "OnResign", res.SerializeAsString());
}

void ElectionServicePassthroughActor::Observe(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Observe MetaStoreResponse");

    ::v3electionpb::LeaderRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   "failed to parse Observe LeaderRequest: " + req.requestid());

    YRLOG_DEBUG("{}|receive observe request, name: {}, observeID: {}", req.requestid(), request.name(), observeID_);
    auto record = std::make_shared<ObserveRecord>();
    record->client = from;
    record->observeID = observeID_++;
    record->key = request.name();
    observersByID_[record->observeID] = record;
    Link(from);

    // observed key already existed, or is waiting for observe response
    if (observersByKey_.find(request.name()) != observersByKey_.end()) {
        YRLOG_DEBUG("{}|key({}) is already observed, add client to map, observeID: {}, current leader: {}",
                    req.requestid(), record->key, record->observeID,
                    observersByKey_[request.name()].currentLeader.kv.second);
        SendObserveCreated(req.requestid(), record);
        if (!observersByKey_[request.name()].currentLeader.kv.first.empty()) {
            // already has an observer, send currentleader
            SendObserveEvent(request.name(), observersByKey_[request.name()].currentLeader, record);
        }
        return;
    }

    // add empty vector for request deduplication
    observersByKey_[request.name()] = {};
    etcdClient_
        ->Observe(record->key,
                  [key(record->key), aid(GetAID())](const LeaderResponse &leaderResponse) {
                      litebus::Async(aid, &ElectionServicePassthroughActor::ObserveEvent, key, leaderResponse);
                  })
        .OnComplete(litebus::Defer(GetAID(), &ElectionServicePassthroughActor::OnObserve, std::placeholders::_1, record,
                                   req.requestid()));
}

void ElectionServicePassthroughActor::OnObserve(const litebus::Future<std::shared_ptr<Observer>> &observer,
                                                const ElectionServicePassthroughActor::ObserveRecord::Ptr &record,
                                                const std::string &id)
{
    RETURN_IF_NULL(record);
    if (observer.IsError() || observer.Get() == nullptr) {
        YRLOG_WARN("{}|failed to observe key({})", id, record->key);
        return;
    }
    etcdObserverMap_[record->key] = observer.Get();
    SendObserveCreated(id, record);
}

void ElectionServicePassthroughActor::SendObserveCreated(
    const std::string &id, const std::shared_ptr<ElectionServicePassthroughActor::ObserveRecord> &record)
{
    observersByKey_[record->key].records.push_back(record);

    messages::MetaStoreResponse res;
    res.set_responseid(id);
    messages::MetaStore::ObserveResponse ret;
    ret.set_name(record->key);
    ret.set_observeid(record->observeID);
    ret.set_iscreate(true);
    res.set_responsemsg(ret.SerializeAsString());
    YRLOG_DEBUG("{}|send observe created response to {}", id, std::string(record->client));
    Send(record->client, "OnObserve", res.SerializeAsString());
}

void ElectionServicePassthroughActor::CancelObserve(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse CancelObserve MetaStoreResponse");

    messages::MetaStore::ObserveCancelRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   "failed to parse CancelObserve ObserveCancelRequest: " + req.requestid());

    YRLOG_DEBUG("{}|receive cancel observer({})", req.requestid(), request.cancelobserveid());
    CancelObserveWithID(req.requestid(), "by user", request.cancelobserveid());
}

void ElectionServicePassthroughActor::ObserveEvent(const std::string &key, const LeaderResponse &leaderResponse)
{
    YRLOG_DEBUG("received observe event for key {}:{}, send to {} client", leaderResponse.kv.first,
                leaderResponse.kv.second, observersByKey_[key].records.size());
    observersByKey_[key].currentLeader = leaderResponse;  // update
    for (const auto &record : observersByKey_[key].records) {
        SendObserveEvent(key, leaderResponse, record);
    }
}

void ElectionServicePassthroughActor::SendObserveEvent(const std::string &key, const LeaderResponse &leaderResponse,
                                                       ObserveRecord::Ptr record)
{
    messages::MetaStoreResponse res;
    res.set_responseid("");

    messages::MetaStore::ObserveResponse observeResponse;
    v3electionpb::LeaderResponse ret;
    Transform(ret.mutable_header(), leaderResponse.header);
    ret.mutable_kv()->set_key(leaderResponse.kv.first);
    ret.mutable_kv()->set_value(leaderResponse.kv.second);
    observeResponse.set_responsemsg(ret.SerializeAsString());

    observeResponse.set_name(key);
    observeResponse.set_observeid(record->observeID);
    res.set_responsemsg(observeResponse.SerializeAsString());

    YRLOG_DEBUG("send observe event response to {}", std::string(record->client));
    Send(record->client, "OnObserve", res.SerializeAsString());
}

void ElectionServicePassthroughActor::CancelObserveWithID(const std::string &requestID, const std::string &msg,
                                                          uint64_t observeID)
{
    if (observersByID_.find(observeID) == observersByID_.end()) {
        YRLOG_WARN("{}|try to cancel non-exist observer({})", requestID, observeID);
        return;
    }

    // copy
    std::string key = observersByID_[observeID]->key;
    litebus::AID client = observersByID_[observeID]->client;
    observersByID_.erase(observeID);

    for (auto record = observersByKey_[key].records.cbegin(); record != observersByKey_[key].records.cend();) {
        if (record->get()->observeID == observeID) {
            (void)observersByKey_[key].records.erase(record);
            break;
        } else {
            ++record;
        }
    }

    if (observersByKey_[key].records.empty()) {
        (void)observersByKey_.erase(key);
        // cancel etcd observe
        if (etcdObserverMap_[key]) {
            etcdObserverMap_[key]->Shutdown();
            etcdObserverMap_.erase(key);
        }
    }

    messages::MetaStoreResponse res;
    res.set_responseid(requestID);
    messages::MetaStore::ObserveResponse ret;
    ret.set_name(key);
    ret.set_observeid(observeID);
    ret.set_iscancel(true);
    ret.set_cancelmsg(msg);
    res.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|send observe cancel response to {}, msg, {}", requestID, std::string(client), msg);
    Send(client, "OnObserve", res.SerializeAsString());
}

void ElectionServicePassthroughActor::OnHealthyStatus(const Status &status)
{
    YRLOG_DEBUG("ElectionServicePassthroughActor health status changes to healthy({})", status.IsOk());
    healthyStatus_ = status;
}

}  // namespace functionsystem::meta_store
