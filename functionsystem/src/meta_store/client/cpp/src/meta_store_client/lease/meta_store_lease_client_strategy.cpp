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

#include "meta_store_lease_client_strategy.h"

#include "meta_store_client/utils/etcd_util.h"
#include "common/proto/pb/message_pb.h"
#include "common/utils/random_number.h"

namespace functionsystem::meta_store {
MetaStoreLeaseClientStrategy::MetaStoreLeaseClientStrategy(const std::string &name, const std::string &address,
                                                           const MetaStoreTimeoutOption &timeoutOption)
    : LeaseClientStrategy(name, address, timeoutOption)
{
    leaseServiceAid_ = std::make_shared<litebus::AID>("LeaseServiceActor", address_);
    auto backOff = [lower(timeoutOption_.operationRetryIntervalLowerBound),
                    upper(timeoutOption_.operationRetryIntervalUpperBound),
                    base(timeoutOption_.grpcTimeout * 1000)](int64_t attempt) {
        return GenerateRandomNumber(base + lower * attempt, base + upper * attempt);
    };

    grantHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    revokeHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
    keepAliveOnceHelper_.SetBackOffStrategy(backOff, timeoutOption_.operationRetryTimes);
}

void MetaStoreLeaseClientStrategy::Init()
{
    YRLOG_INFO("Init lease Client actor({}).", address_);
    Receive("GrantCallback", &MetaStoreLeaseClientStrategy::GrantCallback);
    Receive("RevokeCallback", &MetaStoreLeaseClientStrategy::RevokeCallback);
    Receive("KeepAliveCallback", &MetaStoreLeaseClientStrategy::KeepAliveOnceCallback);
}

litebus::Future<LeaseGrantResponse> MetaStoreLeaseClientStrategy::Grant(int ttl)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto promise = std::make_shared<litebus::Promise<LeaseGrantResponse>>();
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Grant api of etcd");
        return promise->GetFuture();
    }

    etcdserverpb::LeaseGrantRequest request;
    request.set_ttl(ttl);
    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    YRLOG_DEBUG("{}|begin to grant", req.requestid());
    return grantHelper_.Begin(req.requestid(), leaseServiceAid_, "ReceiveGrant", req.SerializeAsString());
}

void MetaStoreLeaseClientStrategy::GrantCallback(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Grant MetaStoreResponse");

    etcdserverpb::LeaseGrantResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Grant LeaseGrantResponse: " + res.responseid());

    LeaseGrantResponse ret;
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    YRLOG_DEBUG("{}|success to grant a lease, id is {}", res.responseid(), response.id());
    ret.leaseId = response.id();
    ret.ttl = response.ttl();
    grantHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<LeaseRevokeResponse> MetaStoreLeaseClientStrategy::Revoke(int64_t leaseId)
{
    // StatusCode::META_STORE_BACK_UP_ERR, meta-store failed to back up, don't fallbreak
    if (healthyStatus_.IsError() && healthyStatus_ != StatusCode::META_STORE_BACK_UP_ERR) {
        auto promise = std::make_shared<litebus::Promise<LeaseRevokeResponse>>();
        MetaStoreFailure(promise, healthyStatus_, "[fallbreak] failed to call Revoke api of etcd");
        return promise->GetFuture();
    }

    etcdserverpb::LeaseRevokeRequest request;
    request.set_id(leaseId);

    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());
    YRLOG_DEBUG("{}|begin to revoke lease({})", req.requestid(), leaseId);
    return revokeHelper_.Begin(req.requestid(), leaseServiceAid_, "ReceiveRevoke", req.SerializeAsString());
}

void MetaStoreLeaseClientStrategy::RevokeCallback(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse Revoke MetaStoreResponse");

    etcdserverpb::LeaseRevokeResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse Revoke LeaseRevokeResponse: " + res.responseid());

    LeaseRevokeResponse ret;
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    YRLOG_DEBUG("{}|success to revoke a lease", res.responseid());
    revokeHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<LeaseKeepAliveResponse> MetaStoreLeaseClientStrategy::KeepAliveOnce(int64_t leaseId)
{
    etcdserverpb::LeaseKeepAliveRequest request;
    request.set_id(leaseId);

    messages::MetaStoreRequest req;
    req.set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    req.set_requestmsg(request.SerializeAsString());

    YRLOG_DEBUG("{}|begin to keep alive once, lease: {}", req.requestid(), leaseId);
    return keepAliveOnceHelper_.Begin(req.requestid(), leaseServiceAid_, "ReceiveKeepAliveOnce",
                                      req.SerializeAsString());
}

void MetaStoreLeaseClientStrategy::KeepAliveOnceCallback(const litebus::AID &from, std::string &&name,
                                                         std::string &&msg)
{
    messages::MetaStoreResponse res;
    RETURN_IF_TRUE(!res.ParseFromString(msg), "failed to parse KeepAlive MetaStoreResponse");

    etcdserverpb::LeaseKeepAliveResponse response;
    RETURN_IF_TRUE(!response.ParseFromString(res.responsemsg()),
                   "failed to parse KeepAlive LeaseKeepAliveResponse: " + res.responseid());

    LeaseKeepAliveResponse ret;  // transform
    ret.status = Status(static_cast<StatusCode>(res.status()), res.errormsg());
    Transform(ret.header, response.header());
    ret.leaseId = response.id();
    ret.ttl = response.ttl();

    YRLOG_DEBUG("{}|success to keep alive once, lease id is {}, ttl {}", res.responseid(), response.id(),
                response.ttl());
    keepAliveOnceHelper_.End(res.responseid(), std::move(ret));
}

litebus::Future<bool> MetaStoreLeaseClientStrategy::IsConnected()
{
    return true;
}

void MetaStoreLeaseClientStrategy::OnAddressUpdated(const std::string &address)
{
    YRLOG_DEBUG("lease client update address from {} to {}", address_, address);
    address_ = address;
    leaseServiceAid_->SetUrl(address);
}
}  // namespace functionsystem::meta_store
