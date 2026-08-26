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

#include "idle_actor.h"

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "common/types/instance_state.h"
#include "common/utils/struct_transfer.h"
#include "local_scheduler/instance_control/instance_ctrl_actor.h"

namespace functionsystem::local_scheduler {

namespace {
bool IsSamePauseGateIdentity(const resources::InstanceInfo &left, const resources::InstanceInfo &right)
{
    return left.instanceid() == right.instanceid()
        && left.requestid() == right.requestid()
        && left.version() == right.version()
        && left.functionproxyid() == right.functionproxyid()
        && left.runtimeid() == right.runtimeid()
        && left.functionagentid() == right.functionagentid()
        && left.containerid() == right.containerid()
        && left.unitid() == right.unitid()
        && left.tenantid() == right.tenantid()
        && left.runtimeaddress() == right.runtimeaddress()
        && left.instancestatus().code() == right.instancestatus().code();
}
constexpr int64_t SECONDS_TO_MILLISECONDS = 1000;
}  // namespace

IdleActor::IdleActor(const std::string &name,
                     const std::string &nodeID,
                     const std::shared_ptr<InstanceControlView> &instanceControlView,
                     const litebus::AID &facadeAID)
    : BasisActor(name), nodeID_(nodeID), instanceControlView_(instanceControlView), facadeAID_(facadeAID)
{
}

void IdleActor::Init()
{
}

void IdleActor::Finalize()
{
    for (auto &[instanceID, timer] : idleTimers_) {
        litebus::TimerTools::Cancel(timer);
    }
    idleTimers_.clear();
    pauseGatedInstances_.clear();
}

void IdleActor::TrafficReport(const std::string &instanceID, const size_t &processingNum)
{
    YRLOG_DEBUG("debug:: instance({}) processing num: {}", instanceID, processingNum);
    bool isIdle = (processingNum == 0);
    ASSERT_IF_NULL(instanceControlView_);
    if (!isIdle) {
        instanceTrafficIdle_.erase(instanceID);
        if (pauseGatedInstances_.find(instanceID) == pauseGatedInstances_.end()) {
            CancelIdleTimer(instanceID);
        }
        return;
    }

    instanceTrafficIdle_[instanceID] = true;
    if (pauseGatedInstances_.find(instanceID) != pauseGatedInstances_.end()) {
        return;
    }

    // Only start idle timer if both traffic idle AND no active sessions
    bool hasActiveSessions = false;
    auto it = instanceActiveSessions_.find(instanceID);
    if (it != instanceActiveSessions_.end()) {
        hasActiveSessions = it->second;
    }

    if (!hasActiveSessions) {
        StartIdleTimer(instanceID);
    } else {
        YRLOG_DEBUG("instance({}) is idle but has active exec sessions, skip idle timer", instanceID);
    }
}

void IdleActor::SessionCountDelta(const std::string &instanceID, int delta)
{
    if (instanceID.empty() || delta == 0) {
        return;
    }

    auto &count = instanceSessionCounts_[instanceID];
    size_t oldCount = count;

    if (delta > 0) {
        count += static_cast<size_t>(delta);
    } else if (delta < 0 && count > 0) {
        size_t dec = static_cast<size_t>(-delta);
        count = (dec >= count) ? 0 : (count - dec);
    }

    size_t newCount = count;
    if (newCount == 0) {
        instanceSessionCounts_.erase(instanceID);
    }

    // Edge detection: 0->N or N->0
    if ((oldCount == 0 && newCount > 0) || (oldCount > 0 && newCount == 0)) {
        bool hasActiveSessions = (newCount > 0);
        YRLOG_INFO("instance({}) session count edge: {} sessions, hasActiveSessions={}",
                   instanceID, newCount, hasActiveSessions);
        SessionAlive(instanceID, hasActiveSessions);
    }
}

void IdleActor::SessionAlive(const std::string &instanceID, bool hasActiveSessions)
{
    YRLOG_INFO("instance({}) session alive status changed: hasActiveSessions={}", instanceID, hasActiveSessions);

    if (hasActiveSessions) {
        instanceActiveSessions_[instanceID] = true;
        if (pauseGatedInstances_.find(instanceID) != pauseGatedInstances_.end()) {
            return;
        }
        // Cancel idle timer when sessions become active
        CancelIdleTimer(instanceID);
    } else {
        instanceActiveSessions_.erase(instanceID);
        if (pauseGatedInstances_.find(instanceID) != pauseGatedInstances_.end()) {
            return;
        }
        // When sessions become inactive, check traffic idle status before starting timer
        bool trafficIdle = false;
        auto trafficIt = instanceTrafficIdle_.find(instanceID);
        if (trafficIt != instanceTrafficIdle_.end()) {
            trafficIdle = trafficIt->second;
        }
        ASSERT_IF_NULL(instanceControlView_);
        auto stateMachine = instanceControlView_->GetInstance(instanceID);
        if (trafficIdle && stateMachine != nullptr) {
            const auto &instanceInfo = stateMachine->GetInstanceInfo();
            if (instanceInfo.functionproxyid() == nodeID_ &&
                instanceInfo.instancestatus().code() == static_cast<int32_t>(InstanceState::RUNNING)) {
                StartIdleTimer(instanceID);
            }
        }
    }
}

void IdleActor::OnInstanceRunning(const resources::InstanceInfo &identity)
{
    const auto &instanceID = identity.instanceid();
    if (instanceID.empty() || instanceControlView_ == nullptr) {
        return;
    }
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        return;
    }
    const auto current = stateMachine->GetInstanceInfo();
    if (current.functionproxyid() != nodeID_
        || current.instancestatus().code() != static_cast<int32_t>(InstanceState::RUNNING)
        || !IsSamePauseGateIdentity(identity, current)) {
        return;
    }
    if (auto gate = pauseGatedInstances_.find(instanceID); gate != pauseGatedInstances_.end()) {
        if (IsSamePauseGateIdentity(gate->second.identity, identity)) {
            return;
        }
        pauseGatedInstances_.erase(gate);
    }
    auto trafficIt = instanceTrafficIdle_.find(instanceID);
    if (trafficIt == instanceTrafficIdle_.end() || !trafficIt->second) {
        return;
    }
    auto sessionIt = instanceActiveSessions_.find(instanceID);
    if (sessionIt != instanceActiveSessions_.end() && sessionIt->second) {
        YRLOG_DEBUG("instance({}) is running and idle but has active exec sessions, skip idle timer", instanceID);
        return;
    }
    StartIdleTimer(instanceID);
}

