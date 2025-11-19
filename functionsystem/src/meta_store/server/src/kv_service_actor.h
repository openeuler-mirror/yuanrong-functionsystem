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

#ifndef FUNCTION_MASTER_META_STORE_KV_SERVICE_ACTOR_H
#define FUNCTION_MASTER_META_STORE_KV_SERVICE_ACTOR_H

#include "actor/actor.hpp"
#include "backup_actor.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
class KvServiceActor : public litebus::ActorBase,
                       public MetaStoreHealthyObserver,
                       public std::enable_shared_from_this<KvServiceActor> {
public:
    KvServiceActor();

    explicit KvServiceActor(const litebus::AID &backupActor, bool needExplore = false);

    explicit KvServiceActor(const std::string &namePrefix);

    ~KvServiceActor() override = default;

public:
    virtual litebus::Future<Status> AsyncPut(const litebus::AID &from,
                                             std::shared_ptr<messages::MetaStore::PutRequest> request);
    virtual litebus::Future<Status> AsyncDelete(const litebus::AID &from,
                                                std::shared_ptr<messages::MetaStoreRequest> request);
    virtual litebus::Future<Status> AsyncGet(const litebus::AID &from,
                                             std::shared_ptr<messages::MetaStoreRequest> request);
    virtual litebus::Future<Status> AsyncTxn(const litebus::AID &from,
                                             std::shared_ptr<messages::MetaStoreRequest> request);
    virtual litebus::Future<Status> AsyncWatch(const litebus::AID &from,
                                               std::shared_ptr<messages::MetaStoreRequest> request);
    virtual litebus::Future<Status> AsyncGetAndWatch(const litebus::AID &from,
                                                     std::shared_ptr<messages::MetaStoreRequest> request);
public:
    struct OngoingEvent {
        etcdserverpb::RequestOp::RequestCase type;
        ::mvccpb::KeyValue kv;
        ::mvccpb::KeyValue preKv;
    };

public:
    Status AddLeaseServiceActor(const litebus::AID &aid);

    Status AddWatchServiceActor(const litebus::AID &aid);  // for test

    Status RemoveWatchServiceActor();  // for test

    void OnCreateWatcher(int64_t startReversion);

    std::pair<KeyValue, KeyValue> TryPutCache(std::shared_ptr<messages::MetaStore::PutRequest> request,
                                std::shared_ptr<messages::MetaStore::PutResponse> response);

    Status OnTryPutCache(const std::string& key, const Status &status);

    bool CheckUniqueOngoingValid(const std::string &key);

    bool CheckDeletesOngoingValid(const DeleteResults& deletes);

    bool CheckTxnOngoingValid(const TxnResults& txn);

    void InsertUniqueOngoing(etcdserverpb::RequestOp::RequestCase type, const KeyValue& kv, const KeyValue& preKv);

    void InsertDeleteOngoing(const DeleteResults& deletes);

    void InsertTxnOngoing(const TxnResults& txn);

    PutResults Put(const ::etcdserverpb::PutRequest *etcdPutRequest, ::etcdserverpb::PutResponse *etcdPutResponse);

    PutResults TryPut(const ::etcdserverpb::PutRequest *etcdPutRequest, ::etcdserverpb::PutResponse *etcdPutResponse);

    DeleteResults DeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
                              ::etcdserverpb::DeleteRangeResponse *response);

    DeleteResults TryDeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
                          ::etcdserverpb::DeleteRangeResponse *response);

    Status OnTryDeleteRange(const DeleteResults& deletes, const Status& status);

    Status OnRevoke(const std::set<std::string> &keys);

    ::grpc::Status Range(const ::etcdserverpb::RangeRequest *request, ::etcdserverpb::RangeResponse *response);

    TxnResults Txn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
                   const std::string &requestId);

    TxnResults TryTxn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
                                   const std::string &requestId);

    Status OnTryTxn(const TxnResults& txnResults, const Status &status);

    Status OnAsyncPut(const std::string &from, std::shared_ptr<messages::MetaStore::PutRequest> request,
                      const std::shared_ptr<messages::MetaStore::PutResponse> &putResponse, const Status &status);

    Status OnAsyncDelete(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                         const std::shared_ptr<etcdserverpb::DeleteRangeResponse> &deleteResponse,
                         const Status& status);

    Status OnAsyncGet(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                      const std::shared_ptr<::etcdserverpb::RangeResponse> &getResponse);

    Status OnAsyncTxn(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                      const std::shared_ptr<::etcdserverpb::TxnResponse> &response, const Status &status);

    virtual litebus::Future<Status> OnAsyncGetAndWatch(const litebus::AID &from, const std::string &uuid,
                                                       std::shared_ptr<::etcdserverpb::WatchCreateRequest> watchRequest,
                                                       std::shared_ptr<::etcdserverpb::WatchResponse> watchResponse);

    litebus::Future<bool> Recover();

    bool Sync(const std::shared_ptr<GetResponse> &getResponse);

    void HandleKvEvents(const std::vector<WatchEvent> &events, bool synced);

    void HandleKvPutEvent(const WatchEvent &event, bool synced, uint32_t currentRevision);

    void HandleKvDeleteEvent(const WatchEvent &event, bool synced);

    litebus::Future<SyncResult> KvSyncer(const std::shared_ptr<GetResponse> &getResponse);

    void OnHealthyStatus(const Status &status) override;

    void InitExplorer();

