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

#include <memory>
#include <string>

namespace functionsystem::local_scheduler {

class IdleActor;

/**
 * IdleMgr owns the IdleActor lifecycle and is the unified external interface
 * for the idle subsystem.
 *
 * All callers (InstanceCtrlActor, ExecStreamService, ...) MUST route idle
 * events through IdleMgr.  IdleActor internals are hidden in idle_mgr.cpp
 * so that consumers need not include idle_actor.h.
 */
class IdleMgr {
public:
    explicit IdleMgr(std::shared_ptr<IdleActor> idleActor);

    ~IdleMgr();

    void Spawn();
    void Stop();
    void Await();

    void TrafficReport(const std::string &instanceID, const size_t &processingNum);

    void SessionCountDelta(const std::string &instanceID, int delta);

private:
    std::shared_ptr<IdleActor> idleActor_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_IDLE_MGR_H
