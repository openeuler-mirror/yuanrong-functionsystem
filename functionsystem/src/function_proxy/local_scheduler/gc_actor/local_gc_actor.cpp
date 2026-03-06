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

#include "local_gc_actor.h"

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "common/types/instance_state.h"

namespace functionsystem::local_scheduler {

namespace {

/**
 * Returns true if the given state is a terminal abnormal state that qualifies
 * for GC after the retention window expires.
 * EXITED and EVICTED are expected lifecycle endings; FATAL is a hard crash.
 */
bool IsTerminalAbnormalState(InstanceState state)
{
    return state == InstanceState::EXITED || state == InstanceState::EVICTED ||
           state == InstanceState::FATAL;
}

/**
 * Returns true if the given state is a transient or failure state that qualifies
 * for GC when the instance has been stuck in it for too long.
 * FAILED and SCHEDULE_FAILED are retriable failures, not hard terminal states.
 */
bool IsStuckTransientState(InstanceState state)
{
    return state == InstanceState::CREATING || state == InstanceState::SCHEDULING ||
           state == InstanceState::FAILED || state == InstanceState::SCHEDULE_FAILED;
}

}  // namespace

LocalGcActor::LocalGcActor(const std::string &name,
                           const std::string &nodeID,
                           uint32_t gcIntervalMs,
                           uint32_t terminalRetentionMs,
                           uint32_t stuckTimeoutMs)
    : BasisActor(name),
      member_nodeID(nodeID),
      member_gcIntervalMs(gcIntervalMs),
      member_terminalRetentionMs(terminalRetentionMs),
      member_stuckTimeoutMs(stuckTimeoutMs)
{
}

void LocalGcActor::Init()
{
    BasisActor::Init();
    YRLOG_INFO("LocalGcActor initialized on node: {}, gcInterval: {}ms, terminalRetention: {}ms, stuckTimeout: {}ms",
               member_nodeID, member_gcIntervalMs, member_terminalRetentionMs, member_stuckTimeoutMs);
    (void)litebus::AsyncAfter(member_gcIntervalMs, GetAID(), &LocalGcActor::RunGcCycle);
}

void LocalGcActor::Finalize()
{
    YRLOG_INFO("LocalGcActor finalizing on node: {}", member_nodeID);
    BasisActor::Finalize();
}

void LocalGcActor::RunGcCycle()
{
    CleanupAbnormalInstances();
    (void)litebus::AsyncAfter(member_gcIntervalMs, GetAID(), &LocalGcActor::RunGcCycle);
}

void LocalGcActor::CleanupAbnormalInstances()
{
    if (member_instanceControlView == nullptr) {
        YRLOG_WARN("LocalGcActor: instanceControlView not bound, skipping GC cycle");
        return;
    }
    if (member_instanceCtrl == nullptr) {
        YRLOG_WARN("LocalGcActor: instanceCtrl not bound, skipping GC cycle");
        return;
    }

    auto instances = member_instanceControlView->GetInstances();
    if (instances.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    uint32_t cleanedCount = 0;
    uint32_t pendingCount = 0;

    for (const auto &[instanceID, stateMachine] : instances) {
        if (stateMachine == nullptr) {
            continue;
        }
        InstanceState state = stateMachine->GetInstanceState();
        bool isAbnormal = IsTerminalAbnormalState(state) || IsStuckTransientState(state);
        if (!isAbnormal) {
            member_abnormalFirstSeenTimes.erase(instanceID);
            continue;
        }

        auto it = member_abnormalFirstSeenTimes.find(instanceID);
        if (it == member_abnormalFirstSeenTimes.end()) {
            member_abnormalFirstSeenTimes.emplace(instanceID, now);
            YRLOG_INFO("LocalGcActor: instance {} first seen in abnormal state {}",
                       instanceID, static_cast<int32_t>(state));
            ++pendingCount;
            continue;
        }

        uint32_t threshold = IsTerminalAbnormalState(state) ? member_terminalRetentionMs : member_stuckTimeoutMs;
        int64_t elapsedMs = ElapsedMs(it->second, now);
        if (elapsedMs < static_cast<int64_t>(threshold)) {
            ++pendingCount;
            continue;
        }

        YRLOG_INFO("LocalGcActor: reclaiming instance {} in state {} (abnormal for {}ms, threshold {}ms)",
                   instanceID, static_cast<int32_t>(state), elapsedMs, threshold);
        member_abnormalFirstSeenTimes.erase(it);
        (void)member_instanceCtrl->ForceDeleteInstance(instanceID)
            .Then(litebus::Defer(GetAID(), &LocalGcActor::OnForceDeleteComplete, instanceID,
                                 std::placeholders::_1));
        ++cleanedCount;
    }

    PurgeVanishedEntries(instances);

    if (cleanedCount > 0 || pendingCount > 0) {
        YRLOG_INFO("LocalGcActor: GC cycle complete on node {}: cleaned {} instances, {} still pending",
                   member_nodeID, cleanedCount, pendingCount);
    }
}

Status LocalGcActor::OnForceDeleteComplete(const std::string &instanceID, const Status &status)
{
    if (status.IsError()) {
        YRLOG_WARN("LocalGcActor: ForceDeleteInstance failed for instance {} on node {}: {}",
                   instanceID, member_nodeID, status.GetMessage());
    }
    return status;
}

void LocalGcActor::PurgeVanishedEntries(
    const std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>> &instances)
{
    for (auto it = member_abnormalFirstSeenTimes.begin(); it != member_abnormalFirstSeenTimes.end();) {
        if (instances.find(it->first) == instances.end()) {
            it = member_abnormalFirstSeenTimes.erase(it);
        } else {
            ++it;
        }
    }
}

int64_t LocalGcActor::ElapsedMs(const std::chrono::steady_clock::time_point &since,
                                 const std::chrono::steady_clock::time_point &now)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count();
}

}  // namespace functionsystem::local_scheduler
