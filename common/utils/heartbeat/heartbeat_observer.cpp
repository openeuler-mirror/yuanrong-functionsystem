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

#include "heartbeat_observer.h"

#include <async/asyncafter.hpp>

#include "logs/logging.h"
#include "status/status.h"
#include "check_link.h"

namespace functionsystem {
const uint32_t DEFAULT_PING_NUMS = 12;
const uint32_t DEFAULT_PING_CYCLE = 1000;

HeartbeatObserver::HeartbeatObserver(const std::string &name, const litebus::AID &dst, uint32_t maxPingTimeoutNums,
                                     uint32_t pingCycleMs, const TimeOutHandler &handler)
    : ActorBase(name + HEARTBEAT_BASENAME),
      dst_(dst),
      maxPingTimeoutNums_(maxPingTimeoutNums == 0 ? DEFAULT_PING_NUMS : maxPingTimeoutNums),
      pingCycleMs_(pingCycleMs == 0 ? DEFAULT_PING_CYCLE : pingCycleMs),
      timeoutHandler_(handler),
      timeouts_(0),
      pinged_(false),
      started_(false),
      reConnectTimes_(0)
{
    dst_.SetProtocol(litebus::BUS_UDP);
}

HeartbeatObserver::HeartbeatObserver(const std::string &name, const litebus::AID &dst, const TimeOutHandler &handler)
    : HeartbeatObserver(name, dst, DEFAULT_PING_NUMS, DEFAULT_PING_CYCLE, handler)
{
}

void HeartbeatObserver::Init()
{
    YRLOG_DEBUG("init HeartbeatObserver({})", std::string(GetAID()));
    ReceiveUdp("Pong", &HeartbeatObserver::Pong);
}

void HeartbeatObserver::Ping()
{
    if (!started_) {
        return;
    }
    if (auto size(Send(dst_, "Ping", "")); size >= static_cast<int>(maxPingTimeoutNums_)) {
        YRLOG_WARN("send size queue of waiting to write is too large. to({}) size({}).", std::string(dst_), size);
    }
    pinged_ = true;
    nextTimer_ = litebus::AsyncAfter(pingCycleMs_, GetAID(), &HeartbeatObserver::NextPing);
}

void HeartbeatObserver::NextPing()
{
    // if pinged_ is true, lastest ping request was not response
    if (pinged_) {
        timeouts_++;
        YRLOG_WARN("not receive pong from {} {}-times", std::string(dst_), timeouts_);
        if (timeouts_ >= maxPingTimeoutNums_) {
            YRLOG_WARN("{} heart beat lost, ping without response {}-times reach the threshold", std::string(dst_),
                       timeouts_);
            ASSERT_FS(timeoutHandler_);
            timeoutHandler_(dst_);
            started_ = false;
            return;
        }
    }
    Ping();
}

void HeartbeatObserver::Pong(const litebus::AID &, std::string &&, std::string &&)
{
    reConnectTimes_ = 0;
    timeouts_ = 0;
    pinged_ = false;
}

bool HeartbeatObserver::Stop()
{
    YRLOG_DEBUG("heartbeat({}) begin stop", std::string(GetAID()));
    if (!started_) {
        return true;
    }
    (void)Send(dst_, "Ping", "Exited");
    if (!nextTimer_.GetTimeWatch().Expired()) {
        YRLOG_DEBUG("heartbeat({}) cancel send ping", std::string(GetAID()));
        (void)litebus::TimerTools::Cancel(nextTimer_);
    }
    (void)UnLink(dst_);
    started_ = false;
    return true;
}

int HeartbeatObserver::Start()
{
    YRLOG_DEBUG("hearbeat aid({}) start", std::string(GetAID()));
    if (started_) {
        YRLOG_INFO("heartbeat observer already started");
        return 0;
    }

    started_ = true;
    Ping();
    return 0;
}

void HeartbeatObserver::Exited(const litebus::AID &actor)
{
    if (!started_) {
        YRLOG_DEBUG("{} heartbeat already closed, don't need to reconnect.", std::string(actor));
        return;
    }

    if (reConnectTimes_ > DEFAULT_PING_NUMS) {
        YRLOG_WARN("{} heartbeat connection lost, exceed max reconnect times {}.", std::string(actor), reConnectTimes_);
        ASSERT_FS(timeoutHandler_);
        timeoutHandler_(actor);
        return;
    }
    YRLOG_WARN("{} heartbeat connection lost, try reconnect times {}.", std::string(actor), reConnectTimes_);
    reConnectTimes_++;
    if (auto ret(Reconnect(actor)); ret < 0) {
        YRLOG_ERROR("heartbeat reconnection failed. {} lost code({})", std::string(actor), ret);
        ASSERT_FS(timeoutHandler_);
        timeoutHandler_(actor);
        return;
    }
}
void HeartbeatObserver::Finalize()
{
    (void)Stop();
}
}  // namespace functionsystem