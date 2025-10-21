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

#include "heartbeat/ping_pong_driver.h"

#include <async/asyncafter.hpp>
#include <timer/timertools.hpp>

#include "logs/logging.h"

namespace functionsystem {

PingPongActor::PingPongActor(const std::string &name, const uint32_t &timeoutMs, const TimeOutHandler &handler)
    : ActorBase(name + PINGPONG_BASENAME),
      handler_(handler),
      timeoutMs_(timeoutMs ? timeoutMs : DEFAULT_PING_PONG_TIMEOUT)
{
}

void PingPongActor::Ping(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    // need frequency print log for debugging
    if (auto iter(pingTimers_.find(from.HashString())); iter != pingTimers_.end()) {
        (void)litebus::TimerTools::Cancel(iter->second);
    } else {
        YRLOG_INFO("received first ping from {}", from.HashString());
    }
    // Should be replaced by message
    if (msg == "Exited") {
        handler_(from, HeartbeatConnection::EXITED);
        return;
    }
    pingTimers_[from.HashString()] = litebus::AsyncAfter(timeoutMs_, GetAID(), &PingPongActor::PingTimeout, from);
    litebus::AID to(from);
    to.SetProtocol(litebus::BUS_UDP);
    (void)Send(to, "Pong", "");
}

void PingPongActor::CheckFirstPing(const litebus::AID &aid)
{
    YRLOG_INFO("check whether receive first ping from {}", aid.HashString());
    if (pingTimers_.find(aid.HashString()) != pingTimers_.end()) {
        return;
    }
    pingTimers_[aid.HashString()] = litebus::AsyncAfter(timeoutMs_, GetAID(), &PingPongActor::PingTimeout, aid);
}

void PingPongActor::PingTimeout(const litebus::AID &from)
{
    if (!pingTimers_[from.HashString()].GetTimeWatch().Expired()) {
        return;
    }
    (void)pingTimers_.erase(from.HashString());
    YRLOG_WARN("No pings from {} within {} ms", from.HashString(), timeoutMs_);
    handler_(from, HeartbeatConnection::LOST);
}

void PingPongActor::Init()
{
    YRLOG_DEBUG("init PingPongActor({})", std::string(GetAID()));
    ReceiveUdp("Ping", &PingPongActor::Ping);
}

}  // namespace functionsystem