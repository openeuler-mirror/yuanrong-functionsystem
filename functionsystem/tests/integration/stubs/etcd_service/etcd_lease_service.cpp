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

#include "etcd_lease_service.h"

#include "async/async.hpp"

namespace functionsystem::meta_store::test {
EtcdLeaseService::EtcdLeaseService(std::shared_ptr<LeaseServiceActor> actor) : actor_(std::move(actor))
{
}

::grpc::Status EtcdLeaseService::LeaseGrant(::grpc::ServerContext *context,
    const ::etcdserverpb::LeaseGrantRequest *request, ::etcdserverpb::LeaseGrantResponse *response)
{
    return litebus::Async(actor_->GetAID(), &LeaseServiceActor::LeaseGrant, request, response).Get();
}

::grpc::Status EtcdLeaseService::LeaseRevoke(::grpc::ServerContext *context,
    const ::etcdserverpb::LeaseRevokeRequest *request, ::etcdserverpb::LeaseRevokeResponse *response)
{
    return litebus::Async(actor_->GetAID(), &LeaseServiceActor::LeaseRevoke, request, response).Get();
}

::grpc::Status EtcdLeaseService::LeaseKeepAlive(::grpc::ServerContext *context,
    ::grpc::ServerReaderWriter<::etcdserverpb::LeaseKeepAliveResponse, ::etcdserverpb::LeaseKeepAliveRequest> *stream)
{
    ::etcdserverpb::LeaseKeepAliveRequest request;
    while (stream->Read(&request)) {
        ::etcdserverpb::LeaseKeepAliveResponse response;
        litebus::Async(actor_->GetAID(), &LeaseServiceActor::LeaseKeepAlive, &request, &response).Get();
        if (!stream->Write(response)) {
            break;
        }
    }

    return grpc::Status::OK;
}
}
