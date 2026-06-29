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

#include "lease_service_passthrough_actor.h"

#include "meta_store_client/utils/etcd_util.h"
#include "meta_store/server/src/meta_store_common.h"

namespace functionsystem::meta_store {
void LeaseServicePassthroughActor::ReceiveGrant(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Passthrough ReceiveGrant request");

    if (healthyStatus_.IsError()) {
        LeaseGrantResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call Grant: " + healthyStatus_.GetMessage());
        return OnGrant(response, req.requestid(), from);
    }

    ::etcdserverpb::LeaseGrantRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   req.requestid() + "failed to parse Passthrough ReceiveGrant request");

    YRLOG_DEBUG("{}|receive Passthrough ReceiveGrant request", req.requestid());
    etcdClient_->Grant(static_cast<int>(request.ttl()))
        .OnComplete(litebus::Defer(GetAID(), &LeaseServicePassthroughActor::OnGrant, std::placeholders::_1,
                                   req.requestid(), from));
}

void LeaseServicePassthroughActor::OnGrant(const litebus::Future<LeaseGrantResponse> &response, const std::string &id,
                                           const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to grant, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "GrantCallback", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to grant, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "GrantCallback", res.SerializeAsString());
        return;
    }

    // trans to etcdserverpb::LeaseGrantResponse
    ::etcdserverpb::LeaseGrantResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    ret.set_id(response.Get().leaseId);
    ret.set_ttl(response.Get().ttl);
    res.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|send LeaseGrantResponse to {}", id, std::string(aid));
    Send(aid, "GrantCallback", res.SerializeAsString());
}

void LeaseServicePassthroughActor::ReceiveRevoke(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Passthrough ReceiveRevoke request");

    if (healthyStatus_.IsError()) {
        LeaseRevokeResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call Revoke: " + healthyStatus_.GetMessage());
        return OnRevoke(response, req.requestid(), from);
    }

    ::etcdserverpb::LeaseRevokeRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   req.requestid() + "failed to parse Passthrough ReceiveRevoke request");

    YRLOG_DEBUG("{}|receive Passthrough ReceiveRevoke request", req.requestid());
    etcdClient_->Revoke(request.id())
        .OnComplete(litebus::Defer(GetAID(), &LeaseServicePassthroughActor::OnRevoke, std::placeholders::_1,
                                   req.requestid(), from));
}

void LeaseServicePassthroughActor::OnRevoke(const litebus::Future<LeaseRevokeResponse> &response, const std::string &id,
                                            const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to revoke, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "RevokeCallback", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to revoke, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "RevokeCallback", res.SerializeAsString());
        return;
    }

    // trans to etcdserverpb::LeaseRevokeResponse
    ::etcdserverpb::LeaseRevokeResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    res.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|send LeaseRevokeResponse to {}", id, std::string(aid));
    Send(aid, "RevokeCallback", res.SerializeAsString());
}

void LeaseServicePassthroughActor::ReceiveKeepAlive(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::MetaStoreRequest req;
    RETURN_IF_TRUE(!req.ParseFromString(msg), "failed to parse Passthrough KeepAlive request");

    if (healthyStatus_.IsError()) {
        LeaseKeepAliveResponse response;
        response.status =
            Status(StatusCode::FAILED, "[fallbreak] failed to call KeepAlive: " + healthyStatus_.GetMessage());
        return OnKeepAlive(response, req.requestid(), from);
    }

    ::etcdserverpb::LeaseKeepAliveRequest request;
    RETURN_IF_TRUE(!request.ParseFromString(req.requestmsg()),
                   req.requestid() + "failed to parse Passthrough KeepAlive request");

    YRLOG_DEBUG("{}|receive Passthrough KeepAlive request", req.requestid());
    etcdClient_->KeepAliveOnce(request.id())
        .OnComplete(litebus::Defer(GetAID(), &LeaseServicePassthroughActor::OnKeepAlive, std::placeholders::_1,
                                   req.requestid(), from));
}

void LeaseServicePassthroughActor::OnKeepAlive(const litebus::Future<LeaseKeepAliveResponse> &response,
                                               const std::string &id, const litebus::AID &aid)
{
    messages::MetaStoreResponse res;
    res.set_responseid(id);

    if (response.IsError()) {
        YRLOG_ERROR("{}|failed to keep alive, err: {}", id, response.GetErrorCode());
        res.set_status(response.GetErrorCode());
        Send(aid, "KeepAliveCallback", res.SerializeAsString());
        return;
    }

    if (response.Get().status.IsError()) {
        YRLOG_ERROR("{}|failed to keep alive, err: {}", id, response.Get().status.ToString());
        res.set_status(response.Get().status.StatusCode());
        res.set_errormsg(response.Get().status.GetMessage());
        Send(aid, "KeepAliveCallback", res.SerializeAsString());
        return;
    }

    // trans to etcdserverpb::LeaseKeepAliveResponse
    ::etcdserverpb::LeaseKeepAliveResponse ret;
    Transform(ret.mutable_header(), response.Get().header);
    ret.set_id(response.Get().leaseId);
    ret.set_ttl(response.Get().ttl);
    res.set_responsemsg(ret.SerializeAsString());

    YRLOG_DEBUG("{}|send LeaseKeepAliveResponse to {}", id, std::string(aid));
    Send(aid, "KeepAliveCallback", res.SerializeAsString());
}
}  // namespace functionsystem::meta_store
