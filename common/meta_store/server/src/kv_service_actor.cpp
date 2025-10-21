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
#include "logs/logging.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/utils/etcd_util.h"
#include "lease_service_actor.h"
#include "meta_store_common.h"
#include "watch_service_actor.h"

namespace functionsystem::meta_store {
KvServiceActor::KvServiceActor() : ActorBase("KvServiceActor")
{
}

KvServiceActor::KvServiceActor(const litebus::AID &backupActor) : ActorBase("KvServiceActor"), backupActor_(backupActor)
{
}

KvServiceActor::KvServiceActor(const std::string &namePrefix)
    : ActorBase(namePrefix + "KvServiceActor"), namePrefix_(namePrefix)
{
}

void KvServiceActor::Init()
{
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
    YRLOG_DEBUG("execute watch request");
    etcdserverpb::WatchRequest watchRequest;
    if (!watchRequest.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal watch request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal watch request");
    }
    CheckAndCreateWatchServiceActor();
    return litebus::Async(watchServiceActor_, &WatchServiceActor::Create, from, request->requestid(),
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
    YRLOG_DEBUG("execute get and watch request");
    etcdserverpb::WatchRequest watchRequest;
    if (!watchRequest.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal get and watch request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal get and watch request");
    }
    CheckAndCreateWatchServiceActor();
    auto watchCreateRequest = std::make_shared<::etcdserverpb::WatchCreateRequest>(watchRequest.create_request());
    // get after create watch, in order to ensure the kv is the latest version
    return litebus::Async(watchServiceActor_, &WatchServiceActor::CreateWatch, from, watchCreateRequest)
        .Then(litebus::Defer(GetAID(), &KvServiceActor::OnAsyncGetAndWatch, from, request->requestid(),
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
    YRLOG_DEBUG("send GetAndWatch reponse to {}, watch id: {}, get key count: {}", from.HashString(),
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
                                  const std::shared_ptr<messages::MetaStore::PutResponse> &putResponse)
{
    YRLOG_DEBUG("{}|put response callback to client.", request->requestid());
    Send(from, "OnPut", putResponse->SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServiceActor::AsyncPut(const litebus::AID &from,
                                                 std::shared_ptr<messages::MetaStore::PutRequest> request)
{
    YRLOG_DEBUG("{}|received put request", request->requestid());
    auto response = std::make_shared<messages::MetaStore::PutResponse>();
    (void)PutCache(request, response);
    if (backupActor_.OK()) {
        auto future =
            litebus::Async(backupActor_, &BackupActor::WritePut, cache_[request->key()], request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request](const Status &status) -> bool {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup put: {}, reason: {}", request->requestid(), request->key(),
                                   status.ToString());
                    }
                    return true;
                })
                .Then(litebus::Defer(GetAID(), &KvServiceActor::OnAsyncPut, from, request, response));
        }
    }

    return OnAsyncPut(from, request, response);
}

::mvccpb::KeyValue KvServiceActor::PutCache(std::shared_ptr<messages::MetaStore::PutRequest> request,
                                            std::shared_ptr<messages::MetaStore::PutResponse> response)
{
    response->set_requestid(request->requestid());

    ::mvccpb::KeyValue prevKv = cache_[request->key()];  // 1.copy,temporary
    ::mvccpb::KeyValue &kv = cache_[request->key()];     // 2.get reference
    if (modRevision_ >= std::numeric_limits<int64_t>::max()) {
        YRLOG_WARN("modRevision_ reached maximum value. Auto-reset to 0.");
        modRevision_ = 0;
    } else {
        ++modRevision_;
    }
    kv.set_mod_revision(modRevision_);
    if (!kv.key().empty()) {
        if (request->prevkv()) {  // 3.return preview
            response->set_prevkv(prevKv.SerializeAsString());
        }
        kv.set_version(kv.version() + 1);
    } else {
        kv.set_key(request->key());
        kv.set_version(1);  // reset
        kv.set_create_revision(modRevision_);
    }
    // 4.update,after if(empty)
    kv.set_value(request->value());
    kv.set_lease(request->lease());

    litebus::Async(watchServiceActor_, &WatchServiceActor::OnPut, kv, prevKv);
    litebus::Async(leaseServiceActor_, &LeaseServiceActor::Attach, request->key(), request->lease());

    YRLOG_INFO("success to put key-value, revision: {}, kv.mod_revision: {}.", modRevision_, kv.mod_revision());
    response->set_revision(modRevision_);

    if (request->prevkv()) {
        return prevKv;
    }

    return ::mvccpb::KeyValue();
}

PutResults KvServiceActor::Put(const ::etcdserverpb::PutRequest *etcdPutRequest,
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
    auto prevKv = PutCache(request, response);

    etcdPutResponse->mutable_header()->set_revision(response->revision());
    etcdPutResponse->mutable_header()->set_cluster_id(META_STORE_CLUSTER_ID);
    if (request->prevkv() && !prevKv.key().empty()) {
        *etcdPutResponse->mutable_prev_kv() = std::move(prevKv);
    }

    return cache_[etcdPutRequest->key()];
}

Status KvServiceActor::OnAsyncDelete(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                                     const std::shared_ptr<etcdserverpb::DeleteRangeResponse> &deleteResponse)
{
    messages::MetaStoreResponse response;
    response.set_responseid(request->requestid());
    response.set_responsemsg(deleteResponse->SerializeAsString());

    YRLOG_DEBUG("{}|delete response callback to client.", request->requestid());
    Send(from, "OnDelete", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServiceActor::AsyncDelete(const litebus::AID &from,
                                                    std::shared_ptr<messages::MetaStoreRequest> request)
{
    ::etcdserverpb::DeleteRangeRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal delete request", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal delete request");
    }

    auto response = std::make_shared<::etcdserverpb::DeleteRangeResponse>();
    auto deletes = DeleteRange(&payload, response.get());
    YRLOG_DEBUG("{}|delete {} records for {}.", request->requestid(), response->deleted(), payload.key());
    if (backupActor_.OK()) {
        auto future = litebus::Async(backupActor_, &BackupActor::WriteDeletes, deletes, request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request, key(payload.key())](const Status &status) -> bool {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup delete: {}, reason: {}", request->requestid(), key,
                                   status.ToString());
                    }
                    return true;
                })
                .Then(litebus::Defer(GetAID(), &KvServiceActor::OnAsyncDelete, from, request, response));
        }
    }

    return OnAsyncDelete(from, request, response);
}

