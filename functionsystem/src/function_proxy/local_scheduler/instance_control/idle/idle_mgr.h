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

#ifndef LOCAL_SCHEDULER_IDLE_MGR_H
#define LOCAL_SCHEDULER_IDLE_MGR_H

#include <actor/actor.hpp>
#include <async/async.hpp>
#include <string>

#include "idle_actor.h"

namespace functionsystem::local_scheduler {

/**
 * IdleMgr wraps IdleActor's AID and provides a thread-safe interface for
 * forwarding idle-related events via litebus::Async.
 *
 * Consumers (InstanceCtrlActor) should never include idle_actor.h directly —
 * they use IdleMgr for all interactions with the idle subsystem.
 */
class IdleMgr {
public:
    explicit IdleMgr(const litebus::AID &idleActorAID) : idleActorAID_(idleActorAID) {}

    /**
     * Forward a traffic report to IdleActor.
     * Called from InstanceCtrlActor::TrafficReport (private forwarding method).
     */
    void TrafficReport(const std::string &instanceID, const size_t &processingNum)
    {
        litebus::Async(idleActorAID_, &IdleActor::TrafficReport, instanceID, processingNum);
    }

    /**
     * Forward a session count delta to IdleActor.
     * Called from InstanceCtrlActor::SessionCountDelta after updating local session state.
     */
    void SessionCountDelta(const std::string &instanceID, int delta)
    {
        litebus::Async(idleActorAID_, &IdleActor::SessionCountDelta, instanceID, delta);
    }

private:
    litebus::AID idleActorAID_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_IDLE_MGR_H
