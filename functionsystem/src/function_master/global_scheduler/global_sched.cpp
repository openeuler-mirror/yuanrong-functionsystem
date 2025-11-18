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

#include "global_sched.h"

#include <async/defer.hpp>
#include <functional>

#include "common/logs/logging.h"
#include "common/scheduler_topology/sched_tree.h"
#include "common/utils/meta_store_kv_operation.h"

namespace functionsystem::global_scheduler {
const std::string EXTERNAL_AGENT_ID_DELIMITER = "/";

Status GlobalSched::Start(std::shared_ptr<GlobalSchedActor> globalSchedActor)
{
    ASSERT_IF_NULL(globalSchedActor);
    globalSchedActor_ = std::move(globalSchedActor);
    globalSchedActor_->BindDomainSchedMgr(domainSchedMgr_);
    globalSchedActor_->BindLocalSchedMgr(localSchedMgr_);
    litebus::Spawn(globalSchedActor_);

    (void)domainSchedMgr_->AddDomainSchedCallback([aid(globalSchedActor_->GetAID())](const litebus::AID &from,
                                                                                     const std::string &name,
                                                                                     const std::string &address) {
        litebus::Async(aid, &GlobalSchedActor::AddDomainSchedHandler, from, name, address);
    });

    (void)domainSchedMgr_->DelDomainSchedCallback(
        [aid(globalSchedActor_->GetAID())](const std::string &name, const std::string &ip) {
            litebus::Async(aid, &GlobalSchedActor::DelDomainSchedHandler, name);
        });
    (void)domainSchedMgr_->DelLocalSchedCallback(
        [aid(globalSchedActor_->GetAID())](const std::string &name, const std::string &ip) {
            litebus::Async(aid, &GlobalSchedActor::DelLocalSchedHandler, name, LocalExitType::ABNORMAL);
            litebus::Async(aid, &GlobalSchedActor::UpdateNodeTaintsHandler, ip, FUNCTION_PROXY_TAINT_KEY, false);
        });

    (void)domainSchedMgr_->NotifyWorkerStatusCallback(
        [aid(globalSchedActor_->GetAID())](const std::string &ip, const std::string &key, const bool healthy) {
            litebus::Async(aid, &GlobalSchedActor::UpdateNodeTaintsHandler, ip, key, healthy);
        });

    (void)localSchedMgr_->AddLocalSchedCallback([aid(globalSchedActor_->GetAID())](const litebus::AID &from,
                                                                                   const std::string &name,
                                                                                   const std::string &address) {
        litebus::Async(aid, &GlobalSchedActor::AddLocalSchedHandler, from, name, address);
        litebus::Async(aid, &GlobalSchedActor::UpdateNodeTaintsHandler, GetIPFromAddress(address),
                       FUNCTION_PROXY_TAINT_KEY, true);
    });

    (void)localSchedMgr_->DelLocalSchedCallback(
        [aid(globalSchedActor_->GetAID())](const std::string &name, const std::string &ip) {
            litebus::Async(aid, &GlobalSchedActor::DelLocalSchedHandler, name, LocalExitType::UNREGISTER);
            litebus::Async(aid, &GlobalSchedActor::UpdateNodeTaintsHandler, ip, FUNCTION_PROXY_TAINT_KEY, false);
        });

    return Status::OK();
}

Status GlobalSched::Stop() const
{
    if (domainSchedMgr_) {
        domainSchedMgr_->Stop();
    }
    if (localSchedMgr_) {
        localSchedMgr_->Stop();
    }
    if (globalSchedActor_) {
        litebus::Terminate(globalSchedActor_->GetAID());
    }
    return Status::OK();
}

void GlobalSched::Await() const
{
    if (!globalSchedActor_) {
        return;
    }
    litebus::Await(globalSchedActor_->GetAID());
}

void GlobalSched::InitManager(std::unique_ptr<DomainSchedMgr> domainSchedMgr,
                              std::unique_ptr<LocalSchedMgr> localSchedMgr)
{
    ASSERT_IF_NULL(domainSchedMgr);
    ASSERT_IF_NULL(localSchedMgr);
    domainSchedMgr_ = std::move(domainSchedMgr);
    localSchedMgr_ = std::move(localSchedMgr);
    domainSchedMgr_->Start();
    localSchedMgr_->Start();
}

void GlobalSched::LocalSchedAbnormalCallback(const LocalSchedAbnormalCallbackFunc &func)
{
    ASSERT_IF_NULL(globalSchedActor_);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::BindLocalSchedAbnormalCallback, func);
}

