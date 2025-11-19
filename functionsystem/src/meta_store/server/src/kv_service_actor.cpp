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

#include "kv_service_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/utils/etcd_util.h"
#include "lease_service_actor.h"
#include "meta_store_common.h"
#include "watch_service_actor.h"

namespace functionsystem::meta_store {
using namespace functionsystem::explorer;

KvServiceActor::KvServiceActor() : ActorBase("KvServiceActor"), needExplore_(false)
{
}

KvServiceActor::KvServiceActor(const litebus::AID &backupActor, bool needExplore)
    : ActorBase("KvServiceActor"), backupActor_(backupActor), needExplore_(needExplore)
{
    if (!backupActor_.OK()) {
        YRLOG_WARN("backup actor isn't ok, don't explore");
        needExplore_ = false;
    }
}

KvServiceActor::KvServiceActor(const std::string &namePrefix)
    : ActorBase(namePrefix + "KvServiceActor"), namePrefix_(namePrefix), needExplore_(false)
{
}

bool KvServiceActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID("KvServiceActor", leaderInfo.address);
    member_->leaderInfo = leaderInfo;

    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("KvServiceActor UpdateLeaderInfo new status({}) business don't exist", newStatus);
        return false;
    }
    business_ = businesses_[newStatus];
    ASSERT_IF_NULL(business_);
    business_->OnChange();
    curStatus_ = newStatus;
    return true;
}

void KvServiceActor::InitExplorer()
{
    member_ = std::make_shared<Member>();
    auto masterBusiness = std::make_shared<MasterBusiness>(member_, shared_from_this());
    auto slaveBusiness = std::make_shared<SlaveBusiness>(member_, shared_from_this());

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    if (needExplore_) {
        business_ = businesses_[SLAVE_BUSINESS];
        curStatus_ = SLAVE_BUSINESS;
        (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
            "kvService", [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
                litebus::Async(aid, &KvServiceActor::UpdateLeaderInfo, leaderInfo);
            });
        return;
    }

    // don't explore, set to master business
    business_ = businesses_[MASTER_BUSINESS];
    curStatus_ = MASTER_BUSINESS;
}

void KvServiceActor::Init()
{
    InitExplorer();
}

void KvServiceActor::Finalize()
{
    if (watchServiceActor_.OK()) {
        litebus::Terminate(watchServiceActor_);
        litebus::Await(watchServiceActor_);
    }
}

Status KvServiceActor::AddLeaseServiceActor(const litebus::AID &aid)
{
    leaseServiceActor_ = aid;
    return Status::OK();
}

void KvServiceActor::CheckAndCreateWatchServiceActor()
{
    if (!watchServiceActor_.OK()) {
        YRLOG_DEBUG("create watch service actor");
        const std::string watchServiceActorName = namePrefix_ + "WatchServiceActor";
        auto watchSrvActor = std::make_shared<WatchServiceActor>(watchServiceActorName);
        watchServiceActor_ = litebus::Spawn(watchSrvActor);
    }
}

