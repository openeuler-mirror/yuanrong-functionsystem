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

#ifndef FUNCTION_MASTER_META_STORE_BACKUP_ACTOR_H
#define FUNCTION_MASTER_META_STORE_BACKUP_ACTOR_H

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/defer.hpp"
#include "meta_store_client/key_value/etcd_kv_client_strategy.h"
#include "meta_store_client/meta_store_struct.h"
#include "meta_store_client/txn_transaction.h"
#include "common/proto/pb/message_pb.h"
#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"

namespace functionsystem::meta_store {
const std::string META_STORE_BACKUP_KV_PREFIX = "/metastore/kv/";
const std::size_t ETCD_BODY_LIMIT = 1 * 1024 * 1024;  // 1 MB
using PutResults = std::pair<::mvccpb::KeyValue, ::mvccpb::KeyValue>;
using DeleteResults = std::shared_ptr<std::vector<::mvccpb::KeyValue>>;
using TxnResults = std::pair<std::vector<PutResults>, std::vector<DeleteResults>>;

class BackupActor : public litebus::ActorBase {
public:
    BackupActor() = default;

    BackupActor(const std::string &name, const litebus::AID &persistor, const MetaStoreBackupOption &backupOption = {});

    ~BackupActor() override = default;

public:
    litebus::Future<Status> WritePut(const KeyValue &kv, const bool asyncBackup = true);

    litebus::Future<Status> WriteDeletes(const DeleteResults &kvs, const bool asyncBackup = true);

    litebus::Future<Status> WriteTxn(const TxnResults &txn, const bool asyncBackup = true);

    void Flush();

    void FlushAsync();

    litebus::Future<std::shared_ptr<DeleteResponse>> Delete(const std::string &key, const DeleteOption &option);

    litebus::Future<std::shared_ptr<GetResponse>> Get(const std::string &key, const GetOption &option);

    litebus::Future<std::shared_ptr<PutResponse>> Put(const std::string &key, const std::string &value,
                                                      const PutOption &option);

    litebus::Future<std::shared_ptr<Watcher>> Watch(const std::string &key, const WatchOption &option,
                                                    const ObserverFunction &observer, const SyncerFunction &syncer);

protected:
    void Init() override;
    void Finalize() override;
    void TriggerAsyncBackup();
    bool WritePutInternal(const KeyValue &kv, const bool asyncBackup = true);
    bool WriteDeletesInternal(const DeleteResults &kvs, const bool asyncBackup = true);
    bool SetPromises(const std::shared_ptr<TxnResponse> &response,
                     const std::vector<std::shared_ptr<litebus::Promise<Status>>> &committedPromises);
    litebus::Future<Status> CheckFlush(const bool asyncBackup);
    void DoFlush();
    void DoFlushForAsync();
    void CommitBackup(const std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> &toBackup,
                      const std::vector<std::shared_ptr<litebus::Promise<Status>>> &promises);

protected:
    litebus::AID persistor_;
    std::size_t toBackupAsyncSize_{ 0 };
    std::size_t toBackupSize_{ 0 };
    std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> toBackup_;
    std::vector<std::shared_ptr<litebus::Promise<Status>>> promises_;
    std::deque<std::pair<std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>>, std::size_t>> toFlush_;
    std::deque<std::vector<std::shared_ptr<litebus::Promise<Status>>>> toFlushPromises_;
    // for async, requests need needs to execute in sequence.
    std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>> toBackupAsync_;
    std::deque<std::pair<std::unordered_map<std::string, litebus::Option<::mvccpb::KeyValue>>, std::size_t>>
        toFlushAsync_;
    bool enableSyncSysFunc_{ false };
    uint32_t metaStoreMaxFlushConcurrency_{ 0 };
    uint32_t metaStoreMaxFlushBatchSize_{ 0 };
    uint32_t currentFlushThreshold_{ 0 };
    uint32_t inFlushing_{ 0 };
    bool inFlushingAsync_{ false };
    litebus::Timer timer_;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_BACKUP_ACTOR_H
