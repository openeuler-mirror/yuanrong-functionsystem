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

#ifndef FUNCTION_MASTER_META_STORE_ETCD_KV_SERVICE_H
#define FUNCTION_MASTER_META_STORE_ETCD_KV_SERVICE_H

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "kv_service_actor.h"

namespace functionsystem::meta_store::test {
class EtcdKvService final : public etcdserverpb::KV::Service {
public:
    EtcdKvService() = delete;

    explicit EtcdKvService(std::shared_ptr<KvServiceActor> actor);

    ~EtcdKvService() override = default;

    ::grpc::Status Put(::grpc::ServerContext *context, const ::etcdserverpb::PutRequest *request,
        ::etcdserverpb::PutResponse *response) override;

    ::grpc::Status DeleteRange(::grpc::ServerContext *context, const ::etcdserverpb::DeleteRangeRequest *request,
        ::etcdserverpb::DeleteRangeResponse *response) override;

    ::grpc::Status Range(::grpc::ServerContext *context, const ::etcdserverpb::RangeRequest *request,
        ::etcdserverpb::RangeResponse *response) override;

    ::grpc::Status Txn(::grpc::ServerContext *context, const ::etcdserverpb::TxnRequest *request,
        ::etcdserverpb::TxnResponse *response) override;

private:
    std::shared_ptr<KvServiceActor> actor_;
}; // class EtcdKvService
} // namespace functionsystem::meta_store

#endif // FUNCTION_MASTER_META_STORE_ETCD_KV_SERVICE_H
