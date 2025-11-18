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

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/check_link.h"

namespace functionsystem {

HeartbeatObserver::HeartbeatObserver(const std::string &name, const uint32_t &timeoutMs)
    : ActorBase(name), timeoutMs_(timeoutMs ? timeoutMs : DEFAULT_PING_PONG_TIMEOUT)
{
}

void HeartbeatObserver::Init()
{
    YRLOG_DEBUG("init HeartbeatObserver({})", std::string(GetAID()));
    Receive("Ping", &HeartbeatObserver::Ping);
}

void HeartbeatObserver::Ping(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    std::string key = from.HashString();

    auto it = nodeInfos_.find(key);
    if (it != nodeInfos_.end()) {
        (void)litebus::TimerTools::Cancel(it->second.timer);
    } else {
        YRLOG_DEBUG("received first ping from {}", key);
    }

    if (it != nodeInfos_.end()) {
        it->second.timer = litebus::AsyncAfter(it->second.timeoutMs, GetAID(), &HeartbeatObserver::PingTimeout, from);
    }

    litebus::AID to(from);
    to.SetProtocol(litebus::BUS_TCP);
    (void)Send(to, "Pong", "");
}

void HeartbeatObserver::AddNewHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs, const TimeOutHandler &handler)
{
    YRLOG_DEBUG("add new node, check whether receive first ping from {}, GetAID():{}, timeout:{}", aid.HashString(),
                std::string(GetAID()), timeoutMs);
    std::string key = aid.HashString();

    auto it = nodeInfos_.find(key);
    if (it != nodeInfos_.end()) {
        return;
    }

    nodeInfos_[key] = { .timeoutMs = timeoutMs,
                        .handler = handler,
                        .timer = litebus::AsyncAfter(timeoutMs, GetAID(), &HeartbeatObserver::PingTimeout, aid) };

    YRLOG_INFO("Added node {} with timeout {} ms", key, timeoutMs);
}

void HeartbeatObserver::RemoveHeartbeatNode(const litebus::AID &aid)
{
    std::string key = aid.HashString();
    auto it = nodeInfos_.find(key);
    if (it == nodeInfos_.end()) {
        return;
    }
    YRLOG_DEBUG("remove node, node aid: {}, GetAID():{}", aid.HashString(), std::string(GetAID()));
    auto &nodeInfo = it->second;
    (void)litebus::TimerTools::Cancel(nodeInfo.timer);
    auto handler = nodeInfo.handler;
    handler(aid, HeartbeatConnection::EXITED);
    (void)nodeInfos_.erase(it);
    YRLOG_DEBUG("remove node success, node aid: {}, GetAID():{}", aid.HashString(), std::string(GetAID()));
}

void HeartbeatObserver::PingTimeout(const litebus::AID &from)
{
    std::string key = from.HashString();

    auto it = nodeInfos_.find(key);
    if (it != nodeInfos_.end()) {
        auto &nodeInfo = it->second;
        if (!nodeInfo.timer.GetTimeWatch().Expired()) {
            return;
        }
        auto handler = nodeInfo.handler;
        handler(from, HeartbeatConnection::LOST);
        (void)nodeInfos_.erase(it);
        YRLOG_WARN("No pings from {} within {} ms", key, nodeInfo.timeoutMs);
    } else {
        YRLOG_WARN("PingTimeout for node {} not found in nodeInfos_", key);
    }
}

void HeartbeatObserver::ResetHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs,
                                                  const HeartbeatObserver::TimeOutHandler &handler)
{
    YRLOG_DEBUG("Reset heartbeat node handler {}, GetAID():{}, timeout:{}", aid.HashString(),
                std::string(GetAID()), timeoutMs);
    std::string key = aid.HashString();

    auto it = nodeInfos_.find(key);
    if (it == nodeInfos_.end()) {
        return;
    }
    (void)litebus::TimerTools::Cancel(nodeInfos_[key].timer);
    nodeInfos_[key] = { .timeoutMs = timeoutMs,
                        .handler = handler,
                        .timer = litebus::AsyncAfter(timeoutMs, GetAID(), &HeartbeatObserver::PingTimeout, aid) };

    YRLOG_INFO("Reset node {} with timeout {} ms", key, timeoutMs);
}

}  // namespace functionsystem