litebus::Future<Status> KvServiceActor::AsyncWatch(const litebus::AID &from,
                                                   std::shared_ptr<messages::MetaStoreRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncWatch(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncWatch(const litebus::AID &from,
                                                                   std::shared_ptr<messages::MetaStoreRequest> request)
{
    etcdserverpb::WatchRequest watchRequest;
    if (!watchRequest.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal watch request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal watch request");
    }

    YRLOG_DEBUG("{}|execute watch request, key: {}", request->requestid(), watchRequest.create_request().key());
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->CheckAndCreateWatchServiceActor();
    return litebus::Async(actor->watchServiceActor_, &WatchServiceActor::Create, from, request->requestid(),
                          std::make_shared<::etcdserverpb::WatchCreateRequest>(watchRequest.create_request()));
}

void KvServiceActor::ConvertWatchCreateRequestToRangeRequest(
    std::shared_ptr<::etcdserverpb::WatchCreateRequest> createReq, etcdserverpb::RangeRequest &rangeReq)
{
    rangeReq.set_key(createReq->key());
    if (!createReq->range_end().empty()) {
        rangeReq.set_range_end(createReq->range_end());
    }
}

litebus::Future<Status> KvServiceActor::AsyncGetAndWatch(const litebus::AID &from,
                                                         std::shared_ptr<messages::MetaStoreRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncGetAndWatch(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncGetAndWatch(
    const litebus::AID &from, std::shared_ptr<messages::MetaStoreRequest> request)
{
    etcdserverpb::WatchRequest watchRequest;
    if (!watchRequest.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal get and watch request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal get and watch request");
    }

    YRLOG_DEBUG("{}|execute get and watch request, key: {}", request->requestid(), watchRequest.create_request().key());
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    actor->CheckAndCreateWatchServiceActor();
    auto watchCreateRequest = std::make_shared<::etcdserverpb::WatchCreateRequest>(watchRequest.create_request());
    // get after create watch, in order to ensure the kv is the latest version
    return litebus::Async(actor->watchServiceActor_, &WatchServiceActor::CreateWatch, from, watchCreateRequest)
        .Then(litebus::Defer(actor->GetAID(), &KvServiceActor::OnAsyncGetAndWatch, from, request->requestid(),
                             watchCreateRequest, std::placeholders::_1));
}

litebus::Future<Status> KvServiceActor::OnAsyncGetAndWatch(
    const litebus::AID &from, const std::string &uuid, std::shared_ptr<::etcdserverpb::WatchCreateRequest> watchRequest,
    std::shared_ptr<::etcdserverpb::WatchResponse> watchResponse)
{
    ::etcdserverpb::RangeRequest getRequest;
    ::etcdserverpb::RangeResponse getResponse;
    ConvertWatchCreateRequestToRangeRequest(watchRequest, getRequest);
    Range(&getRequest, &getResponse);

    messages::GetAndWatchResponse gwResponse;
    gwResponse.set_getresponsemsg(getResponse.SerializeAsString());
    gwResponse.set_watchresponsemsg(watchResponse->SerializeAsString());
    messages::MetaStoreResponse res;
    res.set_responseid(uuid);
    res.set_responsemsg(gwResponse.SerializeAsString());
    YRLOG_DEBUG("{}|send GetAndWatch response to {}, watch id: {}, get key count: {}", uuid, from.HashString(),
                watchResponse->watch_id(), getResponse.kvs_size());
    litebus::Async(watchServiceActor_, &WatchServiceActor::SendResponse, from, "OnGetAndWatch", res);
    return Status::OK();
}

Status KvServiceActor::AddWatchServiceActor(const litebus::AID &aid)
{
    watchServiceActor_ = aid;
    return Status::OK();
}

Status KvServiceActor::RemoveWatchServiceActor()
{
    watchServiceActor_ = litebus::AID();
    return Status::OK();
}

void KvServiceActor::OnCreateWatcher(int64_t startReversion)
{
    YRLOG_INFO("success to create watcher, revision: {}.", startReversion);
    for (const auto &iterator : cache_) {
        if (iterator.second.mod_revision() >= startReversion) {
            ::mvccpb::KeyValue prevKv;
            litebus::Async(watchServiceActor_, &WatchServiceActor::OnPut, iterator.second, prevKv);
        }
    }
}

Status KvServiceActor::OnAsyncPut(const std::string &from, std::shared_ptr<messages::MetaStore::PutRequest> request,
                                  const std::shared_ptr<messages::MetaStore::PutResponse> &putResponse,
                                  const Status &status)
{
    if (status.IsError()) {
        YRLOG_WARN("{}|on async put failed, reason: {}", request->requestid(), status.RawMessage());
        putResponse->set_status(status.StatusCode());
        putResponse->set_errormsg(status.RawMessage());
    }
    YRLOG_DEBUG("{}|put response callback to client.", request->requestid());
    Send(from, "OnPut", putResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServiceActor::AsyncPut(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStore::PutRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncPut(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncPut(
    const litebus::AID &from, std::shared_ptr<messages::MetaStore::PutRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_DEBUG("{}|received put request", request->requestid());
    auto response = std::make_shared<messages::MetaStore::PutResponse>();
    actor->CheckAndCreateWatchServiceActor();

    if (!actor->CheckUniqueOngoingValid(request->key())) {
        YRLOG_ERROR("async put failed, reason: key {} is being processed", request->key());
        return Status(StatusCode::FAILED, "async put failed.");
    }
    auto puts = actor->TryPutCache(request, response);
    actor->InsertUniqueOngoing(etcdserverpb::RequestOp::RequestCase::kRequestPut, puts.first, puts.second);

    // backup
    if (actor->backupActor_.OK()) {
        auto future = litebus::Async(actor->backupActor_, &BackupActor::WritePut, puts.first, request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request, aid(actor->GetAID())](const Status &status) -> litebus::Future<Status> {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup put: {}, reason: {}", request->requestid(), request->key(),
                                   status.ToString());
                    }
                    return litebus::Async(aid, &KvServiceActor::OnTryPutCache, request->key(), status);
                })
                .Then(litebus::Defer(actor->GetAID(), &KvServiceActor::OnAsyncPut, from, request, response,
                    std::placeholders::_1));
        }
    }
    actor->OnTryPutCache(request->key(), Status::OK());
    return actor->OnAsyncPut(from, request, response, Status::OK());
}

std::pair<KeyValue, KeyValue> KvServiceActor::TryPutCache(std::shared_ptr<messages::MetaStore::PutRequest> request,
                                            std::shared_ptr<messages::MetaStore::PutResponse> response)
{
    response->set_requestid(request->requestid());
    ::mvccpb::KeyValue prevKv;
    ::mvccpb::KeyValue kv;
    if (auto iterator = cache_.find(request->key()); iterator != cache_.end()) {
        kv = iterator->second;
        prevKv = iterator->second;
    }

    if (!kv.key().empty()) {
        if (request->prevkv()) {  // return preview
            response->set_prevkv(prevKv.SerializeAsString());
        }
        kv.set_version(kv.version() + 1);
        kv.set_mod_revision(++modRevision_);
    } else {
        kv.set_key(request->key());
        kv.set_version(1);  // reset
        kv.set_mod_revision(++modRevision_);
        kv.set_create_revision(modRevision_);
    }
    // update,after if(empty)
    kv.set_value(request->value());
    kv.set_lease(request->lease());

    litebus::Async(leaseServiceActor_, &LeaseServiceActor::Attach, request->key(), request->lease());

    YRLOG_INFO("success to try put key-value, revision: {}, kv.mod_revision: {}.", modRevision_, kv.mod_revision());
    response->set_revision(modRevision_);

    return {kv, prevKv};
}

Status KvServiceActor::OnTryPutCache(const std::string& key, const Status &status)

{
    if (status.IsError()) {
        YRLOG_ERROR("on try put cache failed. reason: {}", status.RawMessage());
        ongoing_.erase(key);
        return status;
    }
    const auto iterator = ongoing_.find(key);
    if (iterator == ongoing_.end()) {
        YRLOG_ERROR("on try put cache failed, reason: can not find key ({}) in ongoing_", key);
        return status;
    }
    auto kv = iterator->second.kv;
    auto preKv = iterator->second.preKv;
    cache_[key] = kv;
    litebus::Async(watchServiceActor_, &WatchServiceActor::OnPut, kv, preKv);
    YRLOG_INFO("success to put key-value, revision: {}, kv.mod_revision: {}.", modRevision_, kv.mod_revision());
    ongoing_.erase(key);
    return Status::OK();
}

bool KvServiceActor::CheckUniqueOngoingValid(const std::string &key)
{
    if (ongoing_.find(key) == ongoing_.end()) {
        return true;
    }
    YRLOG_ERROR("failed to check ongoing valid, reason: duplicate ongoing key: ({})", key);
    return false;
}

bool KvServiceActor::CheckDeletesOngoingValid(const DeleteResults& deletes)
{
    for (const auto& kv : *deletes) {
        auto iterator = ongoing_.find(kv.key());
        if (iterator == ongoing_.end()) {
            continue;
        }
        if (iterator->second.type != etcdserverpb::RequestOp::kRequestDeleteRange) {
            YRLOG_INFO("failed to check deletes valid, reason: duplicate ongoing key: ({})", iterator->second.kv.key());
            return false;
        }
    }
    return true;
}

bool KvServiceActor::CheckTxnOngoingValid(const TxnResults& txn)
{
    for (const auto& puts : txn.first) {
        if (!CheckUniqueOngoingValid(puts.first.key())) {
            return false;
        }
    }
    for (const auto& deletes : txn.second) {
        if (!CheckDeletesOngoingValid(deletes)) {
            return false;
        }
    }
    return true;
}

void KvServiceActor::InsertUniqueOngoing(etcdserverpb::RequestOp::RequestCase type, const KeyValue& kv,
    const KeyValue& preKv)
{
    ongoing_[kv.key()] = OngoingEvent {type, kv, preKv};
}

void KvServiceActor::InsertDeleteOngoing(const DeleteResults& deletes)
{
    for (const auto& kv : *deletes) {
        InsertUniqueOngoing(etcdserverpb::RequestOp::kRequestDeleteRange, kv, {});
    }
}

void KvServiceActor::InsertTxnOngoing(const TxnResults& txn)
{
    for (const auto& puts : txn.first) {
        InsertUniqueOngoing(etcdserverpb::RequestOp::kRequestPut, puts.first, puts.second);
    }
    for (const auto& deletes : txn.second) {
        for (const auto& kv : *deletes) {
            InsertUniqueOngoing(etcdserverpb::RequestOp::kRequestDeleteRange, kv, {});
        }
    }
}

PutResults KvServiceActor::Put(const ::etcdserverpb::PutRequest *etcdPutRequest,
                               ::etcdserverpb::PutResponse *etcdPutResponse)
{
    if (!CheckUniqueOngoingValid(etcdPutRequest->key())) {
        YRLOG_ERROR("put failed. reason: key {} is being processed", etcdPutRequest->key());
        return {};
    }
    auto puts = TryPut(etcdPutRequest, etcdPutResponse);
    InsertUniqueOngoing(etcdserverpb::RequestOp::RequestCase::kRequestPut, puts.first, puts.second);
    (void)OnTryPutCache(etcdPutRequest->key(), Status::OK());
    return puts;
}

PutResults KvServiceActor::TryPut(const ::etcdserverpb::PutRequest *etcdPutRequest,
                                   ::etcdserverpb::PutResponse *etcdPutResponse)
{
    if (etcdPutRequest == nullptr || etcdPutResponse == nullptr) {
        return PutResults();
    }

    auto request = std::make_shared<messages::MetaStore::PutRequest>();
    request->set_key(etcdPutRequest->key());
    request->set_value(etcdPutRequest->value());
    request->set_lease(etcdPutRequest->lease());
    request->set_prevkv(etcdPutRequest->prev_kv());

    auto response = std::make_shared<messages::MetaStore::PutResponse>();
    auto puts = TryPutCache(request, response);
    auto kv = puts.first;
    auto preKv = puts.second;
    etcdPutResponse->mutable_header()->set_revision(response->revision());
    etcdPutResponse->mutable_header()->set_cluster_id(META_STORE_CLUSTER_ID);
    if (request->prevkv() && !preKv.key().empty()) {
        *etcdPutResponse->mutable_prev_kv() = std::move(preKv);
    }

    return {kv, preKv};
}

Status KvServiceActor::OnAsyncDelete(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                                     const std::shared_ptr<etcdserverpb::DeleteRangeResponse> &deleteResponse,
                                     const Status& status)
{
    messages::MetaStoreResponse response;
    if (status.IsError()) {
        YRLOG_WARN("{}|on async delete failed, reason: {}", request->requestid(), status.RawMessage());
        response.set_status(status.StatusCode());
        response.set_errormsg(status.RawMessage());
    }
    response.set_responseid(request->requestid());
    response.set_responsemsg(deleteResponse->SerializeAsString());

    YRLOG_DEBUG("{}|delete response callback to client.", request->requestid());
    Send(from, "OnDelete", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServiceActor::AsyncDelete(const litebus::AID &from,
                                                    std::shared_ptr<messages::MetaStoreRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncDelete(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncDelete(const litebus::AID &from,
                                                                    std::shared_ptr<messages::MetaStoreRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    ::etcdserverpb::DeleteRangeRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal delete request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal delete request");
    }

    auto response = std::make_shared<::etcdserverpb::DeleteRangeResponse>();
    auto deletes = actor->TryDeleteRange(&payload, response.get());
    if (!actor->CheckDeletesOngoingValid(deletes)) {
        return Status(StatusCode::FAILED, "receive illegal delete request");
    }
    actor->InsertDeleteOngoing(deletes);
    YRLOG_DEBUG("{}|try to delete {} records for {}.", request->requestid(), response->deleted(), payload.key());

    // backup
    if (actor->backupActor_.OK()) {
        auto future = litebus::Async(actor->backupActor_, &BackupActor::WriteDeletes, deletes, request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request, key(payload.key()), aid(actor->GetAID()), deletes](const Status &status)
                    -> litebus::Future<Status> {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup delete: {}, reason: {}", request->requestid(), key,
                                   status.ToString());
                    }
                    return litebus::Async(aid, &KvServiceActor::OnTryDeleteRange, deletes, status);
                })
                .Then(litebus::Defer(actor->GetAID(), &KvServiceActor::OnAsyncDelete, from, request, response,
                    std::placeholders::_1));
        }
    }
    (void)actor->OnTryDeleteRange(deletes, Status::OK());
    return actor->OnAsyncDelete(from, request, response, Status::OK());
}

DeleteResults KvServiceActor::DeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
                                          ::etcdserverpb::DeleteRangeResponse *response)
{
    auto deletes = TryDeleteRange(request, response);
    if (!CheckDeletesOngoingValid(deletes)) {
        return nullptr;
    }
    InsertDeleteOngoing(deletes);
    (void)OnTryDeleteRange(deletes, Status::OK());
    return deletes;
}

DeleteResults KvServiceActor::TryDeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
                                              ::etcdserverpb::DeleteRangeResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return nullptr;
    }

    auto header = response->mutable_header();
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_revision(modRevision_);

    auto deletes = std::make_shared<std::vector<::mvccpb::KeyValue>>();
    if (request->range_end().empty()) {  // delete one
        if (auto iterator = cache_.find(request->key()); iterator != cache_.end()) {
            mvccpb::KeyValue kv = cache_[request->key()];
            deletes->emplace_back(kv);
            if (request->prev_kv()) {
                AddPrevKv(response, kv);
            }
        }
    } else {
        for (auto iterator = cache_.begin(); iterator != cache_.end();) {
            if (iterator->first < request->key() || iterator->first >= request->range_end()) {
                iterator++;  // not in range, next
                continue;
            }

            ::mvccpb::KeyValue kv = iterator->second;  // 1.get reference of key-value.
            deletes->emplace_back(kv);                  // 2.add key-value to list and copy.
            if (request->prev_kv()) {                   // 3.makeUp preview key-value.
                AddPrevKv(response, kv);
            }
            iterator++;  // 3.erase, must iterator++ or iterator = erase.
        }
    }

    response->set_deleted(static_cast<int64_t>(deletes->size()));

    return deletes;
}