void IdleActor::StartIdleTimer(const std::string &instanceID)
{
    if (pauseGatedInstances_.find(instanceID) != pauseGatedInstances_.end()) {
        return;
    }
    if (idleTimers_.find(instanceID) != idleTimers_.end()) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        return;
    }
    const auto &instanceInfo = stateMachine->GetInstanceInfo();
    if (instanceInfo.functionproxyid() != nodeID_ ||
        instanceInfo.instancestatus().code() != static_cast<int32_t>(InstanceState::RUNNING)) {
        return;
    }

    // Don't start timer if there are active sessions
    auto it = instanceActiveSessions_.find(instanceID);
    if (it != instanceActiveSessions_.end() && it->second) {
        YRLOG_INFO("skip starting idle timer for instance({}) due to active sessions", instanceID);
        return;
    }

    int64_t idleTimeout = GetIdleTimeout(instanceInfo);
    if (idleTimeout <= 0) {
        return;
    }

    // Stamp the timer with current generation to enable stale-callback detection
    auto gen = ++instanceTimerGeneration_[instanceID];
    YRLOG_INFO("start idle timer for instance({}) with timeout {} seconds (gen={})", instanceID, idleTimeout, gen);
    idleTimers_[instanceID] = litebus::AsyncAfter(
        idleTimeout * SECONDS_TO_MILLISECONDS, GetAID(), &IdleActor::HandleIdleTimeout, instanceID, gen);
}

