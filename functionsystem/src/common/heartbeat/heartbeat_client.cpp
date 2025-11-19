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

#include "common/heartbeat/heartbeat_client.h"

#include <async/asyncafter.hpp>
#include <timer/timertools.hpp>

#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem {

HeartbeatClient::HeartbeatClient(const std::string &name, uint32_t maxPingTimeoutNums, uint32_t pingCycleMs,
                                 const TimeOutHandler &handler)
    : ActorBase(HEARTBEAT_CLIENT_BASENAME + name),
      maxPingTimeoutNums_(maxPingTimeoutNums == 0 ? DEFAULT_PING_NUMS : maxPingTimeoutNums),
      pingCycleMs_(pingCycleMs == 0 ? DEFAULT_PING_CYCLE : pingCycleMs),
      timeoutHandler_(handler),
      timeouts_(0),
      pinged_(false),
      started_(false),
      status_(Status(StatusCode::FAILED, "HeartbeatClient is not Started"))
{
}

HeartbeatClient::HeartbeatClient(const std::string &name, const TimeOutHandler &handler)
    : HeartbeatClient(name, DEFAULT_PING_NUMS, DEFAULT_PING_CYCLE, handler)
{
}

void HeartbeatClient::Init()
{
    YRLOG_DEBUG("init HeartbeatClient({})", std::string(GetAID()));
    Receive("Pong", &HeartbeatClient::Pong);
}

void HeartbeatClient::Ping()
{
    if (!started_) {
        return;
    }

    if (auto size(Send(dst_, "Ping", "")); size >= static_cast<int>(maxPingTimeoutNums_)) {
        YRLOG_WARN("send size queue of waiting to write is too large. to({}) size({}).", std::string(dst_), size);
    }
    pinged_ = true;
    nextTimer_ = litebus::AsyncAfter(pingCycleMs_, GetAID(), &HeartbeatClient::NextPing);
}

void HeartbeatClient::NextPing()
{
    // if pinged_ is true, lastest ping request was not response
    if (pinged_) {
        timeouts_++;
        YRLOG_WARN("{} not receive pong from {} {}-times", std::string(GetAID()), std::string(dst_), timeouts_);
        if (timeouts_ >= maxPingTimeoutNums_) {
            YRLOG_WARN("{} heart beat lost, ping without response {}-times reach the threshold", std::string(GetAID()),
                       timeouts_);
            ASSERT_FS(timeoutHandler_);
            timeoutHandler_(dst_);
            started_ = false;
            status_ = Status(StatusCode::FAILED, "HeartbeatClient lost connection with remote service");
            return;
        }
    }
    Ping();
}

void HeartbeatClient::Pong(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    timeouts_ = 0;
    pinged_ = false;
    status_ = Status::OK();
}

void HeartbeatClient::Stop()
{
    YRLOG_INFO("heartbeat({}) begin stop", std::string(GetAID()));
    if (!started_) {
        return;
    }
    if (!nextTimer_.GetTimeWatch().Expired()) {
        YRLOG_INFO("heartbeat({}) cancel send ping", std::string(GetAID()));
        (void)litebus::TimerTools::Cancel(nextTimer_);
    }
    started_ = false;
    status_ = Status(StatusCode::FAILED, "HeartbeatClient is stopped");
    return;
}

void HeartbeatClient::Start(const litebus::AID &dst)
{
    if (started_) {
        if (dst_ == dst) {
            YRLOG_DEBUG("HeartbeatClient({}) to ({}) already started", GetAID().HashString(), dst.HashString());
            return;
        }
        YRLOG_INFO("HeartbeatClient({}) to ({}) already started, stop it first, new target is ({})",
                   GetAID().HashString(), dst_.HashString(), dst.HashString());
        Stop();
    } else {
        YRLOG_INFO("HeartbeatClient({}) start, target is ({})", GetAID().HashString(), dst.HashString());
    }
    dst_ = dst;
    dst_.SetProtocol(litebus::BUS_TCP);
    started_ = true;
    status_ = Status::OK();
    Ping();
}

void HeartbeatClient::Finalize()
{
    (void)Stop();
}

Status HeartbeatClient::GetStatus()
{
    return status_;
}
}  // namespace functionsystem