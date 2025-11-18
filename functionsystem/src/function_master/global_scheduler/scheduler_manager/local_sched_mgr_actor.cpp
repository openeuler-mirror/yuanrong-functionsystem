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

#include "local_sched_mgr_actor.h"

#include "async/asyncafter.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/generate_message.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem::global_scheduler {
const uint64_t DEFAULT_RETRY_INTERVAL = 3000;

LocalSchedMgrActor::LocalSchedMgrActor(const std::string &name) : litebus::ActorBase(name)
{
}

void LocalSchedMgrActor::Init()
{
    YRLOG_DEBUG("init LocalSchedMgrActor");
    auto masterBusiness = std::make_shared<MasterBusiness>(shared_from_this());
    auto slaveBusiness = std::make_shared<SlaveBusiness>(shared_from_this());

    (void)businesses_.emplace(MASTER_BUSINESS, masterBusiness);
    (void)businesses_.emplace(SLAVE_BUSINESS, slaveBusiness);

    curStatus_ = SLAVE_BUSINESS;
    business_ = slaveBusiness;
    Receive("Register", &LocalSchedMgrActor::Register);
    Receive("UnRegister", &LocalSchedMgrActor::UnRegister);
    Receive("EvictAck", &LocalSchedMgrActor::EvictAck);
    Receive("NotifyEvictResult", &LocalSchedMgrActor::NotifyEvictResult);
}

void LocalSchedMgrActor::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->Register(from, std::move(name), std::move(msg));
}

void LocalSchedMgrActor::UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ASSERT_IF_NULL(business_);
    business_->UnRegister(from, std::move(name), std::move(msg));
}

void LocalSchedMgrActor::Registered(const litebus::AID &dst,
                                    const litebus::Option<messages::ScheduleTopology> &topology)
{
    if (topology.IsNone()) {
        YRLOG_INFO("send registered message to local scheduler[{}], ScheduleTopology is none", dst.HashString());
        Send(dst, "Registered",
             GenRegistered(StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE, "topology message is none")
                 .SerializeAsString());
        return;
    }

    Send(dst, "Registered",
         GenRegistered(StatusCode::SUCCESS, "registered success", topology.Get()).SerializeAsString());
}

Status LocalSchedMgrActor::AddLocalSchedCallback(const functionsystem::global_scheduler::CallbackAddFunc &func)
{
    if (func == nullptr) {
        YRLOG_ERROR("add local scheduler callback is empty");
        return Status(StatusCode::FAILED);
    }

    this->addLocalSchedCallback_ = func;
    return Status(StatusCode::SUCCESS);
}

Status LocalSchedMgrActor::DelLocalSchedCallback(const functionsystem::global_scheduler::CallbackDelFunc &func)
{
    this->delLocalSchedCallback_ = func;
    return Status(StatusCode::SUCCESS);
}

void LocalSchedMgrActor::UpdateSchedTopoView(const std::string &address, const messages::ScheduleTopology &topology)
{
    std::string responseMsg;
    if (!topology.SerializeToString(&responseMsg)) {
        YRLOG_ERROR("schedule topology message is invalid, can't update schedule topo view");
        return;
    }

    Send(litebus::AID(LOCAL_SCHED_SRV_ACTOR_NAME, address), "UpdateSchedTopoView", std::move(responseMsg));
}

void LocalSchedMgrActor::UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
{
    litebus::AID masterAID(GLOBAL_SCHED_ACTOR_NAME, leaderInfo.address);
    auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
    if (businesses_.find(newStatus) == businesses_.end()) {
        YRLOG_WARN("new status({}) business don't exist for LocalSchedMgr", newStatus);
        return;
    }
    business_ = businesses_[newStatus];
    curStatus_ = newStatus;
}

litebus::Future<Status> LocalSchedMgrActor::EvictAgentOnLocal(const std::string &address,
                                                              const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    YRLOG_INFO("start to evict agent({}) on {}. timeout({})", req->agentid(), address, req->timeoutsec());
    if (evictCtxs_.find(address) != evictCtxs_.end() &&
        evictCtxs_[address].find(req->agentid()) != evictCtxs_[address].end()) {
        YRLOG_WARN("duplicated evict agent({}) on {}. timeout({})", req->agentid(), address, req->timeoutsec());
        return evictCtxs_[address][req->agentid()]->resultPromise->GetFuture();
    }
    auto ctx = std::make_shared<EvictContext>();
    ctx->agentID = req->agentid();
    ctx->resultPromise = std::make_shared<litebus::Promise<Status>>();
    evictCtxs_[address][req->agentid()] = ctx;
    SendEvict(ctx, address, req);
    return ctx->resultPromise->GetFuture();
}

void LocalSchedMgrActor::SendEvict(const std::shared_ptr<EvictContext> &ctx, const std::string &address,
                                   const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    auto future = evictAckSync_.AddSynchronizer(req->agentid());
    Send(litebus::AID(LOCAL_SCHED_SRV_ACTOR_NAME, address), "EvictAgent", req->SerializeAsString());
    future.OnComplete([aid(GetAID()), ctx, address, req](const litebus::Future<Status> &future) {
        // while EvictAgent ack timeout
        if (future.IsError()) {
            litebus::TimerTools::Cancel(ctx->ackRetryTimer);
            ctx->ackRetryTimer =
                litebus::AsyncAfter(DEFAULT_RETRY_INTERVAL, aid, &LocalSchedMgrActor::SendEvict, ctx, address, req);
            return;
        }
        auto status = future.Get();
        if (status.IsOk()) {
            YRLOG_INFO("evict agent({}) request accepted by {}.", req->agentid(), address);
        }
        return;
    });
}

void LocalSchedMgrActor::EvictAck(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto ack = messages::EvictAgentAck();
    if (msg.empty() || !ack.ParseFromString(msg)) {
        YRLOG_WARN("invalid evict agent ack: {}", msg);
        return;
    }
    auto status = Status(static_cast<StatusCode>(ack.code()), ack.message());
    evictAckSync_.Synchronized(ack.agentid(), status);
    if (status.IsOk()) {
        return;
    }
    auto address = from.Url();
    if (evictCtxs_.find(address) == evictCtxs_.end() ||
        evictCtxs_[address].find(ack.agentid()) == evictCtxs_[address].end()) {
        return;
    }
    auto &ctx = evictCtxs_[address][ack.agentid()];
    ctx->resultPromise->SetValue(status);
    YRLOG_ERROR("failed to evict agent({}), reason:{}", ack.agentid(), status.ToString());
    litebus::TimerTools::Cancel(ctx->ackRetryTimer);
    (void)evictCtxs_[address].erase(ack.agentid());
    if (evictCtxs_[address].empty()) {
        (void)evictCtxs_.erase(address);
    }
}

void LocalSchedMgrActor::NotifyEvictResult(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto result = messages::EvictAgentResult();
    if (msg.empty() || !result.ParseFromString(msg)) {
        YRLOG_WARN("invalid evict agent ack: {}", msg);
        return;
    }
    auto ack = messages::EvictAgentResultAck();
    ack.set_agentid(result.agentid());
    // return ack
    Send(from, "NotifyEvictResultAck", ack.SerializeAsString());
    auto address = from.Url();
    if (evictCtxs_.find(address) == evictCtxs_.end() ||
        evictCtxs_[address].find(result.agentid()) == evictCtxs_[address].end()) {
        YRLOG_WARN("no evict request waiting from {} to evict agent({})", address, result.agentid());
        return;
    }
    auto &ctx = evictCtxs_[address][result.agentid()];
    auto status = Status(static_cast<StatusCode>(result.code()), result.message());
    ctx->resultPromise->SetValue(status);
    YRLOG_DEBUG("received agent({}) evicted result from {}, message:{}", result.agentid(), address, status.ToString());
    litebus::TimerTools::Cancel(ctx->ackRetryTimer);
    (void)evictCtxs_[address].erase(result.agentid());
    if (evictCtxs_[address].empty()) {
        (void)evictCtxs_.erase(address);
    }
}

void LocalSchedMgrActor::OnLocalAbnormal(const std::string &localID, const std::string &address)
{
    if (evictCtxs_.find(address) == evictCtxs_.end()) {
        YRLOG_WARN("no evicting request waiting from {}", address);
        return;
    }
    for (auto [agentID, ctx] : evictCtxs_[address]) {
        YRLOG_INFO("agent({}) evicted because of local({}) is abnormal", agentID, localID);
        litebus::TimerTools::Cancel(ctx->ackRetryTimer);
        ctx->resultPromise->SetValue(Status(
            StatusCode::SUCCESS,
            "warn: Due to the local exception, the evicted agent is considered to have been evicted successfully."));
    }
    (void)evictCtxs_.erase(address);
}

void LocalSchedMgrActor::MasterBusiness::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_DEBUG("receive message({}) from {}", name, from.HashString());

    messages::Register request;

    if (!request.ParseFromString(msg) || request.name().empty() || request.address().empty()) {
        YRLOG_ERROR("invalid register request message");
        actor->Send(
            from, "Registered",
            GenRegistered(StatusCode::GS_REGISTER_REQUEST_INVALID, "invalid request message").SerializeAsString());
        return;
    }
    if (actor->addLocalSchedCallback_ != nullptr) {
        actor->addLocalSchedCallback_(from, request.name(), request.address());
    }
}

void LocalSchedMgrActor::MasterBusiness::UnRegister(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto actor = actor_.lock();
    ASSERT_IF_NULL(actor);
    YRLOG_DEBUG("receive message({}) from {}", name, from.HashString());
    messages::Register request;
    if (!request.ParseFromString(msg) || request.name().empty() || request.address().empty()) {
        YRLOG_ERROR("invalid unregister request message");
        actor->Send(
            from, "UnRegistered",
            GenRegistered(StatusCode::GS_REGISTER_REQUEST_INVALID, "invalid request message").SerializeAsString());
        return;
    }
    if (actor->delLocalSchedCallback_ != nullptr) {
        actor->delLocalSchedCallback_(request.name(), GetIPFromAddress(request.address()));
    }
    actor->Send(from, "UnRegistered", GenRegistered(StatusCode::SUCCESS, "registered success").SerializeAsString());
}

void LocalSchedMgrActor::SlaveBusiness::Register(const litebus::AID &, std::string &&, std::string &&)
{
}

}  // namespace functionsystem::global_scheduler