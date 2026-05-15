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

#include "runtime_reconcile_actor.h"

#include <unordered_map>
#include <unordered_set>

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "common/types/instance_state.h"

namespace functionsystem::local_scheduler {

RuntimeReconcileActor::RuntimeReconcileActor(const std::string &name,
                                             const std::string &nodeID)
    : BasisActor(name),
      nodeID_(nodeID)
{
}

void RuntimeReconcileActor::Init()
{
    BasisActor::Init();
    YRLOG_INFO("RuntimeReconcileActor initialized on node: {} (co-process mode, trigger-once)", nodeID_);
}

void RuntimeReconcileActor::Finalize()
{
    YRLOG_INFO("RuntimeReconcileActor finalizing on node: {}", nodeID_);
    BasisActor::Finalize();
}

void RuntimeReconcileActor::TriggerOnce(const std::string &funcAgentID)
{
    (void)litebus::Async(GetAID(), &RuntimeReconcileActor::OnTrigger, funcAgentID);
}

void RuntimeReconcileActor::OnTrigger(const std::string &funcAgentID)
{
    pendingAgents_.push_back(funcAgentID);
    RunReconcileCycle();
}

void RuntimeReconcileActor::RunReconcileCycle()
{
    if (instanceControlView_ == nullptr || functionAgentMgr_ == nullptr) {
        YRLOG_WARN("RuntimeReconcileActor: dependencies not bound, skipping cycle");
        return;
    }

    auto instances = instanceControlView_->GetInstances();

    // Group instances by funcAgentID, collecting {runtimeID, containerID} entries
    std::unordered_map<std::string, std::shared_ptr<messages::ReconcileRuntimesRequest>> agentRequests;

    for (const auto &[instanceID, sm] : instances) {
        if (sm == nullptr) {
            continue;
        }
        auto info = sm->GetInstanceInfo();
        const auto &agentID = info.functionagentid();
        if (agentID.empty() || info.runtimeid().empty()) {
            continue;
        }

        auto it = agentRequests.find(agentID);
        if (it == agentRequests.end()) {
            auto request = std::make_shared<messages::ReconcileRuntimesRequest>();
            request->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
            it = agentRequests.emplace(agentID, std::move(request)).first;
        }

        auto *entry = it->second->add_entries();
        entry->set_runtimeid(info.runtimeid());
        entry->set_containerid(info.containerid());
        entry->set_instanceid(instanceID);
    }

    YRLOG_INFO("RuntimeReconcileActor: starting reconcile cycle, {} agents to check, {} pending",
               agentRequests.size(), pendingAgents_.size());

    // Agents with no instances: transition directly to NORMAL
    for (const auto &agentID : pendingAgents_) {
        if (agentRequests.find(agentID) == agentRequests.end()) {
            if (resourceView_) {
                YRLOG_INFO("RuntimeReconcileActor: agent {} has no instances, transitioning to NORMAL", agentID);
                resourceView_->UpdateUnitStatus(agentID, resource_view::UnitStatus::NORMAL);
            }
        }
    }
    pendingAgents_.clear();

    for (const auto &[agentID, request] : agentRequests) {
        ReconcileAgent(agentID, request);
    }
}

void RuntimeReconcileActor::ReconcileAgent(
    const std::string &funcAgentID,
    const std::shared_ptr<messages::ReconcileRuntimesRequest> &request)
{
    YRLOG_INFO("{}|RuntimeReconcileActor: reconciling agent {}, entries: {}",
               request->requestid(), funcAgentID, request->entries_size());

    functionAgentMgr_->ReconcileRuntimes(funcAgentID, request)
        .OnComplete(litebus::Defer(GetAID(), [this, funcAgentID](
            const litebus::Future<messages::ReconcileRuntimesResponse> &future) {
            if (future.IsError()) {
                YRLOG_WARN("RuntimeReconcileActor: ReconcileRuntimes RPC failed for agent {}, fail-open to NORMAL",
                           funcAgentID);
                if (resourceView_) {
                    resourceView_->UpdateUnitStatus(funcAgentID, resource_view::UnitStatus::NORMAL);
                }
                return;
            }
            OnReconcileComplete(future.Get(), funcAgentID);
        }));
}

void RuntimeReconcileActor::OnReconcileComplete(const messages::ReconcileRuntimesResponse &resp,
                                                const std::string &funcAgentID)
{
    if (resp.code() != 0) {
        YRLOG_WARN("{}|RuntimeReconcileActor: ReconcileRuntimes returned error for agent {}: {}, "
                   "fail-open to NORMAL",
                   resp.requestid(), funcAgentID, resp.message());
        if (resourceView_) {
            resourceView_->UpdateUnitStatus(funcAgentID, resource_view::UnitStatus::NORMAL);
        }
        return;
    }

    YRLOG_INFO("{}|RuntimeReconcileActor: agent {} reconcile result: orphansCleaned={}, missingIDs={}",
               resp.requestid(), funcAgentID, resp.orphanscleaned(), resp.missingids_size());

    if (resp.orphanscleaned() > 0) {
        YRLOG_INFO("{}|RuntimeReconcileActor: {} orphan containers cleaned on agent {}",
                   resp.requestid(), resp.orphanscleaned(), funcAgentID);
    }

    // Handle ghost instances: containers expected by proxy but missing in executor
    auto instances = instanceControlView_->GetInstances();
    // Build containerID → instanceID mapping for this agent
    std::unordered_map<std::string, std::string> containerToInstance;
    for (const auto &[instanceID, sm] : instances) {
        if (sm == nullptr) {
            continue;
        }
        auto info = sm->GetInstanceInfo();
        if (info.functionagentid() == funcAgentID && !info.containerid().empty()) {
            containerToInstance[info.containerid()] = instanceID;
        }
    }

    for (const auto &missingContainerID : resp.missingids()) {
        auto it = containerToInstance.find(missingContainerID);
        if (it != containerToInstance.end()) {
            YRLOG_WARN("{}|RuntimeReconcileActor: ghost instance {} (container {}) on agent {} — "
                       "container missing from executor",
                       resp.requestid(), it->second, missingContainerID, funcAgentID);
            CleanGhostInstance(it->second);
        }
    }

    // Transition agent from RECOVERING to NORMAL after reconciliation
    if (resourceView_) {
        YRLOG_INFO("{}|RuntimeReconcileActor: agent {} reconciliation complete, confirmed={}, ghosts={}, "
                   "orphans={}, transitioning to NORMAL",
                   resp.requestid(), funcAgentID, resp.confirmedentries_size(),
                   resp.missingids_size(), resp.orphanscleaned());
        resourceView_->UpdateUnitStatus(funcAgentID, resource_view::UnitStatus::NORMAL);
    }
}

void RuntimeReconcileActor::CleanGhostInstance(const std::string &instanceID)
{
    if (instanceCtrl_ == nullptr) {
        YRLOG_WARN("RuntimeReconcileActor: instanceCtrl not bound, cannot clean ghost instance {}", instanceID);
        return;
    }

    (void)instanceCtrl_->ForceDeleteInstance(instanceID)
        .Then(litebus::Defer(GetAID(), &RuntimeReconcileActor::OnForceDeleteComplete, instanceID,
                             std::placeholders::_1));
}

Status RuntimeReconcileActor::OnForceDeleteComplete(const std::string &instanceID, const Status &status)
{
    if (status.IsError()) {
        YRLOG_WARN("RuntimeReconcileActor: ForceDeleteInstance failed for ghost instance {}: {}",
                   instanceID, status.GetMessage());
    } else {
        YRLOG_INFO("RuntimeReconcileActor: ghost instance {} cleaned successfully", instanceID);
    }
    return status;
}

}  // namespace functionsystem::local_scheduler
