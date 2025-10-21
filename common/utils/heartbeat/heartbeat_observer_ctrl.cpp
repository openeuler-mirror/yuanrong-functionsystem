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

#include "heartbeat_observer_ctrl.h"

#include "ping_pong_driver.h"

namespace {
const uint32_t MIN_PING_TIMES = 5;
const uint32_t MIN_PING_CYCLE = 1000;  // ms
}  // namespace

namespace functionsystem {

using std::string;

HeartbeatObserverCtrl::HeartbeatObserverCtrl(uint32_t pingTimes, uint32_t pingCycleMs)
    : pingTimes_(pingTimes > MIN_PING_TIMES ? pingTimes : MIN_PING_TIMES),
      pingCycleMs_(pingCycleMs > MIN_PING_CYCLE ? pingCycleMs : MIN_PING_CYCLE)
{
}

litebus::Future<Status> HeartbeatObserverCtrl::Add(const string &id, const string &address,
                                                   const HeartbeatObserver::TimeOutHandler handler)
{
    auto &heartbeat = heartbeats_[id];
    if (heartbeat != nullptr) {
        YRLOG_INFO("build heartbeat for {} already.", id);
        return Status(StatusCode::SUCCESS);
    }

    litebus::AID pingPongAID(id + PINGPONG_BASENAME, address);

    heartbeat = std::make_unique<HeartbeatObserveDriver>(id, pingPongAID, pingTimes_, pingCycleMs_, handler);

    if (auto ret = heartbeat->Start(); ret != 0) {
        YRLOG_ERROR("build heartbeat for {} fail, aid: {}, ret: {}.", id, pingPongAID.HashString(), ret);
        return Status(StatusCode::LS_AGENT_MGR_START_HEART_BEAT_FAIL);
    }

    YRLOG_INFO("build heartbeat for ({}) successfully. aid: {}, ping times: {}, ping cycle(ms): {}", id,
               pingPongAID.HashString(), pingTimes_, pingCycleMs_);

    return Status(StatusCode::SUCCESS);
}

void HeartbeatObserverCtrl::Delete(const std::string &id)
{
    heartbeats_[id] = nullptr;
    YRLOG_INFO("disconnect heartbeat for {}.", id);
}
}  // namespace functionsystem