protected:
    void Init() override;
    void Finalize() override;

protected:
    int64_t modRevision_{ 0 };  // default 0

    std::map<std::string, OngoingEvent> ongoing_;

    std::map<std::string, ::mvccpb::KeyValue> cache_;

    litebus::AID leaseServiceActor_;

    litebus::AID watchServiceActor_;

    litebus::AID etcdKvClientActor_;

    litebus::AID backupActor_;

protected:
    virtual void CheckAndCreateWatchServiceActor();
    void ConvertWatchCreateRequestToRangeRequest(std::shared_ptr<::etcdserverpb::WatchCreateRequest> createReq,
                                                 etcdserverpb::RangeRequest &rangeReq);

    template <typename S, typename T>
    bool TxnIfCompare(S source, const ::etcdserverpb::Compare_CompareResult &operation, T target);

    bool TxnIf(const ::etcdserverpb::TxnRequest *request);

    TxnResults TxnThen(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response);

    TxnResults TxnElse(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response);

    static void SortTarget(const etcdserverpb::RangeRequest *request, std::vector<::mvccpb::KeyValue> &targets);

    void AddPrevKv(etcdserverpb::DeleteRangeResponse *response, const ::mvccpb::KeyValue &kv);

    void TxnCommon(const etcdserverpb::RequestOp &cmp, ::etcdserverpb::TxnResponse *response, TxnResults &txn);

    std::string namePrefix_;

    Status healthyStatus_ = Status::OK();

    bool needExplore_;

private:
    bool UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    struct Member {
        explorer::LeaderInfo leaderInfo;
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<KvServiceActor> &actor)
            : member_(member), actor_(actor){};
        ~Business() override = default;

        virtual litebus::Future<Status> AsyncPut(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStore::PutRequest> request) = 0;
        virtual litebus::Future<Status> AsyncDelete(const litebus::AID &from,
                                                    std::shared_ptr<messages::MetaStoreRequest> request) = 0;
        virtual litebus::Future<Status> AsyncGet(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request) = 0;
        virtual litebus::Future<Status> AsyncTxn(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request) = 0;
        virtual litebus::Future<Status> AsyncWatch(const litebus::AID &from,
                                                   std::shared_ptr<messages::MetaStoreRequest> request) = 0;
        virtual litebus::Future<Status> AsyncGetAndWatch(const litebus::AID &from,
                                                         std::shared_ptr<messages::MetaStoreRequest> request) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<KvServiceActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<KvServiceActor> &actor)
            : Business(member, actor)
        {
        }

        ~MasterBusiness() override = default;

        void OnChange() override;

        litebus::Future<Status> AsyncPut(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStore::PutRequest> request) override;
        litebus::Future<Status> AsyncDelete(const litebus::AID &from,
                                            std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncGet(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncTxn(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncWatch(const litebus::AID &from,
                                           std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncGetAndWatch(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<KvServiceActor> &actor)
            : Business(member, actor){};
        ~SlaveBusiness() override = default;

        void OnChange() override;

        litebus::Future<Status> AsyncPut(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStore::PutRequest> request) override;
        litebus::Future<Status> AsyncDelete(const litebus::AID &from,
                                            std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncGet(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncTxn(const litebus::AID &from,
                                         std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncWatch(const litebus::AID &from,
                                           std::shared_ptr<messages::MetaStoreRequest> request) override;
        litebus::Future<Status> AsyncGetAndWatch(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request) override;
    };

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_KV_SERVICE_ACTOR_H
