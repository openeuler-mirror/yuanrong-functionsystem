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

namespace functionsystem {
const std::string PINGPONG_BASENAME = "-PingPong";
const uint32_t DEFAULT_PING_PONG_TIMEOUT = 10000;  // ms

enum HeartbeatConnection : int {
    LOST = 0,
    EXITED = 1,
};

class PingPongActor : public litebus::ActorBase {
public:
    using TimeOutHandler = std::function<void(const litebus::AID &, HeartbeatConnection)>;
    /**
     * PingPongActor constructor
     * @param name: actor name which will be appended with -PingPong
     * @param timeoutMs: millisecond. while received a new ping from some other actor,
     * if not receviced subsequent ping within timeout, TimeOutHandler will be invoked.
     * @param handler: regitered timeout handler
     */
    PingPongActor(const std::string &name, const uint32_t &timeoutMs, const TimeOutHandler &handler);

    ~PingPongActor() override = default;

    virtual void Ping(const litebus::AID &from, std::string &&name, std::string &&msg);

    // Triggers a re-detection of the observer when we does
    // not receive a ping.
    void PingTimeout(const litebus::AID &from);

    void CheckFirstPing(const litebus::AID &aid);

protected:
    void Init() override;

private:
    TimeOutHandler handler_;
    uint32_t timeoutMs_;
    std::unordered_map<std::string, litebus::Timer> pingTimers_;
};

class PingPongDriver {
public:
    /**
     * PingPongDriver constructor, spawn PingPongActor
     * @param name: actor name which will be appended with -PingPong
     * @param timeoutMs: millisecond. while received a new ping from some other actor,
     * if not receviced subsequent ping within timeout, TimeOutHandler will be invoked.
     * @param handler: regitered timeout handler
     */
    PingPongDriver(const std::string &name, const uint32_t &timeoutMs, const PingPongActor::TimeOutHandler &handler)
    {
        actor_ = std::make_shared<PingPongActor>(name, timeoutMs, handler);
        (void)litebus::Spawn(actor_);
    }

    PingPongDriver(const std::string &name, const PingPongActor::TimeOutHandler &handler)
        : PingPongDriver(name, DEFAULT_PING_PONG_TIMEOUT, handler)
    {
    }

    virtual ~PingPongDriver()
    {
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_->GetAID());
    }

    litebus::AID GetActorAID() const
    {
        return actor_->GetAID();
    }

    void CheckFirstPing(const litebus::AID &aid) const
    {
        litebus::Async(actor_->GetAID(), &PingPongActor::CheckFirstPing, aid);
    }

private:
    std::shared_ptr<PingPongActor> actor_;
};
}  // namespace functionsystem
#endif
