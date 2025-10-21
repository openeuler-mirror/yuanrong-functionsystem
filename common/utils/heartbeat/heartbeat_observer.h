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

#include "async/async.hpp"

namespace functionsystem {
const std::string HEARTBEAT_BASENAME = "-HeartbeatObserver";

class HeartbeatObserver : public litebus::ActorBase {
public:
    using TimeOutHandler = std::function<void(const litebus::AID &)>;
    HeartbeatObserver(const std::string &name, const litebus::AID &dst, const TimeOutHandler &handler);
    HeartbeatObserver(const std::string &name, const litebus::AID &dst, uint32_t maxPingTimeoutNums,
                      uint32_t pingCycleMs, const TimeOutHandler &handler);
    ~HeartbeatObserver() override = default;
    void Pong(const litebus::AID &from, std::string &&name, std::string &&msg);
    int Start();
    bool Stop();

protected:
    void Init() override;
    void Finalize() override;
    void Exited(const litebus::AID &actor) override;

private:
    void Ping();
    void NextPing();

    litebus::AID dst_;
    uint32_t maxPingTimeoutNums_;
    uint32_t pingCycleMs_;  // millisecond
    TimeOutHandler timeoutHandler_;
    uint32_t timeouts_;
    bool pinged_;
    bool started_;
    uint32_t reConnectTimes_;
    litebus::Timer nextTimer_;
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
    HeartbeatObserveDriver(const std::string &name, const litebus::AID &dst, uint32_t maxPingTimeoutNums,
                           uint32_t pingCycle, const HeartbeatObserver::TimeOutHandler &handler)
    {
        actor = std::make_shared<HeartbeatObserver>(name, dst, maxPingTimeoutNums, pingCycle, handler);
        (void)litebus::Spawn(actor);
    }
    HeartbeatObserveDriver(const std::string &name, const litebus::AID &dst,
                           const HeartbeatObserver::TimeOutHandler &handler)
    {
        actor = std::make_shared<HeartbeatObserver>(name, dst, handler);
        (void)litebus::Spawn(actor);
    }

    virtual ~HeartbeatObserveDriver() noexcept
    {
        litebus::Terminate(actor->GetAID());
        litebus::Await(actor->GetAID());
    }

    int Start() const
    {
        return litebus::Async(actor->GetAID(), &HeartbeatObserver::Start).Get();
    }

    int Stop() const
    {
        return litebus::Async(actor->GetAID(), &HeartbeatObserver::Stop).Get();
    }

    litebus::AID GetActorAID() const
    {
        return actor->GetAID();
    }

private:
    std::shared_ptr<HeartbeatObserver> actor{ nullptr };
};
}  // namespace functionsystem
#endif  // COMMON_HEARTBEAT_OBSERVER_H
