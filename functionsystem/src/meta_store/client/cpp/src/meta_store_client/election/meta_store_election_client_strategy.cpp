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

#include "meta_store_election_client_strategy.h"

#include "async/asyncafter.hpp"
#include "meta_store_client/utils/etcd_util.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/random_number.h"
#include "meta_store_observer.h"

namespace functionsystem::meta_store {
MetaStoreElectionClientStrategy::MetaStoreElectionClientStrategy(const std::string &name, const std::string &address,
                                                                 const MetaStoreTimeoutOption &timeoutOption,
                                                                 const std::string &etcdTablePrefix)
    : ElectionClientStrategy(name, address, timeoutOption, etcdTablePrefix)
{
    electionServiceAid_ = std::make_shared<litebus::AID>("ElectionServiceActor", address_);

    auto backOff = [lower(timeoutOption_.operationRetryIntervalLowerBound),
                    upper(timeoutOption_.operationRetryIntervalUpperBound),
                    base(timeoutOption_.grpcTimeout * 1000)](int64_t attempt) {
        return GenerateRandomNumber(base + lower * attempt, base + upper * attempt);
    };
    campaignHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    leaderHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    resignHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    observeHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
}

void MetaStoreElectionClientStrategy::Init()
{
    YRLOG_INFO("Init election client actor({}).", address_);
    Receive("OnCampaign", &MetaStoreElectionClientStrategy::OnCampaign);
    Receive("OnLeader", &MetaStoreElectionClientStrategy::OnLeader);
    Receive("OnResign", &MetaStoreElectionClientStrategy::OnResign);
    Receive("OnObserve", &MetaStoreElectionClientStrategy::OnObserve);
}

void MetaStoreElectionClientStrategy::ReconnectSuccess()
{
    YRLOG_INFO("reconnect to meta-store success");
    pendingObservers_.clear();
    readyObservers_.clear();

    // re-observe
    for (const auto &observer : observers_) {
        CancelObserve(observer->GetObserveID());
        litebus::Async(GetAID(), &MetaStoreElectionClientStrategy::Observe, observer->GetName(),
                       observer->GetCallBack());
    }
    observers_.clear();
}

litebus::Future<CampaignResponse> MetaStoreElectionClientStrategy::Campaign(const std::string &name, int64_t lease,
                                                                            const std::string &value)
{
    // makeup Campaign request
    v3electionpb::CampaignRequest request;
    request.set_name(etcdTablePrefix_ + name);
    request.set_lease(lease);
    request.set_value(value);

    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    YRLOG_DEBUG("{}|begin to campaign, name: {}", req.requestid(), name);
    return campaignHelper_.Begin(req.requestid(), electionServiceAid_, "Campaign", req.SerializeAsString());
}

void MetaStoreElectionClientStrategy::OnCampaign(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse campaign MetaStoreResponse");

    v3electionpb::CampaignResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse campaign CampaignResponse: " + res.responseid());

    CampaignResponse ret;
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    YRLOG_DEBUG("{}|success to campaign, name is {}", res.responseid(), response.leader().name());
    ret.leader.name = TrimKeyPrefix(response.leader().name(), etcdTablePrefix_);
    ret.leader.key = TrimKeyPrefix(response.leader().key(), etcdTablePrefix_);
    ret.leader.rev = response.leader().rev();
    ret.leader.lease = response.leader().lease();
    campaignHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<LeaderResponse> MetaStoreElectionClientStrategy::Leader(const std::string &name)
{
    // makeup Leader request
    v3electionpb::LeaderRequest request;
    request.set_name(etcdTablePrefix_ + name);

    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    YRLOG_DEBUG("{}|begin to get leader, name: {}", req.requestid(), name);
    return leaderHelper_.Begin(req.requestid(), electionServiceAid_, "Leader", req.SerializeAsString());
}

void MetaStoreElectionClientStrategy::OnLeader(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Leader MetaStoreResponse");

    v3electionpb::LeaderResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Leader LeaderResponse: " + res.responseid());

    LeaderResponse ret;
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    YRLOG_DEBUG("{}|success to get leader, is {}:{}", res.responseid(), response.kv().key(), response.kv().value());
    ret.kv.first = TrimKeyPrefix(response.kv().key(), etcdTablePrefix_);
    ret.kv.second = response.kv().value();
    leaderHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<ResignResponse> MetaStoreElectionClientStrategy::Resign(const LeaderKey &leader)
{
    // makeup Resign request
    v3electionpb::ResignRequest request;
    request.mutable_leader()->set_name(etcdTablePrefix_ + leader.name);
    request.mutable_leader()->set_key(etcdTablePrefix_ + leader.key);
    request.mutable_leader()->set_rev(leader.rev);
    request.mutable_leader()->set_lease(leader.lease);

    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    YRLOG_DEBUG("{}|begin to resign, name: {}", req.requestid(), leader.name);
    return resignHelper_.Begin(req.requestid(), electionServiceAid_, "Resign", req.SerializeAsString());
}

void MetaStoreElectionClientStrategy::OnResign(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Resign MetaStoreResponse");

    v3electionpb::ResignResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Resign ResignResponse: " + res.responseid());

    ResignResponse ret;
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    YRLOG_DEBUG("{}|success to get resign", res.responseid());
    resignHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<std::shared_ptr<Observer>> MetaStoreElectionClientStrategy::Observe(
    const std::string &name, const std::function<void(LeaderResponse)> &callback)
{
    v3electionpb::LeaderRequest request;
    request.set_name(etcdTablePrefix_ + name);
    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    (void)observeHelper_.Begin(req.requestid(), electionServiceAid_, "Observe", req.SerializeAsString());

    YRLOG_DEBUG("{}|begin to observe, name: {}", req.requestid(), name);
    auto observer =
        std::make_shared<MetaStoreObserver>(name, callback, etcdTablePrefix_, [aid(GetAID())](uint64_t observeID) {
            litebus::Async(aid, &MetaStoreElectionClientStrategy::CancelObserve, observeID);
        });
    observers_.push_back(observer);
    pendingObservers_[req.requestid()] = observer;
    return observer;
}

void MetaStoreElectionClientStrategy::OnObserve(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Observe MetaStoreResponse");

    messages::MetaStore::ObserveResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Observe ObserveResponse: " + res.responseid());

    if (response.iscreate()) {
        observeHelper_.End(res.responseid(), true);
        OnObserveCreated(response, res.responseid(), from);
        return;
    }

    if (response.iscancel()) {
        YRLOG_INFO("{}|receive observer({}) canceled, msg: {}", res.responseid(), response.observeid(),
                   response.cancelmsg());
        observeHelper_.End(res.responseid(), true);
        OnObserveCancel(response.observeid());
        return;
    }

    YRLOG_INFO("{}|receive observe event, name: {}, observeID: {}", res.responseid(), response.name(),
               response.observeid());
    OnObserveEvent(response);
}

void MetaStoreElectionClientStrategy::OnObserveCreated(const messages::MetaStore::ObserveResponse &response,
                                                       const std::string &uuid, const litebus::AID &from)
{
    auto iter = pendingObservers_.find(uuid);
    if (iter == pendingObservers_.end()) {
        YRLOG_WARN("{}|receive invalid observe created event, name: {}", uuid, response.name());
        return;
    }

    YRLOG_INFO("{}|receive observe created event, name: {}, observeID: {}, from: {}", uuid, response.name(),
               response.observeid(), from.HashString());
    iter->second->SetObserveID(response.observeid());
    readyObservers_[response.observeid()] = iter->second;
    pendingObservers_.erase(uuid);
}

void MetaStoreElectionClientStrategy::OnObserveEvent(const messages::MetaStore::ObserveResponse &response)
{
    auto iter = readyObservers_.find(response.observeid());
    if (iter == readyObservers_.end()) {
        YRLOG_WARN("receive invalid observe event, observeID: {}", response.observeid());
        CancelObserve(response.observeid());
        return;
    }

    v3electionpb::LeaderResponse leader;
    RETURN_IF_TRUE(!leader.ParseFromString(response.responsemsg()), "failed to parse LeaderResponse");
    LeaderResponse ret;
    Transform(ret.header, leader.header());
    ret.kv.first = TrimKeyPrefix(leader.kv().key(), etcdTablePrefix_);
    ret.kv.second = leader.kv().value();
    YRLOG_INFO("receive observe event, {}:{}", ret.kv.first, ret.kv.second);
    iter->second->OnObserve(ret);
}

void MetaStoreElectionClientStrategy::CancelObserve(uint64_t observeID)
{
    messages::MetaStore::ObserveCancelRequest request;
    request.set_cancelobserveid(observeID);
    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    // send to the meta-store that handled this observe request directly
    (void)observeHelper_.Begin(req.requestid(), electionServiceAid_, "CancelObserve", req.SerializeAsString());
}

void MetaStoreElectionClientStrategy::OnObserveCancel(uint64_t observeID)
{
    for (auto observer = observers_.cbegin(); observer != observers_.cend(); observer++) {
        if (observer->get()->GetObserveID() == observeID) {
            if (!observer->get()->IsCanceled()) {
                // if observer is not canceled by client, don't clear and re-observe
                litebus::Async(GetAID(), &MetaStoreElectionClientStrategy::Observe, observer->get()->GetName(),
                               observer->get()->GetCallBack());
            } else {
                (void)observers_.erase(observer);
            }
            break;
        }
    }

    for (const auto &iter : pendingObservers_) {
        if (iter.second->GetObserveID() == observeID && iter.second->IsCanceled()) {
            (void)pendingObservers_.erase(iter.first);
            break;
        }
    }

    if (auto iter = readyObservers_.find(observeID); iter != readyObservers_.end() && iter->second->IsCanceled()) {
        (void)readyObservers_.erase(observeID);
    }
}

litebus::Future<bool> MetaStoreElectionClientStrategy::IsConnected()
{
    return true;
}

void MetaStoreElectionClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_DEBUG("election client update address from {} to {}", address_, address);
    address_ = address;
    electionServiceAid_->SetUrl(address);

    ReconnectSuccess();
}
}  // namespace functionsystem::meta_store