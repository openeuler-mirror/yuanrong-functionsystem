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

#include "unit_scheduler.h"

#include <chrono>

namespace functionsystem::schedule_decision {

namespace {
void UpdateMaximum(std::atomic<uint64_t> &maximum, uint64_t value)
{
    auto observed = maximum.load(std::memory_order_relaxed);
    while (observed < value) {
        if (maximum.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
            break;
        }
    }
}
}  // namespace

UnitScheduler::UnitScheduler(const std::shared_ptr<ScheduleRecorder> &recorder, uint16_t maxPriority,
                             PriorityPolicyType priorityPolicyType, const std::string &aggregatedStrategy)
    : PriorityScheduler(recorder, maxPriority, priorityPolicyType, aggregatedStrategy),
      roundContext_(std::make_shared<schedule_framework::RoundAllocationContext>())
{
    semanticAggregation_ = true;
    runningQueue_ = CreateQueue();
    pendingQueue_ = CreateQueue();
    preContext_ = roundContext_;
}

bool UnitScheduler::UsesScheduleSnapshot() const
{
    return useScheduleSnapshot_ && framework_ != nullptr && framework_->SupportsScheduleSnapshot();
}

void UnitScheduler::RegisterSchedulePerformer(
    const std::shared_ptr<resource_view::ResourceView> &resourceView,
    const std::shared_ptr<schedule_framework::Framework> &framework,
    const PreemptInstancesFunc &func, const AllocateType &type)
{
    // Domain PRE_ALLOCATION is read-heavy and benefits from immutable snapshot
    // publication. Local ALLOCATION mutates instance state on every success;
    // publishing a deep-copied dirty Unit per instance is O(n^2) for a single
    // Unit, so Local reuses UnitScheduler's queue/framework path with the
    // existing ResourceView mailbox consistency barrier.
    useScheduleSnapshot_ = type == AllocateType::PRE_ALLOCATION;
    ScheduleStrategy::RegisterSchedulePerformer(resourceView, framework, func, type);
    aggregationAllowed_ = framework != nullptr && framework->SupportsSemanticAggregation();
    // Registration happens before ScheduleQueueActor accepts requests, so both
    // queues are empty and can safely be rebuilt with the capability decision.
    runningQueue_ = CreateQueue();
    pendingQueue_ = CreateQueue();
}

void UnitScheduler::OnFrameworkPoliciesChanged()
{
    aggregationAllowed_ = framework_ != nullptr && framework_->SupportsSemanticAggregation();
    if (auto running = std::dynamic_pointer_cast<AggregatedQueue>(runningQueue_); running != nullptr) {
        running->ConfigureAggregation(aggregationAllowed_);
    }
    if (auto pending = std::dynamic_pointer_cast<AggregatedQueue>(pendingQueue_); pending != nullptr) {
        pending->ConfigureAggregation(aggregationAllowed_);
    }
}

Status UnitScheduler::BeginScheduleRound(const resource_view::ScheduleSnapshotPtr &snapshot)
{
    RETURN_STATUS_IF_NULL(snapshot, StatusCode::RESOURCE_NOT_ENOUGH, "schedule snapshot is not initialized");
    if (!UsesScheduleSnapshot()) {
        return Status(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG, "registered prefilter is not snapshot compatible");
    }
    const auto started = consumeDiagnosticsEnabled_ ? std::chrono::steady_clock::now()
                                                    : std::chrono::steady_clock::time_point{};
    ++scheduleRoundCount_;
    const auto reconcileStarted = consumeDiagnosticsEnabled_ ? std::chrono::steady_clock::now()
                                                             : std::chrono::steady_clock::time_point{};
    roundContext_->BeginRound(snapshot);
    if (consumeDiagnosticsEnabled_) {
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - reconcileStarted).count());
        reconcileTotalNanos_.fetch_add(elapsed, std::memory_order_relaxed);
        UpdateMaximum(reconcileMaxNanos_, elapsed);
    }
    preContext_ = roundContext_;
    if (scheduleViewSnapshot_ != snapshot) {
        scheduleResourceView_ = std::make_shared<resource_view::ScheduleResourceView>(snapshot);
        scheduleViewSnapshot_ = snapshot;
    }
    if (consumeDiagnosticsEnabled_) {
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count());
        roundTotalNanos_.fetch_add(elapsed, std::memory_order_relaxed);
        UpdateMaximum(roundMaxNanos_, elapsed);
    }
    return Status::OK();
}

size_t UnitScheduler::ScheduleRoundCount() const
{
    return scheduleRoundCount_.load(std::memory_order_relaxed);
}

uint64_t UnitScheduler::ConsumedSnapshotSequence() const
{
    return scheduleViewSnapshot_ == nullptr ? 0 : scheduleViewSnapshot_->publicationSequence;
}

size_t UnitScheduler::ReservationCount() const
{
    return roundContext_ == nullptr ? 0 : roundContext_->ReservationCount();
}

size_t UnitScheduler::OverlayRebuildCount() const
{
    return roundContext_ == nullptr ? 0 : roundContext_->OverlayRebuildCount();
}

size_t UnitScheduler::JournalOverflowCount() const
{
    return roundContext_ == nullptr ? 0 : roundContext_->JournalOverflowCount();
}

uint64_t UnitScheduler::ConsumedMutationSequence() const
{
    return roundContext_ == nullptr ? 0 : roundContext_->ConsumedMutationSequence();
}

RoundDiagnostics UnitScheduler::GetRoundDiagnostics() const
{
    return RoundDiagnostics{
        static_cast<uint64_t>(scheduleRoundCount_.load(std::memory_order_relaxed)),
        roundTotalNanos_.load(std::memory_order_relaxed),
        roundMaxNanos_.load(std::memory_order_relaxed),
        reconcileTotalNanos_.load(std::memory_order_relaxed),
        reconcileMaxNanos_.load(std::memory_order_relaxed),
    };
}

}  // namespace functionsystem::schedule_decision
