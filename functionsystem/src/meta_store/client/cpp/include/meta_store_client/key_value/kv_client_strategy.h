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

#ifndef COMMON_META_STORE_CLIENT_KEY_VALUE_KV_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_KEY_VALUE_KV_CLIENT_STRATEGY_H

#include "meta_store_client/meta_store_struct.h"
#include "meta_store_client/txn_transaction.h"
#include "common/metadata/metadata.h"
#include "watcher.h"

namespace functionsystem::meta_store {
using ObserverFunction = std::function<bool(const std::vector<WatchEvent> &, bool)>;
struct WatchRecord {
    std::string uuid;

    std::string key;

    WatchOption option;

    ObserverFunction observer;

    SyncerFunction syncer;

    std::shared_ptr<Watcher> watcher;

    bool isSync;
};

struct GetAndWatchHandlers {
    ObserverFunction observer;
    SyncerFunction syncer;
    ResponseHandler responseHandler;
};

class KvClientStrategy : public litebus::ActorBase {
public:
    KvClientStrategy(const std::string &name, const std::string &address, const MetaStoreTimeoutOption &timeoutOption,
                     const std::string &etcdTablePrefix = "")
        : ActorBase(name), address_(address), etcdTablePrefix_(etcdTablePrefix), timeoutOption_(timeoutOption)
    {
    }
    ~KvClientStrategy() override = default;

    virtual litebus::Future<std::shared_ptr<PutResponse>> Put(const std::string &key, const std::string &value,
                                                              const PutOption &option) = 0;
    virtual litebus::Future<std::shared_ptr<DeleteResponse>> Delete(const std::string &key,
                                                                    const DeleteOption &option) = 0;
    virtual litebus::Future<std::shared_ptr<GetResponse>> Get(const std::string &key, const GetOption &option) = 0;
    litebus::Future<std::shared_ptr<TxnResponse>> Commit(const std::vector<TxnCompare> &compares,
                                                         const std::vector<TxnOperation> &thenOps,
                                                         const std::vector<TxnOperation> &elseOps);

    virtual litebus::Future<std::shared_ptr<Watcher>> Watch(const std::string &key, const WatchOption &option,
                                                            const ObserverFunction &observer,
                                                            const SyncerFunction &syncer,
                                                            const std::shared_ptr<WatchRecord> &reconnectRecord) = 0;

    virtual litebus::Future<std::shared_ptr<Watcher>> GetAndWatch(
        const std::string &key, const WatchOption &option, const ObserverFunction &observer,
        const SyncerFunction &syncer, const std::shared_ptr<WatchRecord> &reconnectRecord) = 0;

    virtual litebus::Future<std::shared_ptr<Watcher>> GetAndWatchWithHandler(
        const std::string &key, const WatchOption &option, const GetAndWatchHandlers &handlers,
        const std::shared_ptr<WatchRecord> &reconnectRecord) = 0;

    virtual void OnHealthyStatus(const Status &status);
    virtual void OnAddressUpdated(const std::string &address) = 0;
    virtual litebus::Future<bool> IsConnected() = 0;

    virtual litebus::Future<std::shared_ptr<TxnResponse>> CommitTxn(const ::etcdserverpb::TxnRequest &request,
                                                                    bool asyncBackup) = 0;

    virtual litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> CommitWithReq(
        const ::etcdserverpb::TxnRequest &request, bool asyncBackup) = 0;

    virtual bool ReconnectWatch();

public:
    static void Convert(const std::shared_ptr<etcdserverpb::TxnResponse> &input, std::shared_ptr<TxnResponse> &output);
    static void ConvertPutResponse(const ::etcdserverpb::ResponseOp &op, std::shared_ptr<TxnResponse> &output);
    static void ConvertRangeResponse(const ::etcdserverpb::ResponseOp &op, std::shared_ptr<TxnResponse> &output);
    static void ConvertDeleteRangeResponse(const ::etcdserverpb::ResponseOp &op, std::shared_ptr<TxnResponse> &output);
    static void ConvertGetRespToWatchResp(int64_t watchId, const etcdserverpb::RangeResponse &input,
                                          WatchResponse &output);

protected:
    virtual void CancelWatch(int64_t watchId);

    void CancelUnknownWatch(int64_t watchId);

    Status OnEvent(const std::shared_ptr<WatchResponse> &response, bool synced);

    Status OnCreate(const std::shared_ptr<WatchRecord> &record);

    void HandleUnknownEvent(const std::shared_ptr<WatchResponse> &response, bool synced);

    litebus::Future<Status> Sync(size_t index);

    litebus::Future<Status> SyncAll();

    virtual litebus::Future<Status> SyncAndReWatch(int64_t watchId);

    Status ReWatch(int64_t watchId);

    virtual litebus::Future<Status> OnCancel(const std::shared_ptr<WatchResponse> &rsp);

    std::shared_ptr<etcdserverpb::WatchRequest> Build(const std::string &key, const WatchOption &option);

    static void Convert(const mvccpb::Event &input, WatchEvent &output);

    void BuildTxnRequest(::etcdserverpb::TxnRequest &request, const std::vector<TxnCompare> &compares,
                         const std::vector<TxnOperation> &thenOps, const std::vector<TxnOperation> &elseOps) const;
    void BuildRangeRequest(etcdserverpb::RangeRequest &request, const std::string &key, const GetOption &option) const;

    const std::string GetKeyWithPrefix(const std::string &key) const;

protected:
    std::string address_;
    std::string etcdTablePrefix_;
    MetaStoreTimeoutOption timeoutOption_;
    Status healthyStatus_ = Status::OK();
    Status syncState_ = Status::OK();

    std::vector<std::shared_ptr<WatchRecord>> records_;
    std::map<int64_t, std::shared_ptr<WatchRecord>> readyRecords_;
    std::unordered_map<int64_t, litebus::Promise<bool>> unknownWatchers_;
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_KEY_VALUE_KV_CLIENT_STRATEGY_H