void IdleActor::CancelIdleTimer(const std::string &instanceID)
{
    // Increment generation first: invalidates any in-flight timeout callback
    // that is already queued in this actor's mailbox but hasn't executed yet.
    ++instanceTimerGeneration_[instanceID];

    auto iter = idleTimers_.find(instanceID);
    if (iter == idleTimers_.end()) {
        return;
    }
    YRLOG_INFO("cancel idle timer for instance({})", instanceID);
    litebus::TimerTools::Cancel(iter->second);
    idleTimers_.erase(iter);
}

void IdleActor::HandleIdleTimeout(const std::string &instanceID, uint64_t generation)
{
    // Check whether this callback is stale (generation was incremented by CancelIdleTimer
    // after the timer fired but before this callback executed in the actor mailbox).
    auto genIt = instanceTimerGeneration_.find(instanceID);
    if (genIt != instanceTimerGeneration_.end() && genIt->second != generation) {
        YRLOG_INFO("instance({}) idle timeout callback is stale (gen={} vs current={}), skip",
                   instanceID, generation, genIt->second);
        return;
    }

    idleTimers_.erase(instanceID);
    if (pauseGatedInstances_.find(instanceID) != pauseGatedInstances_.end()) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        return;
    }

    // Double-check: ensure no active sessions before requesting eviction
    auto it = instanceActiveSessions_.find(instanceID);
    if (it != instanceActiveSessions_.end() && it->second) {
        YRLOG_INFO("{}|instance({}) idle timeout cancelled due to active sessions",
                   stateMachine->GetInstanceInfo().requestid(), instanceID);
        return;
    }

    const auto &instanceInfo = stateMachine->GetInstanceInfo();
    YRLOG_INFO("{}|instance({}) idle timeout, requesting eviction via InstanceCtrlActor",
               instanceInfo.requestid(), instanceID);

    litebus::Async(facadeAID_, &InstanceCtrlActor::EvictByIdleTimeout, instanceID);
}

Status IdleActor::SetPauseGated(const resources::InstanceInfo &identity, uint64_t token, bool gated)
{
    const auto &instanceID = identity.instanceid();
    if (instanceID.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "instance ID is empty");
    }
    if (token == 0 || instanceControlView_ == nullptr) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID, "idle pause gate identity is not available");
    }
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        return Status(StatusCode::ERR_INSTANCE_NOT_FOUND, "idle pause gate instance not found");
    }
    const auto current = stateMachine->GetInstanceInfo();
    if (current.functionproxyid() != nodeID_
        || current.instancestatus().code() != static_cast<int32_t>(InstanceState::RUNNING)
        || !IsSamePauseGateIdentity(identity, current)) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID, "idle pause gate identity changed");
    }
    if (gated) {
        auto existing = pauseGatedInstances_.find(instanceID);
        if (existing != pauseGatedInstances_.end()) {
            if (existing->second.token == token && IsSamePauseGateIdentity(existing->second.identity, identity)) {
                return Status::OK();
            }
            return Status(StatusCode::ERR_INSTANCE_BUSY, "idle pause gate belongs to another operation");
        }
        PauseGateRecord record;
        record.identity.CopyFrom(identity);
        record.token = token;
        pauseGatedInstances_.emplace(instanceID, std::move(record));
        CancelIdleTimer(instanceID);
        return Status::OK();
    }

    auto existing = pauseGatedInstances_.find(instanceID);
    if (existing == pauseGatedInstances_.end()) {
        return Status::OK();
    }
    if (existing->second.token != token || !IsSamePauseGateIdentity(existing->second.identity, identity)) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID, "idle pause gate token changed");
    }
    pauseGatedInstances_.erase(existing);
    const auto trafficIt = instanceTrafficIdle_.find(instanceID);
    const bool trafficIdle = trafficIt != instanceTrafficIdle_.end() && trafficIt->second;
    const auto sessionIt = instanceActiveSessions_.find(instanceID);
    const bool hasActiveSessions = sessionIt != instanceActiveSessions_.end() && sessionIt->second;
    if (trafficIdle && !hasActiveSessions) {
        StartIdleTimer(instanceID);
    }
    return Status::OK();
}

}  // namespace functionsystem::local_scheduler
