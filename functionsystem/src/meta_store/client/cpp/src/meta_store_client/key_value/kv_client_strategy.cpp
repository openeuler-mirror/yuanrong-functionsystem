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

#include "kv_client_strategy.h"

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "async/defer.hpp"
#include "meta_store_client/utils/etcd_util.h"

namespace functionsystem::meta_store {
const uint64_t UNKNOWN_WATCHER_CANCEL_WAIT_MS = 5000;
void KvClientStrategy::Convert(const std::shared_ptr<etcdserverpb::TxnResponse> &input,
                               std::shared_ptr<TxnResponse> &output)
{
    for (int i = 0; i < input->responses_size(); i++) {
        const ::etcdserverpb::ResponseOp &op = input->responses(i);
        switch (op.response_case()) {
            case etcdserverpb::ResponseOp::kResponseRange: {
                ConvertRangeResponse(op, output);
                break;
            }
            case etcdserverpb::ResponseOp::kResponsePut: {
                ConvertPutResponse(op, output);
                break;
            }
            case etcdserverpb::ResponseOp::kResponseDeleteRange: {
                ConvertDeleteRangeResponse(op, output);
                break;
            }
            case etcdserverpb::ResponseOp::kResponseTxn:
            case etcdserverpb::ResponseOp::RESPONSE_NOT_SET:
            default: {
                // not support
                // etcdserverpb::ResponseOp::kResponseTxn
                // etcdserverpb::ResponseOp::RESPONSE_NOT_SET
                break;
            }
        }
    }
    output->success = input->succeeded();
}

void KvClientStrategy::ConvertRangeResponse(const ::etcdserverpb::ResponseOp &op, std::shared_ptr<TxnResponse> &output)
{
    GetResponse target;
    const etcdserverpb::RangeResponse &source = op.response_range();
    Transform(target.header, source.header());
    for (int j = 0; j < source.kvs_size(); j++) {
        (void)target.kvs.emplace_back(source.kvs(j));
    }
    target.count = source.count();  // for count only

    TxnOperationResponse operationResponse;
    operationResponse.response = std::move(target);
    operationResponse.operationType = TxnOperationType::OPERATION_GET;
    Transform(operationResponse.header, source.header());

    (void)output->responses.emplace_back(operationResponse);
}

void KvClientStrategy::ConvertPutResponse(const ::etcdserverpb::ResponseOp &op, std::shared_ptr<TxnResponse> &output)
{
    PutResponse target;
    const etcdserverpb::PutResponse &source = op.response_put();
    Transform(target.header, source.header());
    target.prevKv = source.prev_kv();

    TxnOperationResponse operationResponse;
    operationResponse.response = std::move(target);
    operationResponse.operationType = TxnOperationType::OPERATION_PUT;
    Transform(operationResponse.header, source.header());

    (void)output->responses.emplace_back(operationResponse);
}

void KvClientStrategy::ConvertDeleteRangeResponse(const ::etcdserverpb::ResponseOp &op,
                                                  std::shared_ptr<TxnResponse> &output)
{
    DeleteResponse target;
    const etcdserverpb::DeleteRangeResponse &source = op.response_delete_range();
    Transform(target.header, source.header());
    for (int j = 0; j < source.prev_kvs_size(); j++) {
        (void)target.prevKvs.emplace_back(source.prev_kvs(j));
    }
    target.deleted = source.deleted();

    TxnOperationResponse operationResponse;
    operationResponse.response = std::move(target);
    operationResponse.operationType = TxnOperationType::OPERATION_DELETE;
    Transform(operationResponse.header, source.header());

    (void)output->responses.emplace_back(operationResponse);
}

void KvClientStrategy::ConvertGetRespToWatchResp(int64_t watchId, const etcdserverpb::RangeResponse &input,
                                                 WatchResponse &output)
{
    *output.mutable_header() = input.header();
    output.set_watch_id(watchId);
    for (const auto &kv : input.kvs()) {
        ::mvccpb::Event *event = output.add_events();
        event->set_type(::mvccpb::Event_EventType::Event_EventType_PUT);
        *event->mutable_kv() = kv;
    }
}

std::shared_ptr<etcdserverpb::WatchRequest> KvClientStrategy::Build(const std::string &key, const WatchOption &option)
{
    auto request = std::make_shared<etcdserverpb::WatchRequest>();
    auto *args = request->mutable_create_request();
    std::string realKey = GetKeyWithPrefix(key);
    args->set_key(realKey);
    args->set_prev_kv(option.prevKv);
    // Obtain version from the previous response
    args->set_start_revision(option.revision);
    if (option.prefix) {  // prefix
        args->set_range_end(StringPlusOne(realKey));
    }
    return request;
}

void KvClientStrategy::Convert(const mvccpb::Event &input, WatchEvent &output)
{
    output.kv = input.kv();
    if (input.has_prev_kv()) {
        output.prevKv = input.prev_kv();
    }
}

void KvClientStrategy::CancelWatch(int64_t watchId)
{
    for (auto record = records_.cbegin(); record != records_.cend();) {
        if (record->get()->watcher->IsCanceled()) {
            record = records_.erase(record);
        } else {
            (void)++record;
        }
    }
}

void KvClientStrategy::CancelUnknownWatch(int64_t watchId)
{
    if (readyRecords_.find(watchId) != readyRecords_.end()) {
        YRLOG_DEBUG("watcher({}) is created, skip cancel", watchId);
        return;
    }

    // wait created confirm timeout
    if (auto iter = unknownWatchers_.find(watchId); iter != unknownWatchers_.end()) {
        YRLOG_ERROR("cancel unknown watcher({}) events", watchId);
        iter->second.SetFailed(litebus::Status::KERROR);
        unknownWatchers_.erase(iter);
    }

    CancelWatch(watchId);
}

Status KvClientStrategy::OnEvent(const std::shared_ptr<WatchResponse> &response, bool synced)
{
    auto iterator = readyRecords_.find(response->watch_id());
    if (iterator == readyRecords_.end()) {
        YRLOG_ERROR("can not find a watcher({}) for events", response->watch_id());
        HandleUnknownEvent(response, synced);
        return Status::OK();
    }

    if (iterator->second->watcher->IsCanceled()) {
        YRLOG_WARN("events on canceled watcher({})", response->watch_id());
        readyRecords_.erase(response->watch_id());
        return Status::OK();
    }

    YRLOG_DEBUG("watcher: {} received {} events, revision: {}.", response->watch_id(), response->events_size(),
                response->header().revision());

    // Listen to the returned revision to ensure that the revision is not compacted.
    // need to use the revision while re-watching
    iterator->second->option.revision = response->header().revision() + 1;
    std::vector<WatchEvent> events;
    for (int i = 0; i < response->events_size(); i++) {
        const ::mvccpb::Event &event = response->events(i);
        if (event.type() == ::mvccpb::Event_EventType::Event_EventType_PUT) {
            WatchEvent watchEvent;
            watchEvent.eventType = EventType::EVENT_TYPE_PUT;
            Convert(event, watchEvent);
            (void)events.emplace_back(watchEvent);
            continue;
        }

        if (event.type() == ::mvccpb::Event_EventType::Event_EventType_DELETE) {
            WatchEvent watchEvent;
            watchEvent.eventType = EventType::EVENT_TYPE_DELETE;
            Convert(event, watchEvent);
            (void)events.emplace_back(watchEvent);
            continue;
        }

        YRLOG_WARN("the event's type is not supported for key({})", event.kv().key());
    }

    (void)iterator->second->observer(events, synced);
    return Status::OK();
}

Status KvClientStrategy::OnCreate(const std::shared_ptr<WatchRecord> &record)
{
    auto watchID = record->watcher->GetWatchId();
    readyRecords_[watchID] = record;

    if (auto iter = unknownWatchers_.find(watchID); iter != unknownWatchers_.end()) {
        YRLOG_INFO("watcher({}) is succeed to create, replay early events", watchID);
        iter->second.SetValue(true);
        unknownWatchers_.erase(iter);
    }
    return Status::OK();
}

void KvClientStrategy::HandleUnknownEvent(const std::shared_ptr<WatchResponse> &response, bool synced)
{
    // wait cancel non-existed watch
    litebus::AsyncAfter(UNKNOWN_WATCHER_CANCEL_WAIT_MS, GetAID(), &KvClientStrategy::CancelUnknownWatch,
                        response->watch_id());

    // add unknown event into queue for replay, after creation's confirmation
    unknownWatchers_[response->watch_id()].GetFuture().Then(
        litebus::Defer(GetAID(), &KvClientStrategy::OnEvent, response, synced));
}

bool KvClientStrategy::ReconnectWatch()
{
    readyRecords_.clear();
    unknownWatchers_.clear();
    for (const auto &record : records_) {
        if (record == nullptr || record->watcher->IsCanceled()) {
            continue;
        }
        YRLOG_INFO("re-watch key:{} from revision:{}, watcherid: {}", record->key, record->option.revision,
                   record->watcher->GetWatchId());
        record->watcher->Reset();
        (void)Watch(record->key, record->option, record->observer, record->syncer, record);
    }
    return true;
}

litebus::Future<Status> KvClientStrategy::Sync(size_t index)
{
    if (index >= records_.size() || records_[index] == nullptr || records_[index]->syncer == nullptr) {
        return Status(StatusCode::FAILED);
    }

    readyRecords_.erase(records_[index]->watcher->GetWatchId());
    auto record = records_[index];
    GetOption getOption = { .prefix = record->option.prefix };
    YRLOG_INFO("start to sync leader key({}) for watcher({})", record->key, record->watcher->GetWatchId());
    return Get(record->key, getOption)
        .Then([record](const std::shared_ptr<GetResponse> &getResponse) -> litebus::Future<Status> {
            if (getResponse == nullptr || getResponse->status.IsError()) {
                YRLOG_ERROR("failed to get for key: {} during sync", record->key);
                return Status::OK();
            }
            record->option.revision = getResponse->header.revision;
            return record->syncer(getResponse).Then([record](const SyncResult &syncResult) -> litebus::Future<Status> {
                if (syncResult.status.IsError()) {
                    YRLOG_WARN("sync key {}, watcher({}) failed", record->key, record->watcher->GetWatchId());
                } else {
                    // if sync successfully, set new revision after sync from etcd
                    YRLOG_INFO("sync key {} watcher({}) success, set revision {}", record->key,
                               record->watcher->GetWatchId(), record->option.revision);
                }
                return Status::OK();
            });
        });
}

litebus::Future<Status> KvClientStrategy::SyncAll()
{
    std::list<litebus::Future<Status>> futures;
    for (uint32_t index = 0; index < records_.size(); ++index) {
        futures.push_back(Sync(index));
    }
    return litebus::Collect(futures).Then([]() { return Status::OK(); });
}

litebus::Future<Status> KvClientStrategy::SyncAndReWatch(int64_t watchId)
{
    for (uint32_t index = 0; index < records_.size(); ++index) {
        if (records_[index] == nullptr || records_[index]->watcher == nullptr
            || records_[index]->watcher->GetWatchId() != watchId) {
            continue;
        }
        return Sync(index).Then(litebus::Defer(GetAID(), &KvClientStrategy::ReWatch, watchId));
    }
    YRLOG_WARN("failed to sync and re-watch watcher({}), failed to find", watchId);
    return Status::OK();
}

Status KvClientStrategy::ReWatch(int64_t watchId)
{
    readyRecords_.erase(watchId);
    for (const auto &record : records_) {
        if (record == nullptr || record->watcher == nullptr || record->watcher->GetWatchId() != watchId
            || record->watcher->IsCanceled()) {
            continue;
        }
        YRLOG_INFO("re-watch key:{} from revision:{}, watcher id: {}", record->key, record->option.revision,
                   record->watcher->GetWatchId());
        record->watcher->Reset();
        (void)Watch(record->key, record->option, record->observer, record->syncer, record);
        return Status::OK();
    }
    YRLOG_WARN("failed to re-watch({}), failed to find", watchId);
    return Status::OK();
}

litebus::Future<Status> KvClientStrategy::OnCancel(const std::shared_ptr<WatchResponse> &rsp)
{
    auto iter = readyRecords_.find(rsp->watch_id());
    if (iter == readyRecords_.end()) {
        YRLOG_ERROR("can not find a watcher({}) for cancel events", rsp->watch_id());
        return Status::OK();
    }

    if (iter->second->watcher->IsCanceled()) {
        YRLOG_INFO("success to cancel watcher({})", rsp->watch_id());
        readyRecords_.erase(rsp->watch_id());
        return Status::OK();
    }

    // OnCancelImpl by children
    return Status(StatusCode::FAILED);
}

void KvClientStrategy::OnHealthyStatus(const Status &status)
{
    if (healthyStatus_.StatusCode() == status.StatusCode()) {
        return;
    }

    YRLOG_WARN("update kv client healthy status: {}, from: {}", status.ToString(), healthyStatus_.ToString());
    healthyStatus_ = status;
}

void KvClientStrategy::BuildTxnRequest(::etcdserverpb::TxnRequest &request, const std::vector<TxnCompare> &compares,
                                       const std::vector<TxnOperation> &thenOps,
                                       const std::vector<TxnOperation> &elseOps) const
{
    for (const auto &cmp : compares) {
        cmp.Build(*request.add_compare(), etcdTablePrefix_);
    }

    for (const auto &op : thenOps) {
        op.Build(*request.add_success(), etcdTablePrefix_);
    }

    for (const auto &op : elseOps) {
        op.Build(*request.add_failure(), etcdTablePrefix_);
    }
}

void KvClientStrategy::BuildRangeRequest(etcdserverpb::RangeRequest &request, const std::string &key,
                                         const GetOption &option) const
{
    std::string realKey = GetKeyWithPrefix(key);
    request.set_key(realKey);
    if (option.prefix) {  // prefix
        request.set_range_end(StringPlusOne(realKey));
    }
    request.set_limit(option.limit);
    request.set_keys_only(option.keysOnly);
    request.set_count_only(option.countOnly);
    request.set_sort_order(static_cast<etcdserverpb::RangeRequest_SortOrder>(option.sortOrder));
    request.set_sort_target(static_cast<etcdserverpb::RangeRequest_SortTarget>(option.sortTarget));
}

const std::string KvClientStrategy::GetKeyWithPrefix(const std::string &key) const
{
    if (etcdTablePrefix_.empty()) {
        return key;
    }
    return etcdTablePrefix_ + key;
}

litebus::Future<std::shared_ptr<TxnResponse>> KvClientStrategy::Commit(const std::vector<TxnCompare> &compares,
                                                                       const std::vector<TxnOperation> &thenOps,
                                                                       const std::vector<TxnOperation> &elseOps)
{
    ::etcdserverpb::TxnRequest request;
    BuildTxnRequest(request, compares, thenOps, elseOps);

    bool asyncBackup = true;
    // check if any op needs sync backup
    for (const auto &op : thenOps) {
        if (!op.GetAsyncBackupOption()) {
            asyncBackup = false;
            break;
        }
    }
    if (asyncBackup) {
        for (const auto &op : elseOps) {
            if (!op.GetAsyncBackupOption()) {
                asyncBackup = false;
                break;
            }
        }
    }
    return CommitTxn(request, asyncBackup);
}
}  // namespace functionsystem::meta_store