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

#ifndef COMMON_META_STORE_CLIENT_ELECTION_ELECTION_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_ELECTION_ELECTION_CLIENT_STRATEGY_H

#include "actor/actor.hpp"
#include "meta_store_client/meta_store_struct.h"
#include "etcd/server/etcdserver/api/v3election/v3electionpb/v3election.grpc.pb.h"
#include "observer.h"

namespace functionsystem::meta_store {

class ElectionClientStrategy : public litebus::ActorBase {
public:
    ElectionClientStrategy(const std::string &name, const std::string &address,
                           const MetaStoreTimeoutOption &timeoutOption, const std::string &etcdTablePrefix = "");

    ~ElectionClientStrategy() override = default;

    virtual litebus::Future<CampaignResponse> Campaign(const std::string &name, int64_t lease,
                                                       const std::string &value) = 0;

    virtual litebus::Future<LeaderResponse> Leader(const std::string &name) = 0;

    virtual litebus::Future<ResignResponse> Resign(const LeaderKey &leader) = 0;

    virtual litebus::Future<std::shared_ptr<Observer>> Observe(const std::string &name,
                                                               const std::function<void(LeaderResponse)> &callback) = 0;

    virtual litebus::Future<bool> IsConnected() = 0;

    virtual void OnAddressUpdated(const std::string &address) = 0;

    void OnHealthyStatus(const Status &status);

protected:
    std::string address_;
    std::string etcdTablePrefix_;
    MetaStoreTimeoutOption timeoutOption_;
    Status healthyStatus_ = Status::OK();
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_ELECTION_ELECTION_CLIENT_STRATEGY_H
