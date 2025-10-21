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

#ifndef DEBUG_INSTANCE_INFO_MONITOR_H
#define DEBUG_INSTANCE_INFO_MONITOR_H


#include "actor/actor.hpp"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"

namespace functionsystem::local_scheduler {

class DebugInstanceInfoMonitor : public litebus::ActorBase {
public:
    explicit DebugInstanceInfoMonitor(const std::shared_ptr<local_scheduler::FunctionAgentMgr> funcAgentMgr,
                                      uint64_t monitorInterval_ = QUERY_DEBUG_INSTANCE_INFO_INTERVAL_MS)
        : litebus::ActorBase("DebugInstanceInfoMonitor"),
          monitorInterval_(monitorInterval_),
          funcAgentMgr_(funcAgentMgr)
    {
    }

    ~DebugInstanceInfoMonitor() override = default;

    void Start();

private:
    void DebugInstInfoCheckTimer();
    uint64_t monitorInterval_;
    std::shared_ptr<local_scheduler::FunctionAgentMgr> funcAgentMgr_;
};
}

#endif // DEBUG_INSTANCE_INFO_MONITOR_H