Status KvServiceActor::OnTryDeleteRange(const DeleteResults& deletes, const Status& status)
{
    if (status.IsError()) {
        YRLOG_ERROR("on try delete range failed, reason: {}", status.RawMessage());
        for (const auto& kv : *deletes) {
            ongoing_.erase(kv.key());
        }
        return status;
    }
    if (deletes == nullptr || deletes->empty()) {
        YRLOG_INFO("on try delete range did nothing");
        return Status::OK();
    }
    for (const auto& kv : *deletes) {
        cache_.erase(kv.key());
    }
    for (const auto& kv : *deletes) {
        ongoing_.erase(kv.key());
    }
    litebus::Async(watchServiceActor_, &WatchServiceActor::OnDeleteList, deletes);
    return Status::OK();
}

void KvServiceActor::AddPrevKv(etcdserverpb::DeleteRangeResponse *response, const ::mvccpb::KeyValue &kv)
{
    auto addPrevKv = response->add_prev_kvs();
    addPrevKv->set_key(kv.key());
    addPrevKv->set_value(kv.value());
    addPrevKv->set_lease(kv.lease());
    addPrevKv->set_version(kv.version());
    addPrevKv->set_mod_revision(kv.mod_revision());
    addPrevKv->set_create_revision(kv.create_revision());
}

