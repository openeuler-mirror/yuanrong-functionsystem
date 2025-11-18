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
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
class LeaseServiceActor : public litebus::ActorBase,
                          public MetaStoreHealthyObserver,
                          public std::enable_shared_from_this<LeaseServiceActor> {
public:
    explicit LeaseServiceActor(const litebus::AID &kvServiceActor, bool needExplore = false,
                               const litebus::AID &backupActor = litebus::AID());

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

    void InitExplorer();

protected:
    void Init() override;
    Status healthyStatus_ = Status::OK();

private:
    void CheckpointScheduledLeases();

    void UpdateLeaseExpire();

    bool Sync(const std::shared_ptr<GetResponse> &getResponse);

    void HandleLeaseEvents(const std::vector<WatchEvent> &events, bool synced);

    litebus::Future<SyncResult> LeaseSyncer(const std::shared_ptr<GetResponse> &getResponse);

private:
    bool UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    struct Member {
        explorer::LeaderInfo leaderInfo;
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<LeaseServiceActor> &actor)
            : member_(member), actor_(actor){};
        ~Business() override = default;

        virtual void ReceiveGrant(const litebus::AID &from, const messages::MetaStoreRequest &req) = 0;

        virtual void ReceiveRevoke(const litebus::AID &from, const messages::MetaStoreRequest &req) = 0;

        virtual void ReceiveKeepAlive(const litebus::AID &from, const messages::MetaStoreRequest &req) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<LeaseServiceActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<LeaseServiceActor> &actor)
            : Business(member, actor)
        {
        }

        ~MasterBusiness() override = default;

        void OnChange() override;

        void ReceiveGrant(const litebus::AID &from, const messages::MetaStoreRequest &req) override;

        void ReceiveRevoke(const litebus::AID &from, const messages::MetaStoreRequest &req) override;

        void ReceiveKeepAlive(const litebus::AID &from, const messages::MetaStoreRequest &req) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<LeaseServiceActor> &actor)
            : Business(member, actor){};
        ~SlaveBusiness() override = default;

        void OnChange() override;

        void ReceiveGrant(const litebus::AID &from, const messages::MetaStoreRequest &req) override;

        void ReceiveRevoke(const litebus::AID &from, const messages::MetaStoreRequest &req) override;

        void ReceiveKeepAlive(const litebus::AID &from, const messages::MetaStoreRequest &req) override;
    };

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };

private:
    litebus::AID kvServiceActor_;

    litebus::AID backupActor_;

    bool running_;

    // | 2 bytes  | 5 bytes   | 1 byte  |
    // | memberID | timestamp | cnt     |
    int64_t index_{ time(nullptr) };

    std::unordered_map<int64_t, ::messages::Lease> leases_;

    bool needExplore_;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_LEASE_SERVICE_ACTOR_H
