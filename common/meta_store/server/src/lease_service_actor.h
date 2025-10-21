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

#ifndef FUNCTION_MASTER_META_STORE_LEASE_SERVICE_ACTOR_H
#define FUNCTION_MASTER_META_STORE_LEASE_SERVICE_ACTOR_H

#include <thread>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "backup_actor.h"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_struct.h"
#include "proto/pb/message_pb.h"
#include "status/status.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
class LeaseServiceActor : public litebus::ActorBase, public MetaStoreHealthyObserver {
public:
    explicit LeaseServiceActor(const litebus::AID &kvServiceActor, const litebus::AID &backupActor = litebus::AID());

    LeaseServiceActor(const litebus::AID &kvServiceActor, const std::string &namePrefix);

    ~LeaseServiceActor() override;

    Status Start();

    Status Stop();

    Status Attach(const std::string &item, int64_t leaseID);

    virtual void ReceiveGrant(const litebus::AID &from, std::string &&name, std::string &&msg);
    ::grpc::Status LeaseGrant(const ::etcdserverpb::LeaseGrantRequest *request,
                              ::etcdserverpb::LeaseGrantResponse *response);

    virtual void ReceiveRevoke(const litebus::AID &from, std::string &&name, std::string &&msg);
    ::grpc::Status LeaseRevoke(const ::etcdserverpb::LeaseRevokeRequest *request,
                               ::etcdserverpb::LeaseRevokeResponse *response);

    virtual void ReceiveKeepAlive(const litebus::AID &from, std::string &&name, std::string &&msg);
    ::grpc::Status LeaseKeepAlive(const ::etcdserverpb::LeaseKeepAliveRequest *request,
                                  ::etcdserverpb::LeaseKeepAliveResponse *response);

    void OnHealthyStatus(const Status &status) override;

protected:
    void Init() override;
    Status healthyStatus_ = Status::OK();

private:
    void CheckpointScheduledLeases();

    bool Sync(const std::shared_ptr<GetResponse> &getResponse);

private:
    litebus::AID kvServiceActor_;

    litebus::AID backupActor_;

    bool running_;

    // | 2 bytes  | 5 bytes   | 1 byte  |
    // | memberID | timestamp | cnt     |
    int64_t index_{ time(nullptr) };

    std::unordered_map<int64_t, ::messages::Lease> leases_;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_LEASE_SERVICE_ACTOR_H
