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

#include "etcd_lease_client_strategy.h"

#include "async/asyncafter.hpp"
#include "meta_store_client/utils/etcd_util.h"
#include "common/proto/pb/message_pb.h"
#include "common/utils/random_number.h"

namespace functionsystem::meta_store {
const uint64_t RECONNECT_TIMEOUT_MS = 15000;
EtcdLeaseClientStrategy::EtcdLeaseClientStrategy(const std::string &name, const std::string &address,
                                                 const GrpcSslConfig &sslConfig,
                                                 const MetaStoreTimeoutOption &timeoutOption)
    : LeaseClientStrategy(name, address, timeoutOption)
{
    leaseClient_ = GrpcClient<etcdserverpb::Lease>::CreateGrpcClient(address, sslConfig);
    ASSERT_IF_NULL(leaseClient_);
}

void EtcdLeaseClientStrategy::Finalize()
{
    YRLOG_INFO("Stop lease Client actor({}).", address_);
    running_ = false;
    try {
        // should call TryCancel before Finish,
        // otherwise Finish would be blocked.
        if (leaseContext_ != nullptr) {
            leaseContext_->TryCancel();
        }
        if (leaseStream_ != nullptr) {
            (void)leaseStream_->WritesDone();
            (void)leaseStream_->Finish();
        }
    } catch (...) {
        YRLOG_ERROR("error to finish lease stream.");
    }

    try {
        if (leaseStreamReadLoopThread_ != nullptr) {
            leaseStreamReadLoopThread_->join();
            leaseStreamReadLoopThread_ = nullptr;
        }
    } catch (const std::system_error &e) {
        YRLOG_ERROR("error to join lease thread.");
    }
}

litebus::Future<LeaseGrantResponse> EtcdLeaseClientStrategy::Grant(int ttl)
{
    etcdserverpb::LeaseGrantRequest request;
    request.set_ttl(ttl);

    auto promise = std::make_shared<litebus::Promise<LeaseGrantResponse>>();
    DoGrant(promise, request, 1);
    return promise->GetFuture();
}

void EtcdLeaseClientStrategy::DoGrant(const std::shared_ptr<litebus::Promise<LeaseGrantResponse>> &promise,
                                      const etcdserverpb::LeaseGrantRequest &request, int retryTimes)
{
    if (healthyStatus_.IsError()) {
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Grant api of etcd");
        return;
    }

    (void)leaseClient_
        ->CallAsync("Grant", request, static_cast<etcdserverpb::LeaseGrantResponse *>(nullptr),
                    &etcdserverpb::Lease::Stub::AsyncLeaseGrant, GRPC_TIMEOUT_SECONDS)
        .Then([aid(GetAID()), promise, request, retryTimes,
               timeoutOption(timeoutOption_)](litebus::Try<etcdserverpb::LeaseGrantResponse> rsp) {
            if (rsp.IsOK()) {
                LeaseGrantResponse ret;
                const auto &response = rsp.Get();
                Transform(ret.header, response.header());
                YRLOG_DEBUG("Success to Grant a lease, id is {}", response.id());
                ret.leaseId = response.id();
                ret.ttl = response.ttl();
                promise->SetValue(ret);
                return Status::OK();
            }
            if (retryTimes == KV_OPERATE_RETRY_TIMES) {
                YRLOG_ERROR("{} to Grant a lease, after {} times", rsp.GetErrorCode(), KV_OPERATE_RETRY_TIMES);
                LeaseGrantResponse ret;
                ret.status = Status(static_cast<StatusCode>(rsp.GetErrorCode()), "grant failed");
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            YRLOG_WARN("{} to Grant a lease, begin to retry", rsp.GetErrorCode());
            auto nextSleepTime = GenerateRandomNumber(timeoutOption.operationRetryIntervalLowerBound,
                                                      timeoutOption.operationRetryIntervalUpperBound);
            (void)litebus::AsyncAfter(nextSleepTime, aid, &EtcdLeaseClientStrategy::DoGrant, promise, request,
                                      retryTimes + 1);
            return Status::OK();
        });
}

litebus::Future<LeaseRevokeResponse> EtcdLeaseClientStrategy::Revoke(int64_t leaseId)
{
    etcdserverpb::LeaseRevokeRequest request;
    request.set_id(leaseId);
    auto promise = std::make_shared<litebus::Promise<LeaseRevokeResponse>>();
    DoRevoke(promise, request, 1);
    return promise->GetFuture();
}

void EtcdLeaseClientStrategy::DoRevoke(const std::shared_ptr<litebus::Promise<LeaseRevokeResponse>> &promise,
                                       const etcdserverpb::LeaseRevokeRequest &request, int retryTimes)
{
    if (healthyStatus_.IsError()) {
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Revoke api of etcd");
        return;
    }

    (void)leaseClient_
        ->CallAsync("Revoke", request, static_cast<etcdserverpb::LeaseRevokeResponse *>(nullptr),
                    &etcdserverpb::Lease::Stub::AsyncLeaseRevoke, GRPC_TIMEOUT_SECONDS)
        .Then([aid(GetAID()), promise, request, retryTimes,
               timeoutOption(timeoutOption_)](litebus::Try<etcdserverpb::LeaseRevokeResponse> rsp) {
            if (rsp.IsOK()) {
                LeaseRevokeResponse ret;
                const auto &response = rsp.Get();
                Transform(ret.header, response.header());
                YRLOG_DEBUG("Success to Revoke a lease, id is {}", request.id());
                promise->SetValue(ret);
                return Status::OK();
            }
            if (retryTimes == KV_OPERATE_RETRY_TIMES) {
                YRLOG_ERROR("{} to Revoke a lease, id is {}, after {} times", rsp.GetErrorCode(), request.id(),
                            KV_OPERATE_RETRY_TIMES);
                LeaseRevokeResponse ret;
                ret.status = Status(static_cast<StatusCode>(rsp.GetErrorCode()), "grant failed");
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            YRLOG_WARN("{} to Revoke a lease, id is {}, begin to retry", rsp.GetErrorCode(), request.id());
            auto nextSleepTime = GenerateRandomNumber(timeoutOption.operationRetryIntervalLowerBound,
                                                      timeoutOption.operationRetryIntervalUpperBound);
            (void)litebus::AsyncAfter(nextSleepTime, aid, &EtcdLeaseClientStrategy::DoRevoke, promise, request,
                                      retryTimes + 1);
            return Status::OK();
        });
}

litebus::Future<LeaseKeepAliveResponse> EtcdLeaseClientStrategy::KeepAliveOnce(int64_t leaseId)
{
    etcdserverpb::LeaseKeepAliveRequest request;
    request.set_id(leaseId);
    litebus::Promise<LeaseKeepAliveResponse> promise;

    if (leaseStream_ == nullptr) {
        leaseContext_ = std::make_unique<grpc::ClientContext>();
        auto channel = leaseClient_->GetChannel();
        leaseStream_ = etcdserverpb::Lease::NewStub(channel)->LeaseKeepAlive(&(*leaseContext_));

        leaseStreamReadLoopThread_ = std::make_unique<std::thread>([this, leaseId]() { LeaseStreamReadLoop(leaseId); });

        std::string threadName = "Lease";
        int ret = pthread_setname_np(leaseStreamReadLoopThread_->native_handle(), threadName.c_str());
        if (ret != 0) {
            YRLOG_WARN("set pthread name fail. ret:{}", ret);
        }
    }

    auto &&queue = keepAliveQueue_[leaseId];
    (void)queue.emplace_back(promise);

    if (!leaseStream_->Write(request)) {
        YRLOG_ERROR("Failed to write KeepAliveOnce request, lease id is {}", leaseId);

        LeaseKeepAliveResponse output;
        output.status = Status(StatusCode::FAILED, "Failed to write KeepAliveOnce");
        for (const auto &pair : keepAliveQueue_) {
            for (const auto &item : pair.second) {
                item.SetValue(output);
            }
        }
        keepAliveQueue_.clear();
    }

    return promise.GetFuture();
}

void EtcdLeaseClientStrategy::LeaseStreamReadLoop(int64_t leaseId)
{
    YRLOG_INFO("Start a thread to read lease's stream");
    etcdserverpb::LeaseKeepAliveResponse response;
    while (running_) {
        if (leaseStream_->Read(&response)) {
            litebus::Async(GetAID(), &EtcdLeaseClientStrategy::OnKeepAliveLease, response);
            continue;
        }

        leaseClient_->CheckChannelAndWaitForReconnect(running_);
        if (!running_) {
            YRLOG_ERROR("Stop to read KeepAliveOnce lease({}) response.", leaseId);
            break;
        }

        auto connected =
            litebus::Async(GetAID(), &EtcdLeaseClientStrategy::ReconnectKeepAliveLease).Get(RECONNECT_TIMEOUT_MS);
        if (connected.IsNone() || !connected.Get()) {
            YRLOG_ERROR("Failed to read KeepAliveOnce lease({}) response.", leaseId);
            break;
        }

        // need to clear in actor, to avoid multi-thread operations
        litebus::Async(GetAID(), &EtcdLeaseClientStrategy::ClearLeaseKeepAliveQueue, "lease stream reconnect")
            .Get(RECONNECT_TIMEOUT_MS);  // blocked
        YRLOG_INFO("Success to reconnect, current lease id is {}.", leaseId);
    }
    litebus::Async(GetAID(), &EtcdLeaseClientStrategy::ClearLeaseKeepAliveQueue, "end a thread to read lease's stream");
}

bool EtcdLeaseClientStrategy::ReconnectKeepAliveLease()
{
    if (!running_) {
        return false;
    }
    if (leaseContext_ != nullptr) {
        leaseContext_->TryCancel();
    }
    leaseContext_ = std::make_unique<grpc::ClientContext>();
    auto channel = leaseClient_->GetChannel();
    leaseStream_ = etcdserverpb::Lease::NewStub(channel)->LeaseKeepAlive(&(*leaseContext_));
    return true;
}

bool EtcdLeaseClientStrategy::ClearLeaseKeepAliveQueue(const std::string &errMsg)
{
    YRLOG_WARN("start to clear keep alive queue, err is {}", errMsg);
    LeaseKeepAliveResponse output;
    output.status = Status(StatusCode::FAILED, errMsg);
    for (const auto &pair : keepAliveQueue_) {
        for (const auto &item : pair.second) {
            item.SetValue(output);
        }
    }
    keepAliveQueue_.clear();
    return true;
}

void EtcdLeaseClientStrategy::OnKeepAliveLease(const etcdserverpb::LeaseKeepAliveResponse &response)
{
    auto iterator = keepAliveQueue_.find(response.id());
    if (iterator == keepAliveQueue_.end()) {
        YRLOG_DEBUG("not found promise to response, lease id is {}", response.id());
        return;
    }

    LeaseKeepAliveResponse output;  // transform
    Transform(output.header, response.header());
    output.leaseId = response.id();
    output.ttl = response.ttl();

    YRLOG_DEBUG("success to KeepAliveOnce, lease id is {}, ttl {}", response.id(), response.ttl());
    for (const auto &promise : iterator->second) {
        promise.SetValue(output);
    }

    (void)keepAliveQueue_.erase(iterator);
}

litebus::Future<bool> EtcdLeaseClientStrategy::IsConnected()
{
    ASSERT_IF_NULL(leaseClient_);
    return leaseClient_->IsConnected();
}

void EtcdLeaseClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_WARN("etcd lease client doesn't support address update yet");
}
}  // namespace functionsystem::meta_store