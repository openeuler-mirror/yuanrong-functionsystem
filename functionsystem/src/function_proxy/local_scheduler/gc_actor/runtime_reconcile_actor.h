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

#ifndef LOCAL_SCHEDULER_GC_ACTOR_RUNTIME_RECONCILE_ACTOR_H
#define LOCAL_SCHEDULER_GC_ACTOR_RUNTIME_RECONCILE_ACTOR_H

#include <actor/actor.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view.h"
#include "common/status/status.h"
#include "function_proxy/common/state_machine/instance_control_view.h"
#include "local_scheduler/instance_control/instance_ctrl.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"

namespace functionsystem::local_scheduler {

/**
 * RuntimeReconcileActor reconciles the proxy's instance view against the actual
 * container inventory managed by the executor (e.g. sandboxd).
 *
 * The proxy sends expected {runtimeID, containerID} entries to the agent/executor,
 * which internally compares against its actual container list and returns:
 *   - orphanIDs: containers cleaned by the executor
 *   - missingIDs: expected containers that no longer exist
 *
 * Used only in co-process mode. Triggered once after agent registration completes
 * via TriggerOnce().
 */
class RuntimeReconcileActor : public BasisActor {
public:
    RuntimeReconcileActor(const std::string &name,
                          const std::string &nodeID);
    ~RuntimeReconcileActor() override = default;

    void Init() override;
    void Finalize() override;

    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &view)
    {
        instanceControlView_ = view;
    }

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &ctrl)
    {
        instanceCtrl_ = ctrl;
    }

    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &mgr)
    {
        functionAgentMgr_ = mgr;
    }

    void BindResourceView(const std::shared_ptr<resource_view::ResourceView> &view)
    {
        resourceView_ = view;
    }

    /**
     * Co-process mode: trigger a single reconciliation cycle for specified agent.
     * Used at agent (re)registration to transition RECOVERING → NORMAL once.
     */
    void TriggerOnce(const std::string &funcAgentID);

private:
    void OnTrigger(const std::string &funcAgentID);

    // Run a reconcile pass for a specific subset of agents.
    //   firstPassAgents: agents that just registered; eligible for RECOVERING→NORMAL
    //                    transition when reconcile reports success/empty.
    // Periodic passes (no firstPassAgents) reconcile every agent that owns at least
    // one instance in proxy's local view, but never mutate UnitStatus.
    void RunReconcileCycle(const std::vector<std::string> &firstPassAgents);

    void ReconcileAgent(const std::string &funcAgentID,
                        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
                        bool isFirstPass);

    void OnReconcileComplete(const messages::ReconcileRuntimesResponse &resp,
                             const std::string &funcAgentID,
                             bool isFirstPass);

    void OnReconcileError(const std::string &funcAgentID, bool isFirstPass,
                          const std::string &reason);

    void CleanGhostInstance(const std::string &instanceID);

    Status OnForceDeleteComplete(const std::string &instanceID, const Status &status);

    void SchedulePeriodicCycle();
    void RunPeriodicCycle();

    void RetryPendingGhosts();

    std::string nodeID_;

    std::shared_ptr<InstanceControlView> instanceControlView_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<resource_view::ResourceView> resourceView_;

    // Agents pending first-pass transition (RECOVERING → NORMAL on success).
    std::vector<std::string> pendingAgents_;

    // In-flight reconcile RPCs per agent: prevents overlap between first-pass and
    // periodic passes, or two periodic passes if a previous one is slow.
    std::unordered_set<std::string> inProgressAgents_;

    // Ghost instances (proxy says it exists, executor says it does not) whose
    // ForceDelete attempt did not succeed yet. Retried on next periodic cycle.
    std::unordered_set<std::string> pendingGhostInstances_;

    static constexpr uint64_t kPeriodicReconcileIntervalMs = 60ULL * 1000ULL;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_GC_ACTOR_RUNTIME_RECONCILE_ACTOR_H
