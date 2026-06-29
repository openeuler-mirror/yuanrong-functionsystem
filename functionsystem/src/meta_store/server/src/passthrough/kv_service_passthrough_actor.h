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

#ifndef FUNCTION_MASTER_META_STORE_KV_SERVICE_PASSTHROUGH_ACTOR_H
#define FUNCTION_MASTER_META_STORE_KV_SERVICE_PASSTHROUGH_ACTOR_H

#include "meta_store_client/meta_store_client.h"
#include "meta_store/server/src/kv_service_actor.h"

namespace functionsystem::meta_store {
class KvServicePassthroughActor : public KvServiceActor {
public:
    explicit KvServicePassthroughActor(const std::shared_ptr<MetaStoreClient> &etcdClient);

    ~KvServicePassthroughActor() override = default;

    litebus::Future<Status> AsyncPut(const litebus::AID &from,
                                     std::shared_ptr<messages::MetaStore::PutRequest> request) override;

    litebus::Future<Status> AsyncDelete(const litebus::AID &from,
                                        std::shared_ptr<messages::MetaStoreRequest> request) override;

    litebus::Future<Status> AsyncGet(const litebus::AID &from,
                                     std::shared_ptr<messages::MetaStoreRequest> request) override;

    litebus::Future<Status> AsyncTxn(const litebus::AID &from,
                                     std::shared_ptr<messages::MetaStoreRequest> request) override;

    Status OnAsyncPut(const std::string &from, const std::shared_ptr<messages::MetaStore::PutRequest> &request,
                      const std::shared_ptr<PutResponse> &putResponse);

    Status OnAsyncDelete(const std::string &from, const std::shared_ptr<messages::MetaStoreRequest> &request,
                         const std::shared_ptr<DeleteResponse> &deleteResponse);

    Status OnAsyncGet(const std::string &from, const std::shared_ptr<messages::MetaStoreRequest> &request,
                      const std::shared_ptr<GetResponse> &getResponse);

    Status OnTxn(const std::string &from, const std::shared_ptr<messages::MetaStoreRequest> &request,
                 const std::shared_ptr<::etcdserverpb::TxnResponse> &txnResponse);

    void CheckAndCreateWatchServiceActor() override;

    litebus::Future<Status> AsyncWatch(const litebus::AID &from,
                                       std::shared_ptr<messages::MetaStoreRequest> request) override;
    litebus::Future<Status> AsyncGetAndWatch(const litebus::AID &from,
                                             std::shared_ptr<messages::MetaStoreRequest> request) override;

    litebus::Future<Status> OnAsyncGetAndWatch(const litebus::AID &from, const std::string &uid,
                                               std::shared_ptr<::etcdserverpb::WatchCreateRequest> watchRequest,
                                               std::shared_ptr<::etcdserverpb::WatchResponse> watchResponse) override;

private:
    std::shared_ptr<MetaStoreClient> etcdClient_;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_KV_SERVICE_PASSTHROUGH_ACTOR_H
