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

#ifndef FUNCTION_MASTER_META_STORE_ELECTION_SERVICE_ACTOR_H
#define FUNCTION_MASTER_META_STORE_ELECTION_SERVICE_ACTOR_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {

class ElectionServicePassthroughActor : public litebus::ActorBase, public MetaStoreHealthyObserver {
public:
    explicit ElectionServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &client);
    ~ElectionServicePassthroughActor() override = default;

    void Campaign(const litebus::AID &from, std::string &&, std::string &&msg);
    void Leader(const litebus::AID &from, std::string &&, std::string &&msg);
    void Resign(const litebus::AID &from, std::string &&, std::string &&msg);
    void Observe(const litebus::AID &from, std::string &&, std::string &&msg);
    void CancelObserve(const litebus::AID &from, std::string &&, std::string &&msg);

    void OnHealthyStatus(const Status &status) override;

protected:
    void Init() override;
    void Exited(const litebus::AID &from) override;

private:
    struct ObserveRecord {
        using Ptr = std::shared_ptr<ObserveRecord>;

        litebus::AID client;
        uint64_t observeID;
        std::string key;
    };

    struct ObserveRecords {
        LeaderResponse currentLeader;
        std::vector<ObserveRecord::Ptr> records;
    };

    void OnCampaign(const litebus::Future<CampaignResponse> &response, const std::string &id, const litebus::AID &aid);
    void OnLeader(const litebus::Future<LeaderResponse> &response, const std::string &id, const litebus::AID &aid);
    void OnResign(const litebus::Future<ResignResponse> &response, const std::string &id, const litebus::AID &aid);
    void OnObserve(const litebus::Future<std::shared_ptr<Observer>> &observer, const ObserveRecord::Ptr &record,
                   const std::string &id);

    void ObserveEvent(const std::string &key, const LeaderResponse &leaderResponse);

    std::shared_ptr<MetaStoreClient> etcdClient_;
    std::unordered_map<std::string, ObserveRecords> observersByKey_;
    std::unordered_map<uint64_t, ObserveRecord::Ptr> observersByID_;
    std::unordered_map<std::string, std::shared_ptr<Observer>> etcdObserverMap_;

    uint64_t observeID_ = 0;

    void SendObserveCreated(const std::string &id,
                            const std::shared_ptr<ElectionServicePassthroughActor::ObserveRecord> &record);
    void CancelObserveWithID(const std::string &requestID, const std::string &msg, uint64_t observeID);
    void SendObserveEvent(const std::string &key, const LeaderResponse &leaderResponse, ObserveRecord::Ptr record);

    Status healthyStatus_ = Status::OK();
};

}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_ELECTION_SERVICE_ACTOR_H
