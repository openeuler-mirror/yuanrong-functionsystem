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

#ifndef COMMON_META_STORE_CLIENT_KEY_VALUE_META_STORE_KV_CLIENT_STRATEGY_H
#define COMMON_META_STORE_CLIENT_KEY_VALUE_META_STORE_KV_CLIENT_STRATEGY_H

#include "meta_store_client/key_value/kv_client_strategy.h"
#include "request_sync_helper.h"

namespace functionsystem::meta_store {
class MetaStoreKvClientStrategy : public KvClientStrategy {
public:
    MetaStoreKvClientStrategy(const std::string &name, const std::string &address,
                              const MetaStoreTimeoutOption &timeoutOption, const std::string &etcdTablePrefix = "");
    ~MetaStoreKvClientStrategy() override = default;
    litebus::Future<std::shared_ptr<PutResponse>> Put(const std::string &key, const std::string &value,
                                                      const PutOption &option) override;
    litebus::Future<std::shared_ptr<DeleteResponse>> Delete(const std::string &key,
                                                            const DeleteOption &option) override;
    litebus::Future<std::shared_ptr<GetResponse>> Get(const std::string &key, const GetOption &option) override;
    litebus::Future<std::shared_ptr<TxnResponse>> CommitTxn(const ::etcdserverpb::TxnRequest &request,
                                                            bool asyncBackup) override;
    litebus::Future<std::shared_ptr<::etcdserverpb::TxnResponse>> CommitWithReq(
        const ::etcdserverpb::TxnRequest &request, bool asyncBackup) override;
    litebus::Future<std::shared_ptr<Watcher>> Watch(const std::string &key, const WatchOption &option,
                                                    const ObserverFunction &observer, const SyncerFunction &syncer,
                                                    const std::shared_ptr<WatchRecord> &reconnectRecord) override;

    litebus::Future<std::shared_ptr<Watcher>> GetAndWatch(const std::string &key, const WatchOption &option,
                                                          const ObserverFunction &observer,
                                                          const SyncerFunction &syncer,
                                                          const std::shared_ptr<WatchRecord> &reconnectRecord) override;

    void OnAddressUpdated(const std::string &address) override;

    litebus::Future<Status> OnCancel(const std::shared_ptr<WatchResponse> &rsp) override;

    void OnPut(const litebus::AID &, std::string &&, std::string &&msg);
    void OnDelete(const litebus::AID &, std::string &&, std::string &&msg);
    void OnGet(const litebus::AID &, std::string &&, std::string &&msg);
    void OnTxn(const litebus::AID &, std::string &&, std::string &&msg);
    void OnWatch(const litebus::AID &from, std::string &&, std::string &&msg);
    void OnGetAndWatch(const litebus::AID &from, std::string &&, std::string &&msg);

    void CancelWatch(int64_t watchId) override;

    litebus::Future<bool> IsConnected() override;

protected:
    void Init() override;
    void Finalize() override;

private:
    Status OnCreateWithID(const std::shared_ptr<WatchResponse> &response, const std::string &uuid);
    litebus::Future<std::shared_ptr<Watcher>> WatchInternal(std::string &&method, const std::string &key,
                                                            const WatchOption &option, const ObserverFunction &observer,
                                                            const SyncerFunction &syncer,
                                                            const std::shared_ptr<WatchRecord> &reconnectRecord);

    void ReconnectSuccess();
    void ReconnectSuccess(uint32_t watchID);
    bool ReconnectWatch() override;

private:
    std::shared_ptr<litebus::AID> kvServiceAid_;
    BACK_OFF_RETRY_HELPER(MetaStoreKvClientStrategy, bool, watchHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreKvClientStrategy, std::shared_ptr<PutResponse>, putHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreKvClientStrategy, std::shared_ptr<DeleteResponse>, deleterHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreKvClientStrategy, std::shared_ptr<GetResponse>, getHelper_)
    BACK_OFF_RETRY_HELPER(MetaStoreKvClientStrategy, std::shared_ptr<::etcdserverpb::TxnResponse>, txnHelper_)
    std::shared_ptr<litebus::AID> watchServiceActorAID_;

    std::unordered_map<std::string, std::shared_ptr<WatchRecord>> pendingRecordMap_;
};
}  // namespace functionsystem::meta_store

#endif  // COMMON_META_STORE_CLIENT_KEY_VALUE_META_STORE_KV_CLIENT_STRATEGY_H