Status KvServiceActor::OnAsyncGet(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                                  const std::shared_ptr<::etcdserverpb::RangeResponse> &getResponse)
{
    messages::MetaStoreResponse response;
    response.set_responseid(request->requestid());
    response.set_responsemsg(getResponse->SerializeAsString());

    YRLOG_DEBUG("{}|get response callback to client.", request->requestid());
    Send(from, "OnGet", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServiceActor::AsyncGet(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncGet(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncGet(const litebus::AID &from,
                                                                 std::shared_ptr<messages::MetaStoreRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    ::etcdserverpb::RangeRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal get payload.", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal get payload");
    }
    auto response = std::make_shared<::etcdserverpb::RangeResponse>();
    actor->Range(&payload, response.get());
    YRLOG_DEBUG("{}|success to get {} cache size:{}", request->requestid(), payload.key(), response->kvs().size());
    return actor->OnAsyncGet(from, request, response);
}

::grpc::Status KvServiceActor::Range(const ::etcdserverpb::RangeRequest *request,
                                     ::etcdserverpb::RangeResponse *response)
{
    if (request == nullptr || response == nullptr) {
        return grpc::Status{ grpc::StatusCode::INVALID_ARGUMENT, "null request or response" };
    }
    auto header = response->mutable_header();
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_revision(modRevision_);
    if (request->range_end().empty()) {
        auto iterator = cache_.find(request->key());
        if (iterator == cache_.end()) {
            return grpc::Status::OK;
        }
        response->set_count(1);
        if (request->count_only()) {
            return grpc::Status::OK;
        }
        auto kv = response->add_kvs();
        kv->set_key(iterator->first);
        kv->set_mod_revision(iterator->second.mod_revision());
        if (request->keys_only()) {
            return grpc::Status::OK;
        }
        kv->set_value(iterator->second.value());

        return grpc::Status::OK;
    }
    std::vector<::mvccpb::KeyValue> targets;
    for (auto &iterator : cache_) {
        if (iterator.first < request->key() || iterator.first >= request->range_end()) {
            continue;
        }

        response->set_count(response->count() + 1);
        targets.emplace_back(iterator.second);
    }
    if (request->count_only()) {
        return grpc::Status::OK;
    }
    SortTarget(request, targets);
    for (auto &iterator : targets) {
        auto kv = response->add_kvs();
        kv->set_key(iterator.key());
        kv->set_mod_revision(iterator.mod_revision());
        if (request->keys_only()) {
            continue;
        }
        kv->set_value(iterator.value());
    }
    return grpc::Status::OK;
}

void KvServiceActor::SortTarget(const etcdserverpb::RangeRequest *request, std::vector<mvccpb::KeyValue> &targets)
{
    etcdserverpb::RangeRequest_SortOrder order = request->sort_order();
    switch (request->sort_target()) {
        case etcdserverpb::RangeRequest_SortTarget_KEY: {
            std::sort(targets.begin(), targets.end(),
                      [&order](const mvccpb::KeyValue &s, const mvccpb::KeyValue &t) -> bool {
                          return order == etcdserverpb::RangeRequest_SortOrder_DESCEND ? s.key() > t.key()
                                                                                       : s.key() < t.key();
                      });
            break;
        }
        case etcdserverpb::RangeRequest_SortTarget_VERSION: {
            std::sort(targets.begin(), targets.end(),
                      [&order](const mvccpb::KeyValue &s, const mvccpb::KeyValue &t) -> bool {
                          return order == etcdserverpb::RangeRequest_SortOrder_DESCEND ? s.version() > t.version()
                                                                                       : s.version() < t.version();
                      });
            break;
        }
        case etcdserverpb::RangeRequest_SortTarget_CREATE: {
            std::sort(targets.begin(), targets.end(),
                      [&order](const mvccpb::KeyValue &s, const mvccpb::KeyValue &t) -> bool {
                          return order == etcdserverpb::RangeRequest_SortOrder_DESCEND
                                     ? s.create_revision() > t.create_revision()
                                     : s.create_revision() < t.create_revision();
                      });
            break;
        }
        case etcdserverpb::RangeRequest_SortTarget_MOD: {
            std::sort(
                targets.begin(), targets.end(), [&order](const mvccpb::KeyValue &s, const mvccpb::KeyValue &t) -> bool {
                    return order == etcdserverpb::RangeRequest_SortOrder_DESCEND ? s.mod_revision() > t.mod_revision()
                                                                                 : s.mod_revision() < t.mod_revision();
                });
            break;
        }
        case etcdserverpb::RangeRequest_SortTarget_VALUE: {
            std::sort(targets.begin(), targets.end(),
                      [&order](const mvccpb::KeyValue &s, const mvccpb::KeyValue &t) -> bool {
                          return order == etcdserverpb::RangeRequest_SortOrder_DESCEND ? s.value() > t.value()
                                                                                       : s.value() < t.value();
                      });
            break;
        }
        case etcdserverpb::RangeRequest_SortTarget_RangeRequest_SortTarget_INT_MIN_SENTINEL_DO_NOT_USE_:
        case etcdserverpb::RangeRequest_SortTarget_RangeRequest_SortTarget_INT_MAX_SENTINEL_DO_NOT_USE_:
        default:
            // not support
            break;
    }
}

litebus::Future<Status> KvServiceActor::AsyncTxn(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStoreRequest> request)
{
    ASSERT_IF_NULL(business_);
    return business_->AsyncTxn(from, request);
}

litebus::Future<Status> KvServiceActor::MasterBusiness::AsyncTxn(const litebus::AID &from,
                                                                 std::shared_ptr<messages::MetaStoreRequest> request)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_DEBUG("{}|execute txn request", request->requestid());
    ::etcdserverpb::TxnRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal txn payload.", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal txn payload");
    }

    auto response = std::make_shared<::etcdserverpb::TxnResponse>();
    auto txn = actor->TryTxn(&payload, response.get(), request->requestid());
    if (!actor->CheckTxnOngoingValid(txn)) {
        return Status(StatusCode::FAILED, "failed to try txn");
    }
    actor->InsertTxnOngoing(txn);

    // backup
    if (actor->backupActor_.OK()) {
        auto future = litebus::Async(actor->backupActor_, &BackupActor::WriteTxn, txn, request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request, aid(actor->GetAID()), txn](const Status &status) -> litebus::Future<Status> {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup txn, reason: {}", request->requestid(), status.ToString());
                    }
                    return litebus::Async(aid, &KvServiceActor::OnTryTxn, txn, status);
                })
                .Then(litebus::Defer(actor->GetAID(), &KvServiceActor::OnAsyncTxn, from, request, response,
                    std::placeholders::_1));
        }
    }
    (void)actor->OnTryTxn(txn, Status::OK());
    YRLOG_DEBUG("{}|success to txn cache size: {}", request->requestid(), actor->cache_.size());
    return actor->OnAsyncTxn(from, request, response, Status::OK());
}

