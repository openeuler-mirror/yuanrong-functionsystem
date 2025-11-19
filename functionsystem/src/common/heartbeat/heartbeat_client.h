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

#ifndef COMMON_PING_PONG_DRIVER_H
#define COMMON_PING_PONG_DRIVER_H
#include <litebus.hpp>
#include <memory>
#include <string>
#include <timer/timer.hpp>
#include <unordered_map>

#include "async/async.hpp"
#include "common/status/status.h"

namespace functionsystem {
const uint32_t DEFAULT_PING_NUMS = 12;
const uint32_t DEFAULT_PING_CYCLE = 1000;

class HeartbeatClient : public litebus::ActorBase {
public:
    using TimeOutHandler = std::function<void(const litebus::AID &)>;
    /**
     * PingPongActor constructor
     * @param name: actor name which will be appended with -PingPong
     * @param timeoutMs: millisecond. while received a new ping from some other actor,
     * if not receviced subsequent ping within timeout, TimeOutHandler will be invoked.
     * @param handler: regitered timeout handler
     */
    HeartbeatClient(const std::string &name, const TimeOutHandler &handler);
    HeartbeatClient(const std::string &name, uint32_t maxPingTimeoutNums, uint32_t pingCycleMs,
                    const TimeOutHandler &handler);

    ~HeartbeatClient() override = default;

    void Pong(const litebus::AID &from, std::string &&name, std::string &&msg);
    void Start(const litebus::AID &dst);
    void Stop();
    Status GetStatus();

protected:
    void Init() override;
    void Finalize() override;

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
    litebus::Timer nextTimer_;
    Status status_;
};

class HeartbeatClientDriver {
public:
    /**
     * PingPongDriver constructor, spawn PingPongActor
     * @param name: actor name which will be appended with -PingPong
     * @param timeoutMs: millisecond. while received a new ping from some other actor,
     * if not receviced subsequent ping within timeout, TimeOutHandler will be invoked.
     * @param handler: regitered timeout handler
     */
    HeartbeatClientDriver(const std::string &name, uint32_t maxPingTimeoutNums, uint32_t pingCycle,
                          const HeartbeatClient::TimeOutHandler &handler)
    {
        actor_ = std::make_shared<HeartbeatClient>(name, maxPingTimeoutNums, pingCycle, handler);
        YRLOG_INFO("HeartbeatClient {} constructed", actor_->GetAID().HashString());
        (void)litebus::Spawn(actor_);
    }

    HeartbeatClientDriver(const std::string &name, const HeartbeatClient::TimeOutHandler &handler)
    {
        actor_ = std::make_shared<HeartbeatClient>(name, handler);
        YRLOG_INFO("HeartbeatClient {} constructed", actor_->GetAID().HashString());
        (void)litebus::Spawn(actor_);
    }

    virtual ~HeartbeatClientDriver()
    {
        YRLOG_INFO("HeartbeatClient {} destructed", actor_->GetAID().HashString());
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_->GetAID());
    }

    void Start(const litebus::AID &dst) const
    {
        litebus::Async(actor_->GetAID(), &HeartbeatClient::Start, dst);
    }

    void Stop() const
    {
        litebus::Async(actor_->GetAID(), &HeartbeatClient::Stop);
    }

    litebus::AID GetActorAID() const
    {
        return actor_->GetAID();
    }

    litebus::Future<Status> GetStatus()
    {
        return litebus::Async(actor_->GetAID(), &HeartbeatClient::GetStatus);
    }

private:
    std::shared_ptr<HeartbeatClient> actor_;
};
}  // namespace functionsystem
#endif
