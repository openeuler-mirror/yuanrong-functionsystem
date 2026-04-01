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

#ifndef LOCAL_SCHEDULER_GC_ACTOR_H
#define LOCAL_SCHEDULER_GC_ACTOR_H

#include <actor/actor.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

#include "common/state_machine/instance_control_view.h"
#include "common/state_machine/instance_state_machine.h"
#include "common/status/status.h"
#include "local_scheduler/instance_control/instance_ctrl.h"

namespace functionsystem::local_scheduler {

/**
 * LocalGcActor periodically scans all local instances and reclaims those in
 * abnormal states:
 *
 * Terminal abnormal states (cleaned after terminalRetentionMs):
 *   EXITED, EVICTED, FATAL
 *
 * Stuck transient / retriable-failure states (cleaned after stuckTimeoutMs):
 *   CREATING, SCHEDULING, FAILED, SCHEDULE_FAILED
 *
 * Periodic scheduling: self-rescheduling via litebus::AsyncAfter at the end of
 * each GC cycle.
 */
class LocalGcActor : public BasisActor {
public:
    /**
     * @param name            Actor name (from actor_name.h)
     * @param nodeID          Node identifier for logging
     * @param gcIntervalMs    GC scan interval in milliseconds (default 60s)
     * @param terminalRetentionMs  Time before cleaning a terminal instance (default 5min)
     * @param stuckTimeoutMs       Time before cleaning a stuck transient instance (default 10min)
     */
    LocalGcActor(const std::string &name,
                 const std::string &nodeID,
                 uint32_t gcIntervalMs = 60000,
                 uint32_t terminalRetentionMs = 300000,
                 uint32_t stuckTimeoutMs = 600000);
    ~LocalGcActor() override = default;

    void Init() override;
    void Finalize() override;

    /**
     * Bind the InstanceControlView for enumerating instances.
     */
    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &view)
    {
        member_instanceControlView = view;
    }

    /**
     * Bind the InstanceCtrl for triggering instance deletion.
     */
    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &ctrl)
    {
        member_instanceCtrl = ctrl;
    }

private:
    /**
     * Main GC cycle: scan instances, clean abnormal ones, reschedule self.
     * Called once from Init() and then self-rescheduled via AsyncAfter.
     */
    void RunGcCycle();

    /**
     * Scan all instances and delete those that have been in an abnormal state
     * longer than the configured retention / timeout thresholds.
     */
    void CleanupAbnormalInstances();

    /**
     * Compute elapsed milliseconds between two time points.
     * Callers should pass the pre-captured now to avoid double clock reads.
     */
    static int64_t ElapsedMs(const std::chrono::steady_clock::time_point &since,
                              const std::chrono::steady_clock::time_point &now);

    /**
     * Callback invoked (via Defer) when ForceDeleteInstance completes.
     * Logs any failure for observability and returns the status unchanged.
     * Must return Status (not void) to satisfy litebus::Future<T>::Then template.
     */
    Status OnForceDeleteComplete(const std::string &instanceID, const Status &status);

    /**
     * Purge tracking entries for instances that no longer exist in the view.
     * Called at the end of each scan cycle to avoid unbounded map growth.
     */
    void PurgeVanishedEntries(
        const std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>> &instances);

    std::string memberNodeId;
    uint32_t memberGcIntervalMs;
    uint32_t memberTerminalRetentionMs;
    uint32_t memberStuckTimeoutMs;

    std::shared_ptr<InstanceControlView> member_instanceControlView;
    std::shared_ptr<InstanceCtrl> member_instanceCtrl;

    // Records the first time an instance was observed in an abnormal state.
    // Entries are removed when the instance is cleaned up or leaves the abnormal state.
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> member_abnormalFirstSeenTimes;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_GC_ACTOR_H
