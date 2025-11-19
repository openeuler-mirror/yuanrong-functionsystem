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

#include "etcd_maintenance_client_strategy.h"

#include "common/utils/actor_worker.h"

namespace functionsystem::meta_store {
EtcdMaintenanceClientStrategy::EtcdMaintenanceClientStrategy(const std::string &name, const std::string &address,
                                                             const std::shared_ptr<MetaStoreExplorer> &explorer,
                                                             const MetaStoreTimeoutOption &timeoutOption,
                                                             const GrpcSslConfig &sslConfig)
    : MaintenanceClientStrategy(name, address, explorer, timeoutOption)
{
    client_ = GrpcClient<etcdserverpb::Maintenance>::CreateGrpcClient(address, sslConfig);
    ASSERT_IF_NULL(client_);
}

litebus::Future<bool> EtcdMaintenanceClientStrategy::IsConnected()
{
    ASSERT_IF_NULL(client_);
    return client_->IsConnected();
}

litebus::Future<StatusResponse> EtcdMaintenanceClientStrategy::HealthCheck()
{
    auto promise = std::make_shared<litebus::Promise<StatusResponse>>();
    if (client_ == nullptr) {
        YRLOG_ERROR("client is null");
        StatusResponse ret;
        ret.status = Status(static_cast<StatusCode>(FAILED), "client is null");
        return ret;
    }

    // makeup StatusRequest
    etcdserverpb::StatusRequest request;
    (void)client_
        ->CallAsync("Status", request, static_cast<etcdserverpb::StatusResponse *>(nullptr),
                    &etcdserverpb::Maintenance::Stub::AsyncStatus, timeoutOption_.grpcTimeout)
        .Then([promise, aid(GetAID())](litebus::Try<etcdserverpb::StatusResponse> rsp) {
            if (rsp.IsOK() && rsp.Get().errors().empty()) {
                StatusResponse ret;
                promise->SetValue(std::move(ret));
                return Status::OK();
            }

            StatusCode errorCode = StatusCode::FAILED;
            if (rsp.IsError()) {
                YRLOG_ERROR("failed to HealthCheck, err: {}", rsp.GetErrorCode());
                errorCode = static_cast<StatusCode>(rsp.GetErrorCode());
                litebus::Async(aid, &EtcdMaintenanceClientStrategy::CheckChannelAndWaitForReconnect);
            } else {
                // status response ok, but with error
                for (auto &e : rsp.Get().errors()) {
                    YRLOG_ERROR("response ok, but failed to HealthCheck, err: {}", e);
                }
            }

            StatusResponse ret;
            ret.status = Status(errorCode, "failed to health check");
            promise->SetValue(std::move(ret));
            return Status::OK();
        });
    return promise->GetFuture();
}

void EtcdMaintenanceClientStrategy::CheckChannelAndWaitForReconnect()
{
    if (!isReconnecting_) {
        YRLOG_WARN("EtcdMaintenanceClientStrategy try to reconnect");
        isReconnecting_ = true;
        // async check channel, don't block actor
        auto handler = [self(shared_from_this())]() {
            self->client_->CheckChannelAndWaitForReconnect(self->isRunning_);
        };
        auto actor = std::make_shared<ActorWorker>();
        (void)actor->AsyncWork(handler).OnComplete([actor, aid(GetAID())](const litebus::Future<Status> &) {
            actor->Terminate();
            litebus::Async(aid, &EtcdMaintenanceClientStrategy::Reconnected);
        });
    }
}

void EtcdMaintenanceClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_WARN("etcd maintenance client doesn't support address update yet");
}
}  // namespace functionsystem::meta_store
