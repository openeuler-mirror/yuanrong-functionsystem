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

#include "kv_service_passthrough_actor.h"

#include "common/explorer/explorer.h"
#include "meta_store_client/utils/etcd_util.h"
#include "watch_service_passthrough_actor.h"

namespace functionsystem::meta_store {
using namespace functionsystem::explorer;

KvServicePassthroughActor::KvServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient)
    : KvServiceActor(), etcdClient_(etcdClient)
{
}

void KvServicePassthroughActor::CheckAndCreateWatchServiceActor()
{
    if (!watchServiceActor_.OK()) {
        YRLOG_DEBUG("create passthrough watch service actor");
        auto watchSrvActor = std::make_shared<WatchServicePassthroughActor>(etcdClient_);
        watchServiceActor_ = litebus::Spawn(watchSrvActor);
    }
}

litebus::Future<Status> KvServicePassthroughActor::AsyncWatch(const litebus::AID &from,
                                                              std::shared_ptr<messages::MetaStoreRequest> request)
{
    return KvServiceActor::AsyncWatch(from, request);
}

litebus::Future<Status> KvServicePassthroughActor::AsyncGetAndWatch(const litebus::AID &from,
                                                                    std::shared_ptr<messages::MetaStoreRequest> request)
{
    return KvServiceActor::AsyncGetAndWatch(from, request);
}

litebus::Future<Status> KvServicePassthroughActor::OnAsyncGetAndWatch(
    const litebus::AID &from, const std::string &uuid, std::shared_ptr<::etcdserverpb::WatchCreateRequest> watchRequest,
    std::shared_ptr<::etcdserverpb::WatchResponse> watchResponse)
{
    GetOption opt{ .prefix = !watchRequest->range_end().empty() };
    return etcdClient_->Get(watchRequest->key(), opt)
        .Then([from, uuid, watchResponse,
               aid(watchServiceActor_)](const std::shared_ptr<GetResponse> &response) -> Status {
            ::etcdserverpb::RangeResponse ret;
            Transform(ret.mutable_header(), response->header);
            for (auto kv : response->kvs) {
                ret.mutable_kvs()->Add(std::move(kv));
            }
            ret.set_count(response->count);

            messages::GetAndWatchResponse gwResponse;
            gwResponse.set_getresponsemsg(ret.SerializeAsString());
            gwResponse.set_watchresponsemsg(watchResponse->SerializeAsString());
            messages::MetaStoreResponse res;
            res.set_responseid(uuid);
            res.set_responsemsg(gwResponse.SerializeAsString());
            YRLOG_DEBUG("send GetAndWatch response to {}, watch id: {}, get key count: {}", from.HashString(),
                        watchResponse->watch_id(), ret.kvs_size());
            litebus::Async(aid, &WatchServiceActor::SendResponse, from, "OnGetAndWatch", res);
            return Status::OK();
        });
}

litebus::Future<Status> KvServicePassthroughActor::AsyncPut(const litebus::AID &from,
                                                            std::shared_ptr<messages::MetaStore::PutRequest> request)
{
    if (healthyStatus_.IsError()) {
        auto response = std::make_shared<PutResponse>();
        response->status = Status(StatusCode::FAILED, "[fallbreak] failed to call Put: " + healthyStatus_.GetMessage());
        return OnAsyncPut(from, request, response);
    }

    YRLOG_DEBUG("{}|receive Passthrough Put request", request->requestid());
    PutOption op = { .leaseId = request->lease(), .prevKv = request->prevkv(), .asyncBackup = request->asyncbackup() };
    return etcdClient_->Put(request->key(), request->value(), op)
        .Then(litebus::Defer(GetAID(), &KvServicePassthroughActor::OnAsyncPut, from, request, std::placeholders::_1));
}

Status KvServicePassthroughActor::OnAsyncPut(const std::string &from,
                                             const std::shared_ptr<messages::MetaStore::PutRequest> &request,
                                             const std::shared_ptr<PutResponse> &putResponse)
{
    messages::MetaStore::PutResponse response;
    if (putResponse->status.IsError()) {
        response.set_requestid(request->requestid());
        response.set_status(putResponse->status.StatusCode());
        response.set_errormsg(putResponse->status.GetMessage());
        YRLOG_ERROR("{}|failed to passthrough put, error: {}", request->requestid(), putResponse->status.ToString());
        Send(from, "OnPut", response.SerializeAsString());
        return Status::OK();
    }

    YRLOG_DEBUG("{}|passthrough put response callback to client", request->requestid());
    response.set_requestid(request->requestid());
    response.set_revision(putResponse->header.revision);
    response.set_prevkv(putResponse->prevKv.SerializeAsString());
    Send(from, "OnPut", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServicePassthroughActor::AsyncDelete(const litebus::AID &from,
                                                               std::shared_ptr<messages::MetaStoreRequest> request)
{
    if (healthyStatus_.IsError()) {
        auto response = std::make_shared<DeleteResponse>();
        response->status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call Delete: " + healthyStatus_.GetMessage());
        return OnAsyncDelete(from, request, response);
    }

    ::etcdserverpb::DeleteRangeRequest req;
    RETURN_STATUS_IF_TRUE(!req.ParseFromString(request->requestmsg()), StatusCode::FAILED,
                          request->requestid() + "|failed to parse Passthrough Delete request");
    YRLOG_DEBUG("{}|receive Passthrough Delete request", request->requestid());
    DeleteOption op = { .prevKv = req.prev_kv(),
                        .prefix = !req.range_end().empty(),
                        .asyncBackup = request->asyncbackup() };
    return etcdClient_->Delete(req.key(), op).Then(litebus::Defer(GetAID(), &KvServicePassthroughActor::OnAsyncDelete,
                                                                  from, request, std::placeholders::_1));
}

Status KvServicePassthroughActor::OnAsyncDelete(const std::string &from,
                                                const std::shared_ptr<messages::MetaStoreRequest> &request,
                                                const std::shared_ptr<DeleteResponse> &deleteResponse)
{
    messages::MetaStoreResponse response;
    response.set_responseid(request->requestid());
    if (deleteResponse->status.IsError()) {
        response.set_status(deleteResponse->status.StatusCode());
        response.set_errormsg(deleteResponse->status.GetMessage());
        YRLOG_ERROR("{}|failed to passthrough delete, error: {}", request->requestid(),
                    deleteResponse->status.ToString());
        Send(from, "OnDelete", response.SerializeAsString());
        return Status::OK();
    }

    // trans to etcdserverpb::DeleteRangeResponse
    ::etcdserverpb::DeleteRangeResponse ret;
    Transform(ret.mutable_header(), deleteResponse->header);
    ret.set_deleted(deleteResponse->deleted);
    for (auto prevKv : deleteResponse->prevKvs) {
        ret.mutable_prev_kvs()->Add(std::move(prevKv));
    }
    response.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|passthrough delete response callback to client.", request->requestid());
    Send(from, "OnDelete", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServicePassthroughActor::AsyncGet(const litebus::AID &from,
                                                            std::shared_ptr<messages::MetaStoreRequest> request)
{
    if (healthyStatus_.IsError()) {
        auto response = std::make_shared<GetResponse>();
        response->status = Status(StatusCode::FAILED, "[fallback] failed to call Get: " + healthyStatus_.GetMessage());
        return OnAsyncGet(from, request, response);
    }

    etcdserverpb::RangeRequest req;
    RETURN_STATUS_IF_TRUE(!req.ParseFromString(request->requestmsg()), StatusCode::FAILED,
                          request->requestid() + "|failed to parse Passthorugh Get request");

    YRLOG_DEBUG("{}|receive Passthrough Get request", request->requestid());
    GetOption op = { .prefix = !req.range_end().empty(),
                     .keysOnly = req.keys_only(),
                     .countOnly = req.count_only(),
                     .limit = static_cast<int>(req.limit()),
                     .sortOrder = static_cast<enum SortOrder>(req.sort_order()),
                     .sortTarget = static_cast<enum SortTarget>(req.sort_target()) };
    return etcdClient_->Get(req.key(), op)
        .Then(litebus::Defer(GetAID(), &KvServicePassthroughActor::OnAsyncGet, from, request, std::placeholders::_1));
}

Status KvServicePassthroughActor::OnAsyncGet(const std::string &from,
                                             const std::shared_ptr<messages::MetaStoreRequest> &request,
                                             const std::shared_ptr<GetResponse> &getResponse)
{
    messages::MetaStoreResponse response;
    response.set_responseid(request->requestid());
    if (getResponse->status.IsError()) {
        response.set_status(getResponse->status.StatusCode());
        response.set_errormsg(getResponse->status.GetMessage());
        YRLOG_ERROR("{}|failed to passthrough get, error: {}", request->requestid(), getResponse->status.ToString());
        Send(from, "OnGet", response.SerializeAsString());
        return Status::OK();
    }

    // trans to etcdserverpb::RangeResponse
    ::etcdserverpb::RangeResponse ret;
    Transform(ret.mutable_header(), getResponse->header);
    for (auto kv : getResponse->kvs) {
        ret.mutable_kvs()->Add(std::move(kv));
    }
    ret.set_count(getResponse->count);
    response.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|get response callback to client.", request->requestid());
    Send(from, "OnGet", response.SerializeAsString());
    return Status::OK();
}

litebus::Future<Status> KvServicePassthroughActor::AsyncTxn(const litebus::AID &from,
                                                            std::shared_ptr<messages::MetaStoreRequest> request)
{
    if (healthyStatus_.IsError()) {
        auto response = std::make_shared<::etcdserverpb::TxnResponse>();
        YRLOG_ERROR("{}|failed to passhtrough txn, error: {}", request->requestid(), healthyStatus_.ToString());
        return OnTxn(from, request, response);
    }

    ::etcdserverpb::TxnRequest req;
    RETURN_STATUS_IF_TRUE(!req.ParseFromString(request->requestmsg()), StatusCode::FAILED,
                          request->requestid() + "|failed to parse Passthrough Txn request");
                
    YRLOG_DEBUG("{}|receive Passthrough txn request", request->requestid());
    return etcdClient_->Commit(req, true).Then(
        litebus::Defer(GetAID(), &KvServicePassthroughActor::OnTxn, from, request, std::placeholders::_1));
}

Status KvServicePassthroughActor::OnTxn(const std::string &from,
                                        const std::shared_ptr<messages::MetaStoreRequest> &request,
                                        const std::shared_ptr<::etcdserverpb::TxnResponse> &txnResponse)
{
    RETURN_STATUS_IF_TRUE(txnResponse == nullptr, StatusCode::FAILED, request->requestid() + "|failed to txn");
    messages::MetaStoreResponse response;
    response.set_responseid(request->requestid());
    response.set_responsemsg(txnResponse->SerializeAsString());

    YRLOG_DEBUG("{}|txn response callback to client", request->requestid());
    Send(from, "OnTxn", response.SerializeAsString());
    return Status::OK();
}
}  // namespace functionsystem::meta_store
