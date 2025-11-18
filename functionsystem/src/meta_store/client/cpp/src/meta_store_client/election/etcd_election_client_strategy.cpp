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

#include "etcd_election_client_strategy.h"

#include "async/asyncafter.hpp"
#include "meta_store_client/utils/etcd_util.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/utils/random_number.h"
#include "etcd_observer.h"

namespace functionsystem::meta_store {
EtcdElectionClientStrategy::EtcdElectionClientStrategy(const std::string &name, const std::string &address,
                                                       const MetaStoreTimeoutOption &timeoutOption,
                                                       const GrpcSslConfig &sslConfig,
                                                       const std::string &etcdTablePrefix)
    : ElectionClientStrategy(name, address, timeoutOption, etcdTablePrefix)
{
    electionClient_ = GrpcClient<v3electionpb::Election>::CreateGrpcClient(address, sslConfig);
    ASSERT_IF_NULL(electionClient_);
}

litebus::Future<CampaignResponse> EtcdElectionClientStrategy::Campaign(const std::string &name, int64_t lease,
                                                                       const std::string &value)
{
    // makeup Campaign request
    v3electionpb::CampaignRequest request;
    request.set_name(etcdTablePrefix_ + name);
    request.set_lease(lease);
    request.set_value(value);
    auto promise = std::make_shared<litebus::Promise<CampaignResponse>>();
    DoCampaign(promise, std::move(request), 1);
    return promise->GetFuture();
}

void EtcdElectionClientStrategy::DoCampaign(const std::shared_ptr<litebus::Promise<CampaignResponse>> &promise,
                                            const v3electionpb::CampaignRequest &request, int retryTimes)
{
    if (healthyStatus_.IsError()) {
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Campaign api of etcd");
        return;
    }

    (void)electionClient_
        ->CallAsync("Campaign", request, static_cast<v3electionpb::CampaignResponse *>(nullptr),
                    &v3electionpb::Election::Stub::AsyncCampaign)
        .Then([aid(GetAID()), promise, request, retryTimes, timeoutOption(timeoutOption_),
               prefix(etcdTablePrefix_)](litebus::Try<v3electionpb::CampaignResponse> rsp) {
            // transform
            if (rsp.IsOK()) {
                CampaignResponse ret;
                const auto &response = rsp.Get();
                Transform(ret.header, response.header());
                YRLOG_DEBUG("Success to Campaign {}:{}", request.name(), request.value());
                ret.leader.name = TrimKeyPrefix(response.leader().name(), prefix);
                ret.leader.key = TrimKeyPrefix(response.leader().key(), prefix);
                ret.leader.rev = response.leader().rev();
                ret.leader.lease = response.leader().lease();

                promise->SetValue(std::move(ret));
                return Status::OK();
            }

            if (rsp.GetErrorCode() == StatusCode::GRPC_UNKNOWN || retryTimes == timeoutOption.operationRetryTimes) {
                YRLOG_ERROR("{} to Campaign {}:{}", rsp.GetErrorCode(), request.name(), request.value());
                CampaignResponse ret;
                ret.status = Status(static_cast<StatusCode>(rsp.GetErrorCode()), "etcd Campaign failed");
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            YRLOG_WARN("{} to Campaign {}:{}, begin to retry", rsp.GetErrorCode(), request.name(), request.value());
            auto nextSleepTime = GenerateRandomNumber(timeoutOption.operationRetryIntervalLowerBound,
                                                      timeoutOption.operationRetryIntervalUpperBound);
            (void)litebus::AsyncAfter(nextSleepTime, aid, &EtcdElectionClientStrategy::DoCampaign, promise, request,
                                      retryTimes + 1);
            return Status::OK();
        });
}

litebus::Future<LeaderResponse> EtcdElectionClientStrategy::Leader(const std::string &name)
{
    // makeup Leader request
    v3electionpb::LeaderRequest request;
    request.set_name(etcdTablePrefix_ + name);
    auto promise = std::make_shared<litebus::Promise<LeaderResponse>>();
    DoLeader(promise, std::move(request), 1);
    return promise->GetFuture();
}

void EtcdElectionClientStrategy::DoLeader(const std::shared_ptr<litebus::Promise<LeaderResponse>> &promise,
                                          const v3electionpb::LeaderRequest &request, int retryTimes)
{
    if (healthyStatus_.IsError()) {
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Leader api of etcd");
        return;
    }
    (void)electionClient_
        ->CallAsync("Leader", request, static_cast<v3electionpb::LeaderResponse *>(nullptr),
                    &v3electionpb::Election::Stub::AsyncLeader, timeoutOption_.grpcTimeout * retryTimes)
        .Then([aid(GetAID()), promise, request, retryTimes, timeoutOption(timeoutOption_),
               prefix(etcdTablePrefix_)](litebus::Try<v3electionpb::LeaderResponse> rsp) {
            // transform
            if (rsp.IsOK()) {
                LeaderResponse ret;
                const auto &response = rsp.Get();
                Transform(ret.header, response.header());
                YRLOG_DEBUG("Success to get Leader {}", request.name());
                ret.kv.first = TrimKeyPrefix(response.kv().key(), prefix);
                ret.kv.second = response.kv().value();
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            if (rsp.GetErrorCode() == StatusCode::GRPC_UNKNOWN || retryTimes == timeoutOption.operationRetryTimes) {
                YRLOG_ERROR("{} to get Leader {}", rsp.GetErrorCode(), request.name());
                LeaderResponse ret;
                ret.status = Status(static_cast<StatusCode>(rsp.GetErrorCode()), "etcd Leader failed");
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            YRLOG_WARN("{} to get Leader {}, begin to retry", rsp.GetErrorCode(), request.name());
            auto nextSleepTime = GenerateRandomNumber(timeoutOption.operationRetryIntervalLowerBound,
                                                      timeoutOption.operationRetryIntervalUpperBound);
            (void)litebus::AsyncAfter(nextSleepTime, aid, &EtcdElectionClientStrategy::DoLeader, promise, request,
                                      retryTimes + 1);
            return Status::OK();
        });
}

litebus::Future<ResignResponse> EtcdElectionClientStrategy::Resign(const LeaderKey &leader)
{
    // makeup Resign request
    v3electionpb::ResignRequest request;
    request.mutable_leader()->set_name(etcdTablePrefix_ + leader.name);
    request.mutable_leader()->set_key(etcdTablePrefix_ + leader.key);
    request.mutable_leader()->set_rev(leader.rev);
    request.mutable_leader()->set_lease(leader.lease);
    auto promise = std::make_shared<litebus::Promise<ResignResponse>>();
    DoResign(promise, std::move(request), 1);
    return promise->GetFuture();
}

void EtcdElectionClientStrategy::DoResign(const std::shared_ptr<litebus::Promise<ResignResponse>> &promise,
                                          const v3electionpb::ResignRequest &request, int retryTimes)
{
    if (healthyStatus_.IsError()) {
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Resign api of etcd");
        return;
    }
    (void)electionClient_
        ->CallAsync("Resign", request, static_cast<v3electionpb::ResignResponse *>(nullptr),
                    &v3electionpb::Election::Stub::AsyncResign, timeoutOption_.grpcTimeout * retryTimes)
        .Then([aid(GetAID()), promise, request, retryTimes,
               timeoutOption(timeoutOption_)](litebus::Try<v3electionpb::ResignResponse> rsp) {
            // transform
            if (rsp.IsOK()) {
                ResignResponse ret;
                const auto &response = rsp.Get();
                Transform(ret.header, response.header());
                YRLOG_DEBUG("Success to Resign {}", request.leader().name());
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            if (rsp.GetErrorCode() == StatusCode::GRPC_UNKNOWN || retryTimes == timeoutOption.operationRetryTimes) {
                YRLOG_ERROR("{} to Resign {}", rsp.GetErrorCode(), request.leader().name());
                ResignResponse ret;
                ret.status = Status(static_cast<StatusCode>(rsp.GetErrorCode()), "etcd Resign failed");
                promise->SetValue(std::move(ret));
                return Status::OK();
            }
            YRLOG_WARN("{} to Resign {}, begin to retry", rsp.GetErrorCode(), request.leader().name());
            auto nextSleepTime = GenerateRandomNumber(timeoutOption.operationRetryIntervalLowerBound,
                                                      timeoutOption.operationRetryIntervalUpperBound);
            (void)litebus::AsyncAfter(nextSleepTime, aid, &EtcdElectionClientStrategy::DoResign, promise, request,
                                      retryTimes + 1);
            return Status::OK();
        });
}

litebus::Future<std::shared_ptr<Observer>> EtcdElectionClientStrategy::Observe(
    const std::string &name, const std::function<void(LeaderResponse)> &callback)
{
    auto observer = std::make_shared<EtcdObserver>(name, callback, electionClient_->GetChannel(), etcdTablePrefix_);
    if (auto status = observer->Start(); status.IsError()) {
        YRLOG_ERROR("failed to observe key: {}", name);
        litebus::Future<std::shared_ptr<Observer>> ret;
        ret.SetFailed(static_cast<int32_t>(status.StatusCode()));
        return ret;
    }
    return observer;
}

litebus::Future<bool> EtcdElectionClientStrategy::IsConnected()
{
    ASSERT_IF_NULL(electionClient_);
    return electionClient_->IsConnected();
}

void EtcdElectionClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_WARN("etcd election client doesn't support address update yet");
}

}  // namespace functionsystem::meta_store