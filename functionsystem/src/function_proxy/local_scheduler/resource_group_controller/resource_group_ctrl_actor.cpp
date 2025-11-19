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
#include "resource_group_ctrl_actor.h"

#include "common/constants/actor_name.h"
#include "common/logs/logging.h"

namespace functionsystem::local_scheduler {
const int64_t CREATE_RETRY_BACKOFF = 10000;
const int MAX_BUNDLES_SIZE = 5000;
void ResourceGroupCtrlActor::UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo)
{
    auto cache = rgMgrAid_;
    rgMgrAid_ = std::make_shared<litebus::AID>(RESOURCE_GROUP_MANAGER, leaderInfo.address);
    rgMgrAid_->SetProtocol(litebus::BUS_TCP);
    if (cache == nullptr) {
        cache = std::make_shared<litebus::AID>();
    }
    YRLOG_INFO("begin update master info, master aid: {}, new master aid: {}", cache->HashString(),
               rgMgrAid_->HashString());
}

void ResourceGroupCtrlActor::Init()
{
    ActorBase::Init();
    (void)explorer::Explorer::GetInstance().AddLeaderChangedCallback(
        "ResourceGroupCtrlActor", [aid(GetAID())](const explorer::LeaderInfo &leaderInfo) {
            litebus::Async(aid, &ResourceGroupCtrlActor::UpdateMasterInfo, leaderInfo);
        });
    auto backOff = [](int64_t) { return CREATE_RETRY_BACKOFF; };
    createHelper_.SetBackOffStrategy(backOff, -1);
    killHelper_.SetBackOffStrategy(backOff, -1);
    Receive("OnForwardCreateResourceGroup", &ResourceGroupCtrlActor::OnForwardCreateResourceGroup);
    Receive("OnForwardDeleteResourceGroup", &ResourceGroupCtrlActor::OnForwardDeleteResourceGroup);
}

litebus::Future<std::shared_ptr<CreateResourceGroupResponse>> ResourceGroupCtrlActor::Create(
    const std::string &from, const std::shared_ptr<CreateResourceGroupRequest> &req)
{
    if (req->rgroupspec().bundles_size() > MAX_BUNDLES_SIZE) {
        YRLOG_WARN("{}|{} resource group ({}) bundle request size {} over max size {}", req->traceid(),
                   req->requestid(), req->rgroupspec().name(), req->rgroupspec().bundles_size(), MAX_BUNDLES_SIZE);
        auto response = std::make_shared<CreateResourceGroupResponse>();
        response->set_code(common::ERR_PARAM_INVALID);
        response->set_message("bundle request size over max size");
        return response;
    }
    auto opt = createHelper_.Exist(req->requestid());
    if (opt.IsSome()) {
        YRLOG_WARN("{}|{} of create resource group({}) is already exists", req->traceid(), req->requestid(),
                   req->rgroupspec().name());
        return opt.Get();
    }
    YRLOG_INFO("{}|{}| received create resource group({}), bundle size({}) from ({})", req->traceid(),
               req->requestid(), req->rgroupspec().name(), req->rgroupspec().bundles_size(), from);
    auto future =
        createHelper_.Begin(req->requestid(), rgMgrAid_, "ForwardCreateResourceGroup", req->SerializeAsString());
    future.OnComplete([req](const litebus::Future<std::shared_ptr<CreateResourceGroupResponse>> &future) {
        if (future.IsError()) {
            YRLOG_ERROR("{}|{}| failed to create resource group({})", req->traceid(), req->requestid(),
                        req->rgroupspec().name(), req->rgroupspec().bundles_size());
            return;
        }
        auto rsp = future.Get();
        YRLOG_INFO("{}|{}| received create resource group ({}) response, code:({}) reason:({})", req->traceid(),
                   req->requestid(), req->rgroupspec().name(), fmt::underlying(rsp->code()), rsp->message());
    });
    return future;
}

void ResourceGroupCtrlActor::OnForwardCreateResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto rsp = std::make_shared<CreateResourceGroupResponse>();
    if (!rsp->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse message, from: {}, msg: {}", std::string(from), msg);
        return;
    }
    createHelper_.End(rsp->requestid(), std::move(rsp));
}

litebus::Future<KillResponse> ResourceGroupCtrlActor::Kill(const std::string &from, const std::string &srcTenantID,
                                                            const std::shared_ptr<KillRequest> &killReq)
{
    auto rgName = killReq->instanceid();
    auto then = [rgName](const inner_service::ForwardKillResponse &forwardKillResponse) {
        auto rsp = KillResponse();
        rsp.set_code(forwardKillResponse.code());
        rsp.set_message(forwardKillResponse.message());
        YRLOG_INFO("received kill resource group ({}) response, code:({}) reason:({})", rgName,
                   fmt::underlying(rsp.code()), rsp.message());
        return rsp;
    };
    auto opt = killHelper_.Exist(rgName);
    if (opt.IsSome()) {
        YRLOG_WARN("request of kill resource group({}) is already exists", rgName);
        return opt.Get().Then(then);
    }
    auto forwardKill = inner_service::ForwardKillRequest();
    forwardKill.set_requestid(rgName);
    forwardKill.set_srcinstanceid(from);
    forwardKill.set_srctenantid(srcTenantID);
    *forwardKill.mutable_req() = *killReq;
    YRLOG_INFO("ready to forward kill resource group ({}), dst:({})", rgName, rgMgrAid_->HashString());
    return killHelper_.Begin(rgName, rgMgrAid_, "ForwardDeleteResourceGroup", forwardKill.SerializeAsString())
        .Then(then);
}

void ResourceGroupCtrlActor::OnForwardDeleteResourceGroup(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto rsp = inner_service::ForwardKillResponse();
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse message, from: {}, msg: {}", std::string(from), msg);
        return;
    }
    killHelper_.End(rsp.requestid(), std::move(rsp));
}
}  // namespace functionsystem::local_scheduler