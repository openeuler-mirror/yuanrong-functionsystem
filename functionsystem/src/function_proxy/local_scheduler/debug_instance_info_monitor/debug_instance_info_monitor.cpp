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

#include "debug_instance_info_monitor.h"

namespace functionsystem::local_scheduler {
void DebugInstanceInfoMonitor::Start()
{
    // Async call to wait for some data structure to load
    litebus::AsyncAfter(monitorInterval_, GetAID(), &DebugInstanceInfoMonitor::DebugInstInfoCheckTimer);
}

void DebugInstanceInfoMonitor::DebugInstInfoCheckTimer()
{
    // 定时对debugInstanceinfo 状态刷新
    (void)funcAgentMgr_->QueryDebugInstanceInfos().OnComplete([this](const litebus::Future<Status> &status) {
        litebus::AsyncAfter(monitorInterval_, GetAID(), &DebugInstanceInfoMonitor::DebugInstInfoCheckTimer);
    });
}
}