void GlobalSched::BindCheckLocalAbnormalCallback(const CheckLocalAbnormalCallbackFunc &func)
{
    ASSERT_IF_NULL(globalSchedActor_);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::BindCheckLocalAbnormalCallback, func);
}

void GlobalSched::AddLocalSchedAbnormalNotifyCallback(const std::string &name,
                                                      const LocalSchedAbnormalCallbackFunc &func)
{
    ASSERT_IF_NULL(globalSchedActor_);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::AddLocalSchedAbnormalNotifyCallback, name, func);
}

void GlobalSched::BindLocalDeleteCallback(const LocalDeleteCallbackFunc &func)
{
    ASSERT_IF_NULL(globalSchedActor_);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::BindLocalDeleteCallback, func);
}

void GlobalSched::BindLocalAddCallback(const LocalAddCallbackFunc &func)
{
    ASSERT_IF_NULL(globalSchedActor_);
    litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::BindLocalAddCallback, func);
}

litebus::Future<Status> GlobalSched::Schedule(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::DoSchedule, req);
}

litebus::Future<litebus::Option<std::string>> GlobalSched::GetLocalAddress(const std::string &name)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::GetLocalAddress, name);
}

void EncodeExternalAgentID(std::string &externalAgentID, const std::string &localID, const std::string &agentID)
{
    externalAgentID = localID + EXTERNAL_AGENT_ID_DELIMITER + agentID;
}

bool DecodeExternalAgentID(const std::string &externalAgentID, std::string &localID, std::string &agentID)
{
    auto pos = externalAgentID.find(EXTERNAL_AGENT_ID_DELIMITER);
    if (pos == std::string::npos) {
        return false;
    }
    localID = externalAgentID.substr(0, pos);
    agentID = externalAgentID.substr(pos + 1);
    return !localID.empty() && !agentID.empty();
}

void ConvertQueryAgentInfoResponseToExternal(const messages::QueryAgentInfoResponse &resp,
                                             messages::ExternalQueryAgentInfoResponse &externResp)
{
    for (auto &info : resp.agentinfos()) {
        messages::ExternalAgentInfo externInfo;
        externInfo.set_alias(info.alias());
        EncodeExternalAgentID(*externInfo.mutable_id(), info.localid(), info.agentid());
        externResp.mutable_data()->Add(std::move(externInfo));
    }
}

litebus::Future<Status> GlobalSched::EvictAgent(const std::string &localID,
                                                const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::EvictAgent, localID, req);
}

litebus::Future<messages::QueryAgentInfoResponse> GlobalSched::QueryAgentInfo(
    const std::shared_ptr<messages::QueryAgentInfoRequest> &req)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::QueryAgentInfo, req);
}

litebus::Future<messages::QueryInstancesInfoResponse> GlobalSched::GetSchedulingQueue(
    const std::shared_ptr<messages::QueryInstancesInfoRequest> &req)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::GetSchedulingQueue, req);
}

litebus::Future<messages::QueryResourcesInfoResponse> GlobalSched::QueryResourcesInfo(
    const std::shared_ptr<messages::QueryResourcesInfoRequest> &req)
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::HandleQueryResourcesInfo, req);
}

void GlobalSched::OnHealthyStatus(const Status &status)
{
    RETURN_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::OnHealthyStatus, status);
}

litebus::Future<litebus::Option<NodeInfo>> GlobalSched::GetRootDomainInfo()
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::GetRootDomainInfo);
}

litebus::Future<std::unordered_set<std::string>> GlobalSched::QueryNodes()
{
    ASSERT_IF_NULL(globalSchedActor_);
    return litebus::Async(globalSchedActor_->GetAID(), &GlobalSchedActor::QueryNodes);
}
}  // namespace functionsystem::global_scheduler