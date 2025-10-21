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

#ifndef FUNCTION_MASTER_META_STORE_ETCD_LEASE_SERVICE_H
#define FUNCTION_MASTER_META_STORE_ETCD_LEASE_SERVICE_H

#include "etcd/api/etcdserverpb/rpc.grpc.pb.h"
#include "lease_service_actor.h"

namespace functionsystem::meta_store::test {
class EtcdLeaseService final : public etcdserverpb::Lease::Service {
public:
    EtcdLeaseService() = delete;

    explicit EtcdLeaseService(std::shared_ptr<LeaseServiceActor> actor);

    ~EtcdLeaseService() override = default;

    ::grpc::Status LeaseGrant(::grpc::ServerContext *context, const ::etcdserverpb::LeaseGrantRequest *request,
        ::etcdserverpb::LeaseGrantResponse *response) override;

    ::grpc::Status LeaseRevoke(::grpc::ServerContext *context, const ::etcdserverpb::LeaseRevokeRequest *request,
        ::etcdserverpb::LeaseRevokeResponse *response) override;

    ::grpc::Status LeaseKeepAlive(::grpc::ServerContext *context,
        ::grpc::ServerReaderWriter<::etcdserverpb::LeaseKeepAliveResponse,
            ::etcdserverpb::LeaseKeepAliveRequest> *stream) override;

private:
    std::shared_ptr<LeaseServiceActor> actor_;
}; // class EtcdLeaseService
} // namespace functionsystem::meta_store

#endif // FUNCTION_MASTER_META_STORE_ETCD_LEASE_SERVICE_H
