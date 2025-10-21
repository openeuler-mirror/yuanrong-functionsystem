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

#include "etcd_kv_service.h"

#include "async/async.hpp"

namespace functionsystem::meta_store::test {
EtcdKvService::EtcdKvService(std::shared_ptr<KvServiceActor> actor) : actor_(std::move(actor))
{
}

::grpc::Status EtcdKvService::Put(::grpc::ServerContext *context,
    const ::etcdserverpb::PutRequest *request, ::etcdserverpb::PutResponse *response)
{
    // Transfer meta_store::PutResponse from actor to ::etcdserverpb::PutResponse
    litebus::Async(actor_->GetAID(), &KvServiceActor::Put, request, response).Get();
    return ::grpc::Status::OK;
}

::grpc::Status EtcdKvService::DeleteRange(::grpc::ServerContext *context,
    const ::etcdserverpb::DeleteRangeRequest *request, ::etcdserverpb::DeleteRangeResponse *response)
{
    litebus::Async(actor_->GetAID(), &KvServiceActor::DeleteRange, request, response).Get();
    return ::grpc::Status::OK;
}

::grpc::Status EtcdKvService::Range(::grpc::ServerContext *context, const ::etcdserverpb::RangeRequest *request,
    ::etcdserverpb::RangeResponse *response)
{
    return litebus::Async(actor_->GetAID(), &KvServiceActor::Range, request, response).Get();
}

::grpc::Status EtcdKvService::Txn(::grpc::ServerContext *context, const ::etcdserverpb::TxnRequest *request,
    ::etcdserverpb::TxnResponse *response)
{
    litebus::Async(actor_->GetAID(), &KvServiceActor::Txn, request, response, "").Get();
    return ::grpc::Status::OK;
}
}
