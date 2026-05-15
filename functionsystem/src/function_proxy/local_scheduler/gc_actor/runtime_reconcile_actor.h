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
     */
    void TriggerOnce(const std::string &funcAgentID);

private:
    void OnTrigger(const std::string &funcAgentID);
    void RunReconcileCycle();

    void ReconcileAgent(const std::string &funcAgentID,
                        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request);

    void OnReconcileComplete(const messages::ReconcileRuntimesResponse &resp,
                             const std::string &funcAgentID);

    void CleanGhostInstance(const std::string &instanceID);

    Status OnForceDeleteComplete(const std::string &instanceID, const Status &status);

    std::string nodeID_;

    std::shared_ptr<InstanceControlView> instanceControlView_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<resource_view::ResourceView> resourceView_;
    std::vector<std::string> pendingAgents_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_GC_ACTOR_RUNTIME_RECONCILE_ACTOR_H