Status KvServiceActor::OnAsyncTxn(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                                  const std::shared_ptr<::etcdserverpb::TxnResponse> &response, const Status &status)
{
    YRLOG_DEBUG("{}|txn response callback to client.", request->requestid());

    messages::MetaStoreResponse message;
    if (status.IsError()) {
        YRLOG_ERROR("{}|on async txn failed, reason: {}", request->requestid(), status.RawMessage());
        message.set_status(status.StatusCode());
        message.set_errormsg(status.RawMessage());
    }
    message.set_responseid(request->requestid());
    message.set_responsemsg(response->SerializeAsString());
    Send(from, "OnTxn", message.SerializeAsString());

    return Status::OK();
}

TxnResults KvServiceActor::Txn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
                               const std::string &requestId)
{
    const auto txn = TryTxn(request, response, requestId);
    if (!CheckTxnOngoingValid(txn)) {
        return {};
    }
    InsertTxnOngoing(txn);
    (void)OnTryTxn(txn, Status::OK());
    return txn;
}

TxnResults KvServiceActor::TryTxn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
                                   const std::string &requestId)
{
    TxnResults txn;
    if (request == nullptr || response == nullptr) {
        return txn;
    }

    auto header = response->mutable_header();
    header->set_cluster_id(META_STORE_CLUSTER_ID);
    header->set_revision(modRevision_);

    if (TxnIf(request)) {
        YRLOG_DEBUG("{}|txn if then condition", requestId);
        txn = TxnThen(request, response);
    } else {
        YRLOG_DEBUG("{}|txn else condition", requestId);
        txn = TxnElse(request, response);
    }

    return txn;
}

Status KvServiceActor::OnTryTxn(const TxnResults& txnResults, const Status &status)
{
    auto putResults = txnResults.first;
    auto deleteResults = txnResults.second;
    if (status.IsError()) {
        YRLOG_ERROR("on try txn failed, reason: {}", status.RawMessage());
        std::unordered_set<std::string> keysToErase;
        for (const auto& kv : putResults) {
            keysToErase.insert(kv.first.key());
        }
        for (const auto& deletes : deleteResults) {
            if (deletes == nullptr) {
                continue;
            }
            for (const auto& kv : *deletes) {
                keysToErase.insert(kv.key());
            }
        }
        for (const auto& key : keysToErase) {
            ongoing_.erase(key);
        }
        return status;
    }
    if (!putResults.empty()) {
        for (const auto& kv : putResults) {
            OnTryPutCache(kv.first.key(), Status::OK());
        }
    }
    if (!deleteResults.empty()) {
        for (const auto& kv : deleteResults) {
            OnTryDeleteRange(kv, Status::OK());
        }
    }
    return Status::OK();
}

template <typename S, typename T>
bool KvServiceActor::TxnIfCompare(S source, const ::etcdserverpb::Compare_CompareResult &operation, T target)
{
    switch (operation) {
        case etcdserverpb::Compare_CompareResult_EQUAL:
            if (source == target) {
                break;
            }
            return false;
        case etcdserverpb::Compare_CompareResult_GREATER:
            if (source > target) {
                break;
            }
            return false;
        case etcdserverpb::Compare_CompareResult_LESS:
            if (source < target) {
                break;
            }
            return false;
        case etcdserverpb::Compare_CompareResult_NOT_EQUAL:
            if (source != target) {
                break;
            }
            return false;
        case etcdserverpb::Compare_CompareResult::
            Compare_CompareResult_Compare_CompareResult_INT_MIN_SENTINEL_DO_NOT_USE_:
        case etcdserverpb::Compare_CompareResult::
            Compare_CompareResult_Compare_CompareResult_INT_MAX_SENTINEL_DO_NOT_USE_:
        default:
            return false;  // not support, return not match
    }
    // succeeded to compare
    return true;
}

bool KvServiceActor::TxnIf(const ::etcdserverpb::TxnRequest *request)
{
    for (int i = 0; i < request->compare_size(); i++) {
        const ::etcdserverpb::Compare &cmp = request->compare(i);
        const std::string &key = cmp.key();

        auto iterator = cache_.find(key);

        switch (cmp.target()) {
            case etcdserverpb::Compare_CompareTarget_VERSION:
                if (TxnIfCompare(iterator == cache_.end() ? 0 : iterator->second.version(), cmp.result(),
                                 cmp.version())) {
                    break;
                }
                return false;
            case etcdserverpb::Compare_CompareTarget_CREATE:
                if (TxnIfCompare(iterator == cache_.end() ? 0 : iterator->second.create_revision(), cmp.result(),
                                 cmp.create_revision())) {
                    break;
                }
                return false;
            case etcdserverpb::Compare_CompareTarget_MOD:
                if (TxnIfCompare(iterator == cache_.end() ? 0 : iterator->second.mod_revision(), cmp.result(),
                                 cmp.mod_revision())) {
                    break;
                }
                return false;
            case etcdserverpb::Compare_CompareTarget_VALUE:
                if (iterator == cache_.end()) {
                    return false;
                }
                if (TxnIfCompare(iterator->second.value(), cmp.result(), cmp.value())) {
                    break;
                }
                return false;
            case etcdserverpb::Compare_CompareTarget_LEASE:
                if (TxnIfCompare(iterator == cache_.end() ? 0 : iterator->second.lease(), cmp.result(), cmp.lease())) {
                    break;
                }
                return false;
            case etcdserverpb::Compare_CompareTarget::
                Compare_CompareTarget_Compare_CompareTarget_INT_MIN_SENTINEL_DO_NOT_USE_:
            case etcdserverpb::Compare_CompareTarget::
                Compare_CompareTarget_Compare_CompareTarget_INT_MAX_SENTINEL_DO_NOT_USE_:
            default:
                return false;  // not support
        }
    }

    return true;
}

void KvServiceActor::TxnCommon(const etcdserverpb::RequestOp &cmp, ::etcdserverpb::TxnResponse *response,
                               TxnResults &txn)
{
    switch (cmp.request_case()) {
        case etcdserverpb::RequestOp::kRequestRange: {
            const etcdserverpb::RangeRequest &rangeRequest = cmp.request_range();
            etcdserverpb::ResponseOp *responseOp = response->add_responses();
            (void)Range(&rangeRequest, responseOp->mutable_response_range());
            break;
        }
        case etcdserverpb::RequestOp::kRequestPut: {
            const auto &putRequest = cmp.request_put();
            etcdserverpb::ResponseOp *responseOp = response->add_responses();
            auto put = TryPut(&putRequest, responseOp->mutable_response_put());
            txn.first.emplace_back(put);
            break;
        }
        case etcdserverpb::RequestOp::kRequestDeleteRange: {
            const auto &deleteRequest = cmp.request_delete_range();
            ::etcdserverpb::ResponseOp *responseOp = response->add_responses();
            auto deletes = TryDeleteRange(&deleteRequest, responseOp->mutable_response_delete_range());
            txn.second.emplace_back(deletes);
            break;
        }
        case etcdserverpb::RequestOp::kRequestTxn:
        case etcdserverpb::RequestOp::REQUEST_NOT_SET:
        default:
            // not support now
            break;
    }
}

TxnResults KvServiceActor::TxnThen(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response)
{
    TxnResults txn;
    response->set_succeeded(true);
    for (int i = 0; i < request->success_size(); i++) {
        const etcdserverpb::RequestOp &cmp = request->success(i);
        TxnCommon(cmp, response, txn);
    }
    return txn;
}

TxnResults KvServiceActor::TxnElse(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response)
{
    TxnResults txn;
    response->set_succeeded(false);
    for (int i = 0; i < request->failure_size(); i++) {
        const etcdserverpb::RequestOp &cmp = request->failure(i);
        TxnCommon(cmp, response, txn);
    }
    return txn;
}

Status KvServiceActor::OnRevoke(const std::set<std::string> &keys)
{
    auto deletes = std::make_shared<std::vector<::mvccpb::KeyValue>>();
    for (const auto &key : keys) {
        auto iterator = cache_.find(key);
        if (iterator == cache_.end()) {
            // has deleted
            continue;
        }
        YRLOG_DEBUG("delete expired key: {}", key);
        deletes->emplace_back(iterator->second);
        cache_.erase(iterator);
    }
    if (!deletes->empty()) {
        litebus::Async(watchServiceActor_, &WatchServiceActor::OnDeleteList, deletes);
    }
    if (backupActor_.OK()) {
        litebus::Async(backupActor_, &BackupActor::WriteDeletes, deletes, true);
    }

    return Status::OK();
}

litebus::Future<bool> KvServiceActor::Recover()
{
    GetOption option;
    option.prefix = true;
    if (!backupActor_.OK()) {
        return true;
    }
    return litebus::Async(backupActor_, &BackupActor::Get, META_STORE_BACKUP_KV_PREFIX, option)
        .Then(litebus::Defer(GetAID(), &KvServiceActor::Sync, std::placeholders::_1));
}

bool KvServiceActor::Sync(const std::shared_ptr<GetResponse> &getResponse)
{
    for (const auto &item : getResponse->kvs) {
        ::mvccpb::KeyValue kv;
        if (!kv.ParseFromString(item.value())) {
            YRLOG_WARN("failed to parse value for key({})", item.key());
            continue;
        }
        cache_[item.key().substr(META_STORE_BACKUP_KV_PREFIX.size())] = kv;
        if (kv.lease() != 0) {
            litebus::Async(leaseServiceActor_, &LeaseServiceActor::Attach, kv.key(), kv.lease());
        }
        YRLOG_INFO("success to sync kv({})", item.key().substr(META_STORE_BACKUP_KV_PREFIX.size()));

        // set max mod_revision for current mod reversion
        if (modRevision_ < kv.mod_revision()) {
            modRevision_ = kv.mod_revision();
        }
    }

    YRLOG_INFO("success to sync kvs with mod revision({}), and start watch", modRevision_);

    if (needExplore_) {
        WatchOption option;
        option.prefix = true;
        option.revision = getResponse->header.revision;

        auto kvSyncer =
            [aid(GetAID())](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<SyncResult> {
            return litebus::Async(aid, &KvServiceActor::KvSyncer, getResponse);
        };

        litebus::Async(
            backupActor_, &BackupActor::Watch, META_STORE_BACKUP_KV_PREFIX, option,
            [aid(GetAID())](const std::vector<WatchEvent> &events, bool synced) {
                auto respCopy = events;
                litebus::Async(aid, &KvServiceActor::HandleKvEvents, respCopy, synced);
                return true;
            },
            kvSyncer);
    }

    return true;
}

litebus::Future<SyncResult> KvServiceActor::KvSyncer(const std::shared_ptr<GetResponse> &getResponse)
{
    if (getResponse == nullptr || getResponse->status.IsError()) {
        YRLOG_INFO("failed to get kv from meta storage");
        return SyncResult{ getResponse->status };
    }

    if (getResponse->kvs.empty()) {
        YRLOG_INFO("get no result with kv from meta storage, revision is {}", getResponse->header.revision);
        return SyncResult{ Status::OK() };
    }

    std::vector<WatchEvent> events;
    std::set<std::string> remoteKeys;  // keys
    for (auto &kv : getResponse->kvs) {
        WatchEvent event{ .eventType = EVENT_TYPE_PUT, .kv = kv, .prevKv = {} };
        events.emplace_back(event);
        remoteKeys.emplace(kv.key().substr(META_STORE_BACKUP_KV_PREFIX.size()));
    }

    for (const auto &iter : cache_) {
        if (remoteKeys.find(iter.first) != remoteKeys.end()) {
            continue;
        }
        YRLOG_WARN("failed to find local key({}) in etcd, delete", iter.first);
        ::mvccpb::KeyValue kv;
        kv.set_key(META_STORE_BACKUP_KV_PREFIX + iter.first);
        kv.set_value(iter.second.SerializeAsString());
        WatchEvent event{ .eventType = EVENT_TYPE_DELETE, .kv = kv, .prevKv = {} };
        events.emplace_back(event);
    }

    HandleKvEvents(events, true);
    return SyncResult{ Status::OK() };
}

void KvServiceActor::HandleKvEvents(const std::vector<WatchEvent> &events, bool synced)
{
    // all events in one patch need to use the same revision to compare, because modRevisions in a patch may not be in
    // order (a lot of events being put into etcd in one txn)
    auto currentModRevision = modRevision_;
    for (const auto &event : events) {
        switch (event.eventType) {
            case EVENT_TYPE_PUT: {
                HandleKvPutEvent(event, synced, currentModRevision);
                break;
            }

            case EVENT_TYPE_DELETE: {
                YRLOG_DEBUG("receive kv({}) delete event", event.kv.key());
                auto prevKv = cache_[event.kv.key().substr(META_STORE_BACKUP_KV_PREFIX.size())];
                litebus::Async(watchServiceActor_, &WatchServiceActor::OnDelete, prevKv);
                cache_.erase(event.kv.key().substr(META_STORE_BACKUP_KV_PREFIX.size()));
                break;
            }

            default: {
                YRLOG_WARN("unknown event type {}", fmt::underlying(event.eventType));
            }
        }
    }
}

void KvServiceActor::HandleKvPutEvent(const WatchEvent &event, bool synced, uint32_t currentRevision)
{
    ::mvccpb::KeyValue kv;
    if (!kv.ParseFromString(event.kv.value())) {
        YRLOG_WARN("failed to parse value for key({})", event.kv.key());
        return;
    }

    auto key = event.kv.key().substr(META_STORE_BACKUP_KV_PREFIX.size());

    // all below events will be updated, other
    // 1. slave events
    // 2. newer events
    // 3. sync  events
    if (curStatus_ == MASTER_BUSINESS && kv.mod_revision() <= currentRevision && !synced) {
        auto cacheKv = cache_.find(key);
        if (cacheKv != cache_.end() && cacheKv->second.SerializeAsString() == kv.SerializeAsString()) {
            // skip same event
            return;
        }

        if (cacheKv != cache_.end() && kv.mod_revision() < cacheKv->second.mod_revision()
            && ongoing_.find(key) == ongoing_.end()) {
            YRLOG_WARN("receive old kv({}) of revision({}), current revision: {}, update", key, kv.mod_revision(),
                       cacheKv->second.mod_revision());
            if (backupActor_.OK()) {
                litebus::Async(backupActor_, &BackupActor::WritePut, cacheKv->second, true);
            }
            return;
        }

        if (cacheKv == cache_.end() && ongoing_.find(key) == ongoing_.end()) {
            YRLOG_WARN("receive non-existed old kv({}) of revision({}), current revision: {}, delete", key,
                       kv.mod_revision(), currentRevision);
            if (backupActor_.OK()) {
                auto deletes = std::make_shared<std::vector<::mvccpb::KeyValue>>();
                deletes->push_back(kv);
                litebus::Async(backupActor_, &BackupActor::WriteDeletes, deletes, true);
            }
            return;
        }
    }

    YRLOG_DEBUG("receive kv({}) put event, mod revision({})", event.kv.key(), kv.mod_revision());
    auto prevKv = cache_[key];
    cache_[key] = kv;
    litebus::Async(watchServiceActor_, &WatchServiceActor::OnPut, kv, prevKv);
    if (kv.lease() != 0) {
        litebus::Async(leaseServiceActor_, &LeaseServiceActor::Attach, kv.key(), kv.lease());
    }

    if (modRevision_ < kv.mod_revision()) {
        modRevision_ = kv.mod_revision();
    }
}

void KvServiceActor::OnHealthyStatus(const Status &status)
{
    YRLOG_DEBUG("KvServiceActor health status changes to healthy({})", status.IsOk());
    healthyStatus_ = status;
}

void KvServiceActor::MasterBusiness::OnChange()
{
    YRLOG_INFO("kv service actor changes to master service");
}

void KvServiceActor::SlaveBusiness::OnChange()
{
    YRLOG_INFO("kv service actor changes to slave service");
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncPut(
    const litebus::AID &from, std::shared_ptr<messages::MetaStore::PutRequest> request)
{
    YRLOG_WARN("{}|slave service receive put request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncDelete(const litebus::AID &from,
                                                                   std::shared_ptr<messages::MetaStoreRequest> request)
{
    YRLOG_WARN("{}|slave service receive delete request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncGet(const litebus::AID &from,
                                                                std::shared_ptr<messages::MetaStoreRequest> request)
{
    YRLOG_WARN("{}|slave service receive get request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncTxn(const litebus::AID &from,
                                                                std::shared_ptr<messages::MetaStoreRequest> request)
{
    YRLOG_WARN("{}|slave service receive txn request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncWatch(const litebus::AID &from,
                                                                  std::shared_ptr<messages::MetaStoreRequest> request)
{
    YRLOG_WARN("{}|slave service receive watch request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}

litebus::Future<Status> KvServiceActor::SlaveBusiness::AsyncGetAndWatch(
    const litebus::AID &from, std::shared_ptr<messages::MetaStoreRequest> request)
{
    YRLOG_WARN("{}|slave service receive get and watch request, ignore", request->requestid());
    return Status(StatusCode::FAILED);
}
}  // namespace functionsystem::meta_store
