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

#ifndef COMMON_SCHEDULE_DECISION_UNIT_SCHEDULER_H
#define COMMON_SCHEDULE_DECISION_UNIT_SCHEDULER_H

#include "priority_scheduler.h"
#include "common/schedule_plugin/common/round_allocation_context.h"

namespace functionsystem::schedule_decision {

struct RoundDiagnostics {
    uint64_t calls{ 0 };
    uint64_t totalNanos{ 0 };
    uint64_t maxNanos{ 0 };
    uint64_t reconcileNanos{ 0 };
    uint64_t maxReconcileNanos{ 0 };
};

// Reuses PriorityScheduler's queue, priority, consume, and performer logic.
// Domain scheduling pins an immutable Unit snapshot for each consume round;
// its long-lived RoundAllocationContext keeps mutating reservations/overlay
// and rebases them when the next snapshot is pinned. Local ALLOCATION continues
// to use the legacy mailbox ResourceView through the inherited scheduling flow.
class UnitScheduler : public PriorityScheduler {
public:
    explicit UnitScheduler(const std::shared_ptr<ScheduleRecorder> &recorder = nullptr, uint16_t maxPriority = 0,
                           PriorityPolicyType priorityPolicyType = PriorityPolicyType::FIFO,
                           const std::string &aggregatedStrategy = "no_aggregate");
    ~UnitScheduler() override = default;

    bool UsesScheduleSnapshot() const override;
    Status BeginScheduleRound(const resource_view::ScheduleSnapshotPtr &snapshot) override;
    void RegisterSchedulePerformer(const std::shared_ptr<resource_view::ResourceView> &resourceView,
                                   const std::shared_ptr<schedule_framework::Framework> &framework,
                                   const PreemptInstancesFunc &func,
                                   const AllocateType &type = AllocateType::PRE_ALLOCATION) override;

    size_t ScheduleRoundCount() const;
    uint64_t ConsumedSnapshotSequence() const;
    size_t ReservationCount() const;
    size_t OverlayRebuildCount() const;
    size_t JournalOverflowCount() const;
    uint64_t ConsumedMutationSequence() const;
    RoundDiagnostics GetRoundDiagnostics() const;

private:
    void OnFrameworkPoliciesChanged() override;

    std::shared_ptr<schedule_framework::RoundAllocationContext> roundContext_;
    resource_view::ScheduleSnapshotPtr scheduleViewSnapshot_;
    bool useScheduleSnapshot_{ true };
    std::atomic<size_t> scheduleRoundCount_{ 0 };
    std::atomic<uint64_t> roundTotalNanos_{ 0 };
    std::atomic<uint64_t> roundMaxNanos_{ 0 };
    std::atomic<uint64_t> reconcileTotalNanos_{ 0 };
    std::atomic<uint64_t> reconcileMaxNanos_{ 0 };
};

}  // namespace functionsystem::schedule_decision

#endif  // COMMON_SCHEDULE_DECISION_UNIT_SCHEDULER_H
