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

#include "backup_actor.h"

#include "async/asyncafter.hpp"

#include "async/collect.hpp"
#include "common/logs/logging.h"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"

namespace functionsystem::meta_store {
const uint32_t CHECK_ASYNC_BACKUP_INTERVAL = 2 * 60 * 1000;  // 2min
const std::set<std::string> SYSTEM_FUNC_SYMBOL{ "0-system-faasscheduler", "0-system-faasmanager",
                                                "0-system-faasfrontend", "0-system-faascontroller" };
bool IsSystemFuncInstanceInfo(const std::string &key)
{
    for (const auto &it : SYSTEM_FUNC_SYMBOL) {
        if (key.find(it) != std::string::npos) {
            return true;
        }
    }
    return false;
}

BackupActor::BackupActor(const std::string &name, const litebus::AID &persistor,
                         const MetaStoreBackupOption &backupOption)
    : ActorBase(name),
      persistor_(persistor),
      enableSyncSysFunc_(backupOption.enableSyncSysFunc),
      metaStoreMaxFlushConcurrency_(backupOption.metaStoreMaxFlushConcurrency),
      metaStoreMaxFlushBatchSize_(backupOption.metaStoreMaxFlushBatchSize),
      currentFlushThreshold_(backupOption.metaStoreMaxFlushConcurrency)
{
}

void BackupActor::Init()
{
    timer_ = litebus::AsyncAfter(CHECK_ASYNC_BACKUP_INTERVAL, GetAID(), &BackupActor::TriggerAsyncBackup);
}

void BackupActor::Finalize()
{
    litebus::TimerTools::Cancel(timer_);
}

void BackupActor::TriggerAsyncBackup()
{
    FlushAsync();
    timer_ = litebus::AsyncAfter(CHECK_ASYNC_BACKUP_INTERVAL, GetAID(), &BackupActor::TriggerAsyncBackup);
}

litebus::Future<Status> BackupActor::WritePut(const KeyValue &kv, const bool asyncBackup)
{
    WritePutInternal(kv, asyncBackup);
    return CheckFlush(asyncBackup);
}

litebus::Future<Status> BackupActor::WriteDeletes(const DeleteResults &kvs, const bool asyncBackup)
{
    WriteDeletesInternal(kvs, asyncBackup);
    return CheckFlush(asyncBackup);
}

litebus::Future<Status> BackupActor::WriteTxn(const TxnResults &txn, const bool asyncBackup)
{
    size_t writes = 0;
    YRLOG_DEBUG("backup transaction starts");
    for (const auto &puts : txn.first) {
        WritePutInternal(puts.first, asyncBackup);
        writes++;
    }
    for (const auto &kvs : txn.second) {
        if (WriteDeletesInternal(kvs, asyncBackup)) {
            writes += kvs->size();
        }
    }
    YRLOG_DEBUG("backup transaction ends, total writes: {}", writes);
    if (writes == 0) {
        return Status::OK();
    }
    return CheckFlush(asyncBackup);
}

litebus::Future<Status> BackupActor::CheckFlush(const bool asyncBackup)
{
    if (asyncBackup) {
        if (toBackupAsync_.size() >= metaStoreMaxFlushBatchSize_) {
            FlushAsync();
        }
        // if async back, we don't care return-value
        return Status::OK();
    } else {
        auto promise = std::make_shared<litebus::Promise<Status>>();
        promises_.emplace_back(promise);
        if (toBackup_.size() >= metaStoreMaxFlushBatchSize_) {
            Flush();
        } else {
            litebus::Async(GetAID(), &BackupActor::Flush);
        }
        return promise->GetFuture();
    }
}

bool BackupActor::WritePutInternal(const KeyValue &kv, const bool asyncBackup)
{
    YRLOG_DEBUG("backup put {}", kv.key());
    std::size_t kvSize = kv.ByteSizeLong();
    if (asyncBackup) {
        if ((kvSize + toBackupAsyncSize_ >= ETCD_BODY_LIMIT)) {
            YRLOG_DEBUG("backup async size {} reach limit {}", toBackupAsyncSize_ + kvSize, ETCD_BODY_LIMIT);
            FlushAsync();
        }
        toBackupAsync_[kv.key()] = kv;
        toBackupAsyncSize_ += kvSize;
    } else {
        if ((kvSize + toBackupSize_ >= ETCD_BODY_LIMIT)) {
            YRLOG_DEBUG("backup size {} reach limit {}", toBackupSize_ + kvSize, ETCD_BODY_LIMIT);
            Flush();
        }
        toBackup_[kv.key()] = kv;
        toBackupSize_ += kvSize;
    }
    return true;
}

bool BackupActor::WriteDeletesInternal(const DeleteResults &kvs, const bool asyncBackup)
{
    if (kvs == nullptr) {
        return false;
    }
    for (const auto kv : *kvs) {
        YRLOG_DEBUG("backup delete {}", kv.key());
        if (asyncBackup) {
            toBackupAsync_[kv.key()] = litebus::None();
        } else {
            toBackup_[kv.key()] = litebus::None();
        }
    }
    return true;
}

bool BackupActor::SetPromises(const std::shared_ptr<TxnResponse> &response,
                              const std::vector<std::shared_ptr<litebus::Promise<Status>>> &committedPromises)
{
    YRLOG_DEBUG("backup done, set {} promises", committedPromises.size());
    if (committedPromises.size() == 0) {
        inFlushingAsync_ = false;
        if (!toFlushAsync_.empty() && !toBackupAsync_.empty()) {
            litebus::Async(GetAID(), &BackupActor::FlushAsync);
        } else {
            litebus::Async(GetAID(), &BackupActor::DoFlushForAsync);
        }
        return true;
    }
    inFlushing_--;
    for (auto &promise : committedPromises) {
        promise->SetValue(response->status);
    }
    if (response->status.IsOk()) {
        if (currentFlushThreshold_ < metaStoreMaxFlushConcurrency_) {
            currentFlushThreshold_++;
        }
    } else {
        if (currentFlushThreshold_ > 1) {
            currentFlushThreshold_--;
        }
    }
    if (inFlushing_ == 0) {
        litebus::Async(GetAID(), &BackupActor::DoFlush);
    }
    return true;
}

void BackupActor::Flush()
{
    if (toBackup_.empty()) {
        return;
    }
    auto backUpSize = toBackup_.size();
    toFlush_.push_back(std::make_pair(std::move(toBackup_), toBackupSize_));
    toBackup_ = {};
    toBackupSize_ = 0;
    toFlushPromises_.push_back(std::move(promises_));
    promises_ = {};
    if (backUpSize >= metaStoreMaxFlushBatchSize_) {
        DoFlush();
    } else {
        litebus::Async(GetAID(), &BackupActor::DoFlush);
    }
}

void BackupActor::FlushAsync()
{
    if (toBackupAsync_.empty()) {
        return;
    }
    auto backUpSize = toBackupAsync_.size();
    toFlushAsync_.push_back(std::make_pair(std::move(toBackupAsync_), toBackupAsyncSize_));
    toBackupAsync_ = {};
    toBackupAsyncSize_ = 0;
    if (backUpSize >= metaStoreMaxFlushBatchSize_) {
        DoFlushForAsync();
    } else {
        litebus::Async(GetAID(), &BackupActor::DoFlushForAsync);
    }
}

void BackupActor::DoFlush()
{
    if (toFlush_.empty()) {
        return;
    }
    if (inFlushing_ >= currentFlushThreshold_) {
        YRLOG_INFO("inFlushing({}) reach threshold({}), delay to flush", inFlushing_, currentFlushThreshold_);
        return;
    }
    std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> toBackup{};
    std::vector<std::shared_ptr<litebus::Promise<Status>>> promises{};
    std::size_t bodySize = 0;
    while (!toFlush_.empty() && !toFlushPromises_.empty() && toBackup.size() <= metaStoreMaxFlushBatchSize_) {
        auto backupSize = toFlush_.front().second;
        if (bodySize + backupSize >= ETCD_BODY_LIMIT) {
            YRLOG_INFO("MessageSize({}) reach limit ({}), break to flush", bodySize + backupSize);
            break;
        }
        auto frontend = toFlush_.front().first;
        auto promisesFrontend = toFlushPromises_.front();
        toBackup.reserve(toBackup.size() + frontend.size());
        promises.reserve(promises.size() + promisesFrontend.size());
        for (auto it = std::make_move_iterator(frontend.begin()); it != std::make_move_iterator(frontend.end()); it++) {
            if (it->second.IsNone()) {
                toBackup[it->first] = litebus::None();
            } else {
                toBackup[it->first] = std::move(it->second.Get());
            }
        }
        promises.insert(promises.end(), std::make_move_iterator(promisesFrontend.begin()),
                        std::make_move_iterator(promisesFrontend.end()));
        bodySize += backupSize;
        toFlush_.pop_front();
        toFlushPromises_.pop_front();
    }
    if (toBackup.empty()) {
        return;
    }
    inFlushing_++;
    CommitBackup(toBackup, promises);
}

void BackupActor::DoFlushForAsync()
{
    if (toFlushAsync_.empty()) {
        return;
    }
    if (inFlushingAsync_) {
        return;
    }
    std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> toBackup{};
    std::size_t bodySize = 0;
    while (!toFlushAsync_.empty() && toBackup.size() <= metaStoreMaxFlushBatchSize_) {
        auto backupSize = toFlushAsync_.front().second;
        if (bodySize + backupSize >= ETCD_BODY_LIMIT) {
            YRLOG_INFO("MessageSize({}) reach limit ({}), break to flush async", backupSize);
            break;
        }
        auto frontend = toFlushAsync_.front().first;
        toBackup.reserve(toBackup.size() + frontend.size());
        for (auto it = std::make_move_iterator(frontend.begin()); it != std::make_move_iterator(frontend.end()); it++) {
            if (it->second.IsNone()) {
                toBackup[it->first] = litebus::None();
            } else {
                toBackup[it->first] = std::move(it->second.Get());
            }
        }
        bodySize += backupSize;
        toFlushAsync_.pop_front();
    }
    if (toBackup.empty()) {
        return;
    }
    inFlushingAsync_ = true;
    CommitBackup(toBackup, {});
}

void BackupActor::CommitBackup(const std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> &toBackup,
                               const std::vector<std::shared_ptr<litebus::Promise<Status>>> &promises)
{
    std::vector<TxnCompare> cmp;
    std::vector<TxnOperation> thenOps;
    std::vector<TxnOperation> elseOps;
    thenOps.reserve(toBackup.size());
    PutOption putOption = { .leaseId = 0, .prevKv = false };
    DeleteOption delOption = { .prevKv = false, .prefix = false };
    size_t deletes = 0;
    size_t puts = 0;
    for (const auto &kv : toBackup) {
        if (kv.second.IsNone()) {  // delete
            thenOps.emplace_back(TxnOperation::Create(META_STORE_BACKUP_KV_PREFIX + kv.first, delOption));
            if (enableSyncSysFunc_ && IsSystemFuncInstanceInfo(kv.first)) {
                thenOps.emplace_back(TxnOperation::Create(kv.first, delOption));
            }
            deletes++;
        } else {  // put
            auto val = kv.second.Get();
            thenOps.emplace_back(
                TxnOperation::Create(META_STORE_BACKUP_KV_PREFIX + kv.first, val.SerializeAsString(), putOption));
            if (enableSyncSysFunc_ && IsSystemFuncInstanceInfo(kv.first)) {
                thenOps.emplace_back(TxnOperation::Create(kv.first, val.value(), putOption));
            }
            puts++;
        }
    }
    YRLOG_DEBUG("backup flush {} kvs, put: {}, delete: {}, promises: {}", thenOps.size(), puts, deletes,
                promises.size());
    (void)litebus::Async(persistor_, &KvClientStrategy::Commit, cmp, thenOps, elseOps)
        .Then(litebus::Defer(GetAID(), &BackupActor::SetPromises, std::placeholders::_1, promises));
}

litebus::Future<std::shared_ptr<DeleteResponse>> BackupActor::Delete(const std::string &key, const DeleteOption &option)
{
    return litebus::Async(persistor_, &KvClientStrategy::Delete, key, option);
}

litebus::Future<std::shared_ptr<GetResponse>> BackupActor::Get(const std::string &key, const GetOption &option)
{
    return litebus::Async(persistor_, &KvClientStrategy::Get, key, option);
}

litebus::Future<std::shared_ptr<PutResponse>> BackupActor::Put(const std::string &key, const std::string &value,
                                                               const PutOption &option)
{
    return litebus::Async(persistor_, &KvClientStrategy::Put, key, value, option);
}

litebus::Future<std::shared_ptr<Watcher>> BackupActor::Watch(const std::string &key, const WatchOption &option,
                                                             const ObserverFunction &observer,
                                                             const SyncerFunction &syncer)
{
    return litebus::Async(persistor_, &KvClientStrategy::Watch, key, option, observer, syncer, nullptr);
}
}  // namespace functionsystem::meta_store
