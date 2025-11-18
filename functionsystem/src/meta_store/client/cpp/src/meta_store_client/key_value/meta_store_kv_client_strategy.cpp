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

#include "meta_store_kv_client_strategy.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "meta_store_client/utils/etcd_util.h"
#include "common/proto/pb/message_pb.h"
#include "common/utils/random_number.h"

namespace functionsystem::meta_store {
MetaStoreKvClientStrategy::MetaStoreKvClientStrategy(const std::string &name, const std::string &address,
                                                     const MetaStoreTimeoutOption &timeoutOption,
                                                     const std::string &etcdTablePrefix)
    : KvClientStrategy(name, address, timeoutOption, etcdTablePrefix)
{
    kvServiceAid_ = std::make_shared<litebus::AID>("KvServiceAccessorActor", address_);
    auto backOff = [lower(timeoutOption_.operationRetryIntervalLowerBound),
                    upper(timeoutOption_.operationRetryIntervalUpperBound),
                    base(timeoutOption_.grpcTimeout * 1000)](int64_t attempt) {
        return GenerateRandomNumber(base + lower * attempt, base + upper * attempt);
    };
    watchHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    putHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    deleterHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    getHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    txnHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
}

void MetaStoreKvClientStrategy::Init()
{
    YRLOG_INFO("MetaStore strategy init...");
    Receive("OnPut", &MetaStoreKvClientStrategy::OnPut);
    Receive("OnDelete", &MetaStoreKvClientStrategy::OnDelete);
    Receive("OnGet", &MetaStoreKvClientStrategy::OnGet);
    Receive("OnTxn", &MetaStoreKvClientStrategy::OnTxn);
    Receive("OnWatch", &MetaStoreKvClientStrategy::OnWatch);
    Receive("OnGetAndWatch", &MetaStoreKvClientStrategy::OnGetAndWatch);
}

void MetaStoreKvClientStrategy::Finalize()
{
    YRLOG_INFO("MetaStore strategy finalize...");
    for (auto &record : std::as_const(readyRecords_)) {
        if (record.second->watcher->IsCanceled()) {
            continue;
        }
        CancelWatch(record.second->watcher->GetWatchId());
    }
    readyRecords_.clear();

    records_.clear();
    pendingRecordMap_.clear();
}

litebus::Future<std::shared_ptr<PutResponse>> MetaStoreKvClientStrategy::Put(const std::string &key,
                                                                             const std::string &value,
                                                                             const PutOption &option)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto response = std::make_shared<PutResponse>();
        response->status =
            Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Put: " + healthyStatus_.GetMessage());
        return response;
    }

    messages::MetaStore::PutRequest request;

    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    request.set_requestid(uuid.ToString());

    request.set_key(GetKeyWithPrefix(key));
    request.set_value(value);
    request.set_lease(option.leaseId);
    request.set_prevkv(option.prevKv);
    request.set_asyncbackup(option.asyncBackup);

    YRLOG_DEBUG("{}|Put {}:{} into MetaStoreServer", request.requestid(), request.key(), request.value());
    return putHelper_.Begin(request.requestid(), kvServiceAid_, "Put", request.SerializeAsString());
}

void MetaStoreKvClientStrategy::OnPut(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStore::PutResponse response;
    if (!response.ParseFromString(msg)) {
        YRLOG_ERROR("Failed to parse the MetaStore response.");
        return;
    }

    auto ret = std::make_shared<PutResponse>();
    ret->status = Status(static_cast<StatusCode>(response.status()), response.errormsg());
    ret->header.revision = response.revision();

    if (!response.prevkv().empty()) {
        ret->prevKv.ParseFromString(response.prevkv());
    }

    YRLOG_DEBUG("{}|Success to Put key-value into MetaStoreServer", response.requestid());
    putHelper_.End(response.requestid(), std::move(ret));
}

litebus::Future<std::shared_ptr<DeleteResponse>> MetaStoreKvClientStrategy::Delete(const std::string &key,
                                                                                   const DeleteOption &option)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto response = std::make_shared<DeleteResponse>();
        response->status =
            Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Delete: " + healthyStatus_.GetMessage());
        return response;
    }
    // makeup Delete request
    etcdserverpb::DeleteRangeRequest request;
    std::string realKey = GetKeyWithPrefix(key);
    request.set_key(realKey);
    if (option.prefix) {  // prefix
        request.set_range_end(StringPlusOne(realKey));
    }
    request.set_prev_kv(option.prevKv);
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<DeleteResponse>>>();
    messages::MetaStoreRequest message;
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    message.set_requestid(uuid.ToString());
    message.set_requestmsg(request.SerializeAsString());
    message.set_asyncbackup(option.asyncBackup);

    YRLOG_DEBUG("{}|Delete {} from MetaStoreServer.", message.requestid(), realKey);
    return deleterHelper_.Begin(message.requestid(), kvServiceAid_, "Delete", message.SerializeAsString());
}

void MetaStoreKvClientStrategy::OnDelete(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Delete MetaStoreResponse");

    etcdserverpb::DeleteRangeResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Delete DeleteRangeResponse: " + res.responseid());

    auto ret = std::make_shared<DeleteResponse>();
    ret->status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret->header, response.header());
    YRLOG_DEBUG("Success Delete {},  key-value is deleted", response.deleted());
    ret->deleted = response.deleted();
    for (int i = 0; i < response.prev_kvs_size(); i++) {
        (void)ret->prevKvs.emplace_back(response.prev_kvs(i));
    }
    deleterHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<std::shared_ptr<GetResponse>> MetaStoreKvClientStrategy::Get(const std::string &key,
                                                                             const GetOption &option)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto response = std::make_shared<GetResponse>();
        response->status =
            Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Get: " + healthyStatus_.GetMessage());
        return response;
    }
    // makeup Get request
    etcdserverpb::RangeRequest request;
    BuildRangeRequest(request, key, option);
    messages::MetaStoreRequest message;
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    message.set_requestid(uuid.ToString());
    message.set_requestmsg(request.SerializeAsString());

    YRLOG_DEBUG("{}|Get {} from MetaStoreServer.", message.requestid(), key);
    return getHelper_.Begin(message.requestid(), kvServiceAid_, "Get", message.SerializeAsString());
}

void MetaStoreKvClientStrategy::OnGet(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Get MetaStoreResponse");

    etcdserverpb::RangeResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Get RangeResponse: " + res.responseid());

    auto ret = std::make_shared<GetResponse>();
    ret->status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret->header, response.header());
    YRLOG_DEBUG("{}|Success to Get {} key-value is found", res.responseid(), response.kvs_size());
    for (int i = 0; i < response.kvs_size(); i++) {
        (void)ret->kvs.emplace_back(response.kvs(i));
    }
    ret->count = response.count();
    getHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<std::shared_ptr<TxnResponse>> MetaStoreKvClientStrategy::CommitTxn(
    const ::etcdserverpb::TxnRequest &request, bool asyncBackup)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto response = std::make_shared<TxnResponse>();
        response->status =
            Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call CommitTxn: " + healthyStatus_.GetMessage());
        return response;
    }
    messages::MetaStoreRequest message;

    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    message.set_requestid(uuid.ToString());
    message.set_requestmsg(request.SerializeAsString());
    message.set_asyncbackup(asyncBackup);

    YRLOG_DEBUG("{}|Commit Txn to MetaStoreServer", message.requestid());
    return txnHelper_.Begin(message.requestid(), kvServiceAid_, "Txn", message.SerializeAsString())
        .Then([requestID(message.requestid())](const std::shared_ptr<::etcdserverpb::TxnResponse> &response) {
            // trans etcdserverpb::TxnResponse to TxnResponse
            std::shared_ptr<TxnResponse> ret = std::make_shared<TxnResponse>();
            if (response->has_header()) {
                Transform(ret->header, response->header());
                YRLOG_DEBUG("{}|Success to OnTxn", requestID);
                KvClientStrategy::Convert(response, ret);
            } else {
                ret->status = Status(StatusCode::FAILED, "etcd txn failed");
            }
            return ret;
        });
}

litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> MetaStoreKvClientStrategy::CommitWithReq(
    const ::etcdserverpb::TxnRequest &request, bool asyncBackup)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        return std::make_shared<::etcdserverpb::TxnResponse>();
    }
    messages::MetaStoreRequest message;
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    message.set_requestid(uuid.ToString());
    message.set_requestmsg(request.SerializeAsString());
    message.set_asyncbackup(asyncBackup);

    YRLOG_DEBUG("{}|Commit Txn with etcd proto to MetaStoreServer", message.requestid());
    return txnHelper_.Begin(message.requestid(), kvServiceAid_, "Txn", message.SerializeAsString());
}

void MetaStoreKvClientStrategy::OnTxn(const litebus::AID &, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse message;
    RETURN_IF_TRUE(!message.ParseFromString(msg), "failed to parse Txn MetaStoreResponse");

    auto response = std::make_shared<etcdserverpb::TxnResponse>();
    if (message.status() != 0) {
        YRLOG_DEBUG("{}|failed to Txn, error: {}, {}", message.responseid(), message.status(), message.errormsg());
        txnHelper_.End(message.responseid(), std::move(response));
        return;
    }

    RETURN_IF_TRUE(!response->ParseFromString(message.responsemsg()), "illegal txn response");

    YRLOG_DEBUG("{}|Success to Txn", message.responseid());
    txnHelper_.End(message.responseid(), std::move(response));
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreKvClientStrategy::WatchInternal(
    std::string &&method, const std::string &key, const WatchOption &option,
    const std::shared_ptr<WatchFunctionParam> funcParam,
    const std::shared_ptr<WatchRecord> &reconnectRecord)
{
    (void)reconnectRecord;
    auto record = reconnectRecord;
    if (record == nullptr) {
        record = std::make_shared<WatchRecord>();
        (void)records_.emplace_back(record);
    }

    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    record->uuid = uuid;
    record->key = key;
    record->option = option;
    record->observer = funcParam->observer;
    record->syncer = funcParam->syncer;
    if (record->watcher == nullptr) {
        record->watcher = std::make_shared<Watcher>([aid(GetAID())](int64_t watchID) {
            litebus::Async(aid, &MetaStoreKvClientStrategy::CancelWatch, watchID);
        });
    }

    pendingRecordMap_[uuid] = record;

    messages::MetaStoreRequest message;
    auto request = Build(key, option);
    message.set_requestmsg(request->SerializeAsString());
    message.set_requestid(std::move(uuid));
    if (funcParam->needAddHandler) {
        auto handKey = key + message.requestid();
        responseHandlerMap_[handKey] = funcParam->handler;
        YRLOG_DEBUG("add handler for key {}", handKey);
    }
    YRLOG_DEBUG("{}|Send {} to MetaStoreServer, key {}", message.requestid(), method, key);
    (void)watchHelper_.Begin(message.requestid(), kvServiceAid_, std::move(method), message.SerializeAsString());

    return record->watcher;
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreKvClientStrategy::Watch(
    const std::string &key, const WatchOption &option, const ObserverFunction &observer, const SyncerFunction &syncer,
    const std::shared_ptr<WatchRecord> &reconnectRecord)
{
    auto param = std::make_shared<WatchFunctionParam>(
        WatchFunctionParam{ .observer = observer, .syncer = syncer, .handler = nullptr, .needAddHandler = false });
    return WatchInternal("Watch", key, option, param, reconnectRecord);
}

void MetaStoreKvClientStrategy::OnWatch(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse message;
    if (!message.ParseFromString(msg)) {
        YRLOG_ERROR("illegal OnWatch msg");
        return;
    }

    auto response = std::make_shared<etcdserverpb::WatchResponse>();
    if (!response->ParseFromString(message.responsemsg())) {
        YRLOG_ERROR("illegal OnWatch response");
        return;
    }

    YRLOG_DEBUG("{}|receive watch callback events", message.responseid());
    if (response->created()) {
        if (watchServiceActorAID_ == nullptr) {
            watchServiceActorAID_ = std::make_shared<litebus::AID>(from);
        }

        (void)OnCreateWithID(response, message.responseid());
    } else if (response->canceled()) {
        (void)OnCancel(response);
    } else {
        (void)OnEvent(response, false);
    }
}

void MetaStoreKvClientStrategy::CancelWatch(int64_t watchId)
{
    KvClientStrategy::CancelWatch(watchId);

    if (watchServiceActorAID_ != nullptr) {
        YRLOG_INFO("Cancel a watcher({})", watchId);

        etcdserverpb::WatchRequest request;
        request.mutable_cancel_request()->set_watch_id(watchId);

        messages::MetaStoreRequest message;
        message.set_requestmsg(request.SerializeAsString());
        message.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());

        Send(*watchServiceActorAID_, "Cancel", message.SerializeAsString());
    } else {
        YRLOG_ERROR("Illegal Watch Service Actor AID");
    }
}

Status MetaStoreKvClientStrategy::OnCreateWithID(const std::shared_ptr<WatchResponse> &response,
                                                 const std::string &uuid,
                                                 const std::shared_ptr<etcdserverpb::RangeResponse> &getResponse)
{
    watchHelper_.End(uuid, true);

    auto iter = pendingRecordMap_.find(uuid);
    if (iter == pendingRecordMap_.end()) {
        YRLOG_ERROR("watcher not found to match {}", response->watch_id());
        CancelWatch(response->watch_id());
        return Status(StatusCode::FAILED, "watcher not found");
    }

    auto record = iter->second;
    RETURN_STATUS_IF_NULL(record, StatusCode::FAILED, "null record");
    if (record->watcher->IsCanceled()) {
        YRLOG_ERROR("the watcher({}) for key({}) has been canceled", response->watch_id(), record->key);
        CancelWatch(response->watch_id());
        return Status(StatusCode::FAILED, "watcher has been canceled");
    }

    if (record->isSync && getResponse != nullptr && record->syncer != nullptr) {
        record->isSync = false;
        YRLOG_INFO("get response for key({}), sync {} key", record->key, getResponse->kvs_size());
        auto output = std::make_shared<GetResponse>();
        Transform(output->header, getResponse->header());
        for (int i = 0; i < getResponse->kvs_size(); i++) {
            (void)output->kvs.emplace_back(getResponse->kvs(i));
        }
        output->count = getResponse->count();
        record->syncer(output);
    }

    const int64_t watchId = response->watch_id();
    record->watcher->SetWatchId(watchId);
    pendingRecordMap_.erase(uuid);
    YRLOG_INFO("watcher({}) is created for key({})", watchId, record->key);
    return KvClientStrategy::OnCreate(record);
}

litebus::Future<bool> MetaStoreKvClientStrategy::IsConnected()
{
    return true;
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreKvClientStrategy::GetAndWatch(
    const std::string &key, const WatchOption &option, const ObserverFunction &observer, const SyncerFunction &syncer,
    const std::shared_ptr<WatchRecord> &reconnectRecord)
{
    auto param = std::make_shared<WatchFunctionParam>(
        WatchFunctionParam{ .observer = observer, .syncer = syncer, .handler = nullptr, .needAddHandler = false });
    return WatchInternal("GetAndWatch", key, option, param, reconnectRecord);
}

litebus::Future<std::shared_ptr<Watcher>> MetaStoreKvClientStrategy::GetAndWatchWithHandler(
    const std::string &key, const WatchOption &option, const GetAndWatchHandlers &handlers,
    const std::shared_ptr<WatchRecord> &reconnectRecord)
{
    auto param = std::make_shared<WatchFunctionParam>(
        WatchFunctionParam{ handlers.observer, handlers.syncer, handlers.responseHandler, true });
    return WatchInternal("GetAndWatch", key, option, param, reconnectRecord);
}

void MetaStoreKvClientStrategy::OnGetAndWatch(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::MetaStoreResponse message;
    if (!message.ParseFromString(msg)) {
        YRLOG_ERROR("illegal OnGetAndWatch msg");
        return;
    }

    messages::GetAndWatchResponse response;
    if (!response.ParseFromString(message.responsemsg())) {
        YRLOG_ERROR("illegal OnGetAndWatch response");
        return;
    }

    YRLOG_DEBUG("{}|receive get and watch callback events", message.responseid());

    auto getResponse = std::make_shared<etcdserverpb::RangeResponse>();
    auto watchResponse = std::make_shared<etcdserverpb::WatchResponse>();
    if (!getResponse->ParseFromString(response.getresponsemsg())) {
        YRLOG_ERROR("illegal get response");
        return;
    }
    if (!watchResponse->ParseFromString(response.watchresponsemsg())) {
        YRLOG_ERROR("illegal watch response");
        return;
    }

    ASSERT_FS(watchResponse->created());
    if (watchServiceActorAID_ == nullptr) {
        watchServiceActorAID_ = std::make_shared<litebus::AID>(from);
    }
    (void)OnCreateWithID(watchResponse, message.responseid(), getResponse);

    auto iterator = readyRecords_.find(watchResponse->watch_id());
    if (iterator == readyRecords_.end()) {
        YRLOG_ERROR("can not find a watcher({})!", watchResponse->watch_id());
        return;
    }
    auto handleKey = iterator->second->key + message.responseid();
    if (auto handler = responseHandlerMap_.find(handleKey); handler != responseHandlerMap_.end()) {
        YRLOG_DEBUG("success find key {} handler, execute it", handleKey);
        handler->second(TransformToGetResponse(getResponse))
            .Then(litebus::Defer(GetAID(), &MetaStoreKvClientStrategy::GetAndWatchEnd, getResponse, watchResponse,
                                 handleKey));
        return;
    }

    (void)GetAndWatchEnd(getResponse, watchResponse, "");
}

litebus::Future<Status> MetaStoreKvClientStrategy::GetAndWatchEnd(
    const std::shared_ptr<etcdserverpb::RangeResponse> &getResponse,
    const std::shared_ptr<WatchResponse> &watchResponse, const std::string &handleKey)
{
    if (!handleKey.empty()) {
        responseHandlerMap_.erase(handleKey);
        YRLOG_DEBUG("erase handler for key {}", handleKey);
    }

    YRLOG_DEBUG("process get response for watch id {}, event size: {}", watchResponse->watch_id(),
                getResponse->kvs_size());
    if (!getResponse->kvs().empty()) {
        auto output = std::make_shared<etcdserverpb::WatchResponse>();
        ConvertGetRespToWatchResp(watchResponse->watch_id(), *getResponse, *output);
        (void)OnEvent(output, true);
    }
    return Status::OK();
}

bool MetaStoreKvClientStrategy::ReconnectWatch()
{
    YRLOG_INFO("reconnect to meta-store success");
    pendingRecordMap_.clear();
    unknownWatchers_.clear();
    readyRecords_.clear();
    for (auto record : records_) {
        if (record != nullptr && record->watcher != nullptr && !record->watcher->IsCanceled()) {
            YRLOG_INFO("re-watch key:{} with sync from {}, watcher id: {}", record->key, record->option.revision,
                       record->watcher->GetWatchId());
            record->watcher->Reset();
            record->isSync = true;
            (void)GetAndWatch(record->key, record->option, record->observer, record->syncer, record);
        }
    }
    return true;
}

litebus::Future<Status> MetaStoreKvClientStrategy::SyncAndReWatch(int64_t watchId)
{
    readyRecords_.erase(watchId);
    for (const auto &record : records_) {
        if (record == nullptr || record->watcher == nullptr || record->watcher->GetWatchId() != watchId
            || record->watcher->IsCanceled()) {
            continue;
        }

        YRLOG_INFO("re-watch key:{} with sync from {}, watcher id: {}", record->key, record->option.revision,
                   record->watcher->GetWatchId());
        record->watcher->Reset();
        record->isSync = true;
        (void)GetAndWatch(record->key, record->option, record->observer, record->syncer, record);
        return Status::OK();
    }
    YRLOG_WARN("failed to sync and re-watch watcher({}), failed to find", watchId);
    return Status::OK();
}

void MetaStoreKvClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_DEBUG("kv client update address from {} to {}", address_, address);
    address_ = address;
    kvServiceAid_->SetUrl(address);

    // cancel all old watches to old address
    for (const auto &record : readyRecords_) {
        if (record.second == nullptr || record.second->watcher->IsCanceled()) {
            continue;
        }
        YRLOG_INFO("cancel key:{} from revision:{}, watcher id: {}", record.second->key, record.second->option.revision,
                   record.first);
        CancelWatch(record.first);
    }
    watchServiceActorAID_ = nullptr;
    ReconnectWatch();
}

litebus::Future<Status> MetaStoreKvClientStrategy::OnCancel(const std::shared_ptr<WatchResponse> &rsp)
{
    auto status = KvClientStrategy::OnCancel(rsp);
    if (status.IsOK() && status.Get().IsOk()) {
        YRLOG_INFO("success to cancel watcher({})", rsp->watch_id());
        return status;
    }

    YRLOG_WARN("watcher({}) is canceled by server, try to reconnect", rsp->watch_id());
    return SyncAndReWatch(rsp->watch_id());
}

void MetaStoreKvClientStrategy::FallBreakResponse()
{
    auto putResponse = std::make_shared<PutResponse>();
    putResponse->status =
        Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Put: " + healthyStatus_.GetMessage());
    putHelper_.EndAll(putResponse);

    auto DelResponse = std::make_shared<DeleteResponse>();
    DelResponse->status =
        Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Delete: " + healthyStatus_.GetMessage());
    deleterHelper_.EndAll(DelResponse);

    auto getResponse = std::make_shared<GetResponse>();
    getResponse->status =
        Status(healthyStatus_.StatusCode(), "[fallbreak] failed to call Get: " + healthyStatus_.GetMessage());
    getHelper_.EndAll(getResponse);

    auto nullTxnResponse = std::make_shared<::etcdserverpb::TxnResponse>();
    txnHelper_.EndAll(nullTxnResponse);
}

void MetaStoreKvClientStrategy::OnHealthyStatus(const Status &status)
{
    KvClientStrategy::OnHealthyStatus(status);
    if (status.IsOk()) {
        // sync after healthy
        SyncAll().OnComplete(litebus::Defer(GetAID(), &MetaStoreKvClientStrategy::ReconnectWatch));
    }
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        FallBreakResponse();
    }
}

std::shared_ptr<GetResponse> MetaStoreKvClientStrategy::TransformToGetResponse(
    std::shared_ptr<etcdserverpb::RangeResponse> response)
{
    auto output = std::make_shared<GetResponse>();
    Transform(output->header, response->header());
    for (int i = 0; i < response->kvs_size(); i++) {
        (void)output->kvs.emplace_back(response->kvs(i));
    }
    output->count = response->count();
    return output;
}

}  // namespace functionsystem::meta_store
