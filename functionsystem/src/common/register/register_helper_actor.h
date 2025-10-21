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

#ifndef COMMON_REGISTER_REGISTER_HELPER_ACTOR_H
#define COMMON_REGISTER_REGISTER_HELPER_ACTOR_H

#include "actor/actor.hpp"
#include "heartbeat/heartbeat_observer.h"
#include "heartbeat/ping_pong_driver.h"
#include "timer/timer.hpp"

namespace functionsystem {

class RegisterHelperActor : public litebus::ActorBase {
public:
    explicit RegisterHelperActor(const std::string &name);
    ~RegisterHelperActor() override;
    void StartRegister(const std::string &name, const std::string &address, const std::string &msg,
                       int32_t maxRegistersTimes);
    void Register(const litebus::AID &from, std::string &&name, std::string &&msg);
    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg);
    void SetRegisterInterval(const uint64_t interval);
    void SetRegisterCallback(const std::function<void(const std::string &)> &func);
    void SetRegisteredCallback(const std::function<void(const std::string &)> &func);
    void SetRegisterTimeoutCallback(const std::function<void()> &func);
    void SendRegistered(const std::string &name, const std::string &address, const std::string &msg);
    bool IsRegistered();
    void SetPingPongDriver(const uint32_t &timeoutMs, const PingPongActor::TimeOutHandler &handler);
    void SetHeartbeatObserveDriver(const std::string &dstName, const std::string &dstAddress, const uint32_t &timeoutMs,
                                   const HeartbeatObserver::TimeOutHandler &handler);

    void StopHeartbeatObserver();
    void StopPingPongDriver();

protected:
    void Init() override;

private:
    void RetryRegister(const std::string &name, const std::string &address, const std::string &msg,
                       int32_t retryTimes);
    void WaitFirstPing(const std::string &name, const std::string &address);

    std::string name_;
    uint64_t registerInterval_;
    bool receiveRegistered_;
    litebus::Timer registerTimer_;
    litebus::AID registeredFrom_;
    std::function<void(const std::string &)> registerCb_;
    std::function<void(const std::string &)> registeredCb_;
    std::function<void()> registerTimeoutCb_;
    std::shared_ptr<HeartbeatObserveDriver> heartbeatObserver_;
    std::shared_ptr<PingPongDriver> pingPongDriver_;
};

}  // namespace functionsystem

#endif  // COMMON_REGISTER_REGISTER_HELPER_ACTOR_H