DeleteResults KvServiceActor::DeleteRange(const ::etcdserverpb::DeleteRangeRequest *request,
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
        mvccpb::KeyValue &kv = cache_[request->key()];
        if (!kv.key().empty()) {
            deletes->emplace_back(kv);
            if (request->prev_kv()) {
                AddPrevKv(response, kv);
            }
        }
        cache_.erase(request->key());
    } else {
        for (auto iterator = cache_.begin(); iterator != cache_.end();) {
            if (iterator->first < request->key() || iterator->first >= request->range_end()) {
                iterator++;  // not in range, next
                continue;
            }

            ::mvccpb::KeyValue &kv = iterator->second;  // 1.get reference of key-value.
            deletes->emplace_back(kv);                  // 2.add key-value to list and copy.
            if (request->prev_kv()) {                   // 3.makeUp preview key-value.
                AddPrevKv(response, kv);
            }
            cache_.erase(iterator++);  // 3.erase, must iterator++ or iterator = erase.
        }
    }

    response->set_deleted(static_cast<int64_t>(deletes->size()));
    if (!deletes->empty()) {
        litebus::Async(watchServiceActor_, &WatchServiceActor::OnDeleteList, deletes);
    }

    return deletes;
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
    ::etcdserverpb::RangeRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal get payload.", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal get payload");
    }

    auto response = std::make_shared<::etcdserverpb::RangeResponse>();
    Range(&payload, response.get());
    YRLOG_DEBUG("{}|success to get {} cache size:{}", request->requestid(), payload.key(), response->kvs().size());

    return OnAsyncGet(from, request, response);
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
    YRLOG_DEBUG("{}|execute txn request", request->requestid());
    ::etcdserverpb::TxnRequest payload;
    if (!payload.ParseFromString(request->requestmsg())) {
        YRLOG_ERROR("{}|receive illegal txn payload.", request->requestid());
        return Status(StatusCode::FAILED, "receive illegal txn payload");
    }

    auto response = std::make_shared<::etcdserverpb::TxnResponse>();

    auto txn = Txn(&payload, response.get(), request->requestid());
    YRLOG_DEBUG("{}|success to txn cache size: {}", request->requestid(), cache_.size());
    if (backupActor_.OK()) {
        auto future = litebus::Async(backupActor_, &BackupActor::WriteTxn, txn, request->asyncbackup());
        if (!request->asyncbackup()) {
            return future
                .Then([request](const Status &status) -> bool {
                    if (status.IsError()) {
                        YRLOG_WARN("{}|failed to backup txn, reason: {}", request->requestid(), status.ToString());
                    }
                    return true;
                })
                .Then(litebus::Defer(GetAID(), &KvServiceActor::OnAsyncTxn, from, request, response));
        }
    }
    return OnAsyncTxn(from, request, response);
}

Status KvServiceActor::OnAsyncTxn(const std::string &from, std::shared_ptr<messages::MetaStoreRequest> request,
                                  const std::shared_ptr<::etcdserverpb::TxnResponse> &response)
{
    YRLOG_DEBUG("{}|txn response callback to client.", request->requestid());

    messages::MetaStoreResponse message;
    message.set_responseid(request->requestid());
    message.set_responsemsg(response->SerializeAsString());
    Send(from, "OnTxn", message.SerializeAsString());

    return Status::OK();
}

TxnResults KvServiceActor::Txn(const ::etcdserverpb::TxnRequest *request, ::etcdserverpb::TxnResponse *response,
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
            auto put = Put(&putRequest, responseOp->mutable_response_put());
            txn.first.emplace_back(put);
            break;
        }
        case etcdserverpb::RequestOp::kRequestDeleteRange: {
            const auto &deleteRequest = cmp.request_delete_range();
            ::etcdserverpb::ResponseOp *responseOp = response->add_responses();
            auto deletes = DeleteRange(&deleteRequest, responseOp->mutable_response_delete_range());
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
        YRLOG_INFO("success to sync kv({})", item.key().substr(META_STORE_BACKUP_KV_PREFIX.size()));

        // set max mod_revision for current mod reversion
        if (modRevision_ < kv.mod_revision()) {
            modRevision_ = kv.mod_revision();
        }
    }
    YRLOG_INFO("success to sync kvs with mod revision({})", modRevision_);
    return true;
}

void KvServiceActor::OnHealthyStatus(const Status &status)
{
    YRLOG_DEBUG("KvServiceActor health status changes to healthy({})", status.IsOk());
    healthyStatus_ = status;
}
}  // namespace functionsystem::meta_store
