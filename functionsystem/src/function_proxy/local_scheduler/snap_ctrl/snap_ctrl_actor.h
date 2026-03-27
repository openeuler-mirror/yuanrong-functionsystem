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

#ifndef LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H
#define LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <memory>
#include <string>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/state_machine/instance_control_view.h"
#include "common/state_machine/instance_state_machine.h"
#include "common/status/status.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr_actor.h"
#include "local_scheduler/instance_control/instance_ctrl.h"
#include "local_scheduler/local_scheduler_service/local_sched_srv.h"

namespace functionsystem::local_scheduler {

class InstanceCtrlActor;

class SnapCtrlActor : public BasisActor {
public:
    SnapCtrlActor(const std::string &name, const std::string &nodeID);
    ~SnapCtrlActor() override = default;

    void Init() override;

    /**
     * Handle INSTANCE_SNAPSHOT_SIGNAL
     * Create a snapshot of the running instance
     * @param requestID: Request ID for tracing
     * @param instanceID: ID of the instance to snapshot
     * @param payload: core_service::SnapOptions payload
     * @return KillResponse containing snapshot info payload
     */
    litebus::Future<KillResponse> HandleSnapshot(const std::string &requestID,
                                                  const std::string &instanceID,
                                                  const std::string &payload);

    /**
     * Callback to convert SnapshotResult to KillResponse
     * @param result: The snapshot result
     * @return KillResponse with appropriate code and payload
     */
    KillResponse OnHandleSnapshot(const SnapshotResult &result);

    /**
     * Handle INSTANCE_SNAPSTART_SIGNAL
     * Restore an instance from a snapshot
     * @param requestID: Request ID for tracing
     * @param checkpointID: The checkpoint ID to restore from
     * @param payload: core_service::SnapStartOptions payload
     * @return KillResponse with restore result
     */
    litebus::Future<KillResponse> HandleSnapStart(const std::string &requestID,
                                                   const std::string &checkpointID,
                                                   const std::string &payload);

    /**
     * Handle snapstart instance initialization after state transition to CREATING
     * Complete flow: DeployInstance -> CreateInstanceClient -> StartHeartbeat
     *                -> SnapStarted -> TransInstanceState(RUNNING) -> SetValue
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request for the restored instance
     * @param result: Schedule result from scheduler
     * @param transResult: Transition result from state transition to CREATING
     * @return Option of TransitionResult
     */
    void SnapStart(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const schedule_decision::ScheduleResult &result,
        const TransitionResult &transResult);

    /**
     * Bind the FunctionAgentMgr for sending snapshot requests to agents
     * @param functionAgentMgr: The function agent manager interface
     */
    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr)
    {
        functionAgentMgr_ = functionAgentMgr;
    }

    /**
     * Bind the LocalSchedSrv for recording snapshot metadata
     * @param localSchedSrv: The local scheduler service interface
     */
    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        localSchedSrv_ = localSchedSrv;
    }

    /**
     * Bind the InstanceControlView for accessing instance state machines
     * @param instanceControlView: The instance control view
     */
    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &instanceControlView)
    {
        instanceControlView_ = instanceControlView;
    }

    /**
     * Bind the ControlInterfaceClientManagerProxy for accessing instance clients
     * @param clientManager: The client manager
     */
    void BindClientManager(const std::shared_ptr<ControlInterfaceClientManagerProxy> &clientManager)
    {
        clientManager_ = clientManager;
    }

    /**
     * Bind the InstanceCtrlActor for deleting instances
     * @param instanceCtrl: The instance control actor
     */
    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        instanceCtrl_ = instanceCtrl;
    }

private:
    /**
     * Prepare snapshot by calling runtime PrepareSnap interface
     * @param requestID: Request ID for tracing
     * @param instanceID: ID of the instance
     * @return Status of preparation
     */
    litebus::Future<Status> PrepareSnap(const std::string &requestID, const std::string &instanceID);

    /**
     * Handle DeploySnapStartInstance completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param deployFuture: Deploy response future
     */
    void OnDeploySnapStartInstanceComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<messages::DeployInstanceResponse> &deployFuture);

    /**
     * Handle CreateInstanceClient completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request with updated runtime info
     * @param clientResult: Client future result
     */
    void OnCreateInstanceClientComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientResult);

    /**
     * Handle SnapStarted RPC completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param snapStartedResult: SnapStarted RPC result
     */
    void OnSnapStartedRpcComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<runtime::SnapStartedResponse> &snapStartedResult);

    /**
     * Handle TransInstanceState completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param transResult: Transition result
     */
    void OnTransInstanceStateComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<TransitionResult> &transResult);

    std::string nodeID_;

    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<LocalSchedSrv> localSchedSrv_;
    std::shared_ptr<InstanceControlView> instanceControlView_;
    std::shared_ptr<ControlInterfaceClientManagerProxy> clientManager_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H
