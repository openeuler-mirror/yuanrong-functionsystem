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
#include "meta_store_monitor/meta_store_healthy_observer.h"
#include "meta_store_client/meta_store_struct.h"
#include "proto/pb/message_pb.h"
#include "status/status.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
class KvServiceActor : public litebus::ActorBase, public MetaStoreHealthyObserver {
public:
    KvServiceActor();

    explicit KvServiceActor(const litebus::AID &backupActor);

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
    Status AddLeaseServiceActor(const litebus::AID &aid);

    Status AddWatchServiceActor(const litebus::AID &aid);  // for test

    Status RemoveWatchServiceActor();  // for test

    void OnCreateWatcher(int64_t startReversion);

    ::mvccpb::KeyValue PutCache(std::shared_ptr<messages::MetaStore::PutRequest> request,
                                std::shared_ptr<messages::MetaStore::PutResponse> response);

    PutResults Put(const ::etcdserverpb::PutRequest *etcdPutRequest, ::etcdserverpb::PutResponse *etcdPutResponse);

    DeleteResults DeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
                              ::etcdserverpb::DeleteRangeResponse *response);

    Status OnRevoke(const std::set<std::string> &keys);

    ::grpc::Status Range(const ::etcdserverpb::RangeRequest *request, ::etcdserverpb::RangeResponse *response);
    TxnResults Txn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
                   const std::string &requestId);

    Status OnAsyncPut(const std::string &from, std::shared_ptr<messages::MetaStore::PutRequest> request,
                      const std::shared_ptr<messages::MetaStore::PutResponse> &putResponse);

    Status OnAsyncDelete(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                         const std::shared_ptr<etcdserverpb::DeleteRangeResponse> &deleteResponse);

    Status OnAsyncGet(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                      const std::shared_ptr<::etcdserverpb::RangeResponse> &getResponse);

    Status OnAsyncTxn(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                      const std::shared_ptr<::etcdserverpb::TxnResponse> &response);

    virtual litebus::Future<Status> OnAsyncGetAndWatch(const litebus::AID &from, const std::string &uuid,
                                                       std::shared_ptr<::etcdserverpb::WatchCreateRequest> watchRequest,
                                                       std::shared_ptr<::etcdserverpb::WatchResponse> watchResponse);

    litebus::Future<bool> Recover();

    bool Sync(const std::shared_ptr<GetResponse> &getResponse);

    void OnHealthyStatus(const Status &status) override;

protected:
    void Init() override;
    void Finalize() override;

protected:
    int64_t modRevision_{ 0 };  // default 0

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
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_KV_SERVICE_ACTOR_H
