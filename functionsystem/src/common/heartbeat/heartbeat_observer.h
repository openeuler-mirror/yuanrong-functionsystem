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

#ifndef COMMON_HEARTBEAT_OBSERVER_H
#define COMMON_HEARTBEAT_OBSERVER_H
#include <litebus.hpp>
#include <string>
#include <timer/timer.hpp>
#include <unordered_map>

#include "async/async.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"

namespace functionsystem {
const uint32_t DEFAULT_PING_PONG_TIMEOUT = 10000;  // ms

enum HeartbeatConnection : int {
    LOST = 0,
    EXITED = 1,
};

class HeartbeatObserver : public litebus::ActorBase {
public:
    using TimeOutHandler = std::function<void(const litebus::AID &, HeartbeatConnection)>;

    struct HeartbeatNodeInfo {
        uint32_t timeoutMs;
        TimeOutHandler handler;
        litebus::Timer timer;
    };

    HeartbeatObserver(const std::string &name, const uint32_t &timeoutMs);

    void Init() override;
    virtual void Ping(const litebus::AID &from, std::string &&name, std::string &&msg);
    void AddNewHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs, const TimeOutHandler &handler);
    void RemoveHeartbeatNode(const litebus::AID &aid);
    void PingTimeout(const litebus::AID &from);
    void ResetHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs,
                                   const HeartbeatObserver::TimeOutHandler &handler);

private:
    uint32_t timeoutMs_;
    std::unordered_map<std::string, HeartbeatNodeInfo> nodeInfos_;
    std::mutex timersMutex_;
};

class HeartbeatObserveDriver {
public:
    /* *
     * HeartbeatObserveDriver constructor
     * @param name: HeartbeatObserver actor name which will be appended with '-HeartbeatObserver'
     * @param dst: heartbeat detect destination
     * @param maxPingTimeoutNums: max numbers of ping while not receiving response
     * @param pingCycle: ping cycle
     * @param handler: ping without response after maxPingTimeoutNums, handler called.
     */
    explicit HeartbeatObserveDriver(const std::string &name)
    {
        if (litebus::GetActor(HEARTBEAT_OBSERVER_BASENAME + name) == nullptr) {
            actor = std::make_shared<HeartbeatObserver>(HEARTBEAT_OBSERVER_BASENAME + name, DEFAULT_PING_PONG_TIMEOUT);
            (void)litebus::Spawn(actor);
        } else {
            actor = std::static_pointer_cast<HeartbeatObserver>(litebus::GetActor(HEARTBEAT_OBSERVER_BASENAME + name));
            YRLOG_INFO("HeartbeatObserver {} constructed", actor->GetAID().HashString());
        }
    }

    virtual ~HeartbeatObserveDriver() noexcept
    {
        if (actor != nullptr) {
            YRLOG_INFO("HeartbeatObserver {} destructed", actor->GetAID().HashString());
            litebus::Terminate(actor->GetAID());
            litebus::Await(actor->GetAID());
            actor = nullptr;
        }
    }

    litebus::AID GetActorAID() const
    {
        return actor->GetAID();
    }

    void AddNewHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs,
                             const HeartbeatObserver::TimeOutHandler &handler) const
    {
        if (actor != nullptr) {
            litebus::Async(actor->GetAID(), &HeartbeatObserver::AddNewHeartbeatNode, aid, timeoutMs, handler);
        }
    }

    void RemoveHeartbeatNode(const litebus::AID &aid) const
    {
        if (actor != nullptr) {
            litebus::Async(actor->GetAID(), &HeartbeatObserver::RemoveHeartbeatNode, aid);
        }
    }

    void ResetHeartbeatNode(const litebus::AID &aid, uint32_t timeoutMs,
                            const HeartbeatObserver::TimeOutHandler &handler)
    {
        if (actor != nullptr) {
            litebus::Async(actor->GetAID(), &HeartbeatObserver::ResetHeartbeatNode, aid, timeoutMs, handler);
        }
    }

private:
    std::shared_ptr<HeartbeatObserver> actor{ nullptr };
};
}  // namespace functionsystem
#endif  // COMMON_HEARTBEAT_OBSERVER_H
