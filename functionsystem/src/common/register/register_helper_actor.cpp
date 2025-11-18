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

#include "register_helper_actor.h"

#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "utils/os_utils.hpp"

namespace functionsystem {

static const std::string REGISTER_HELPER_SUFFIX = "-RegisterHelper";
static const std::string REGISTER_HELPER_ACTOR_NAME = "RegisterHelper";
static constexpr uint64_t DEFAULT_REGISTER_TIMEOUT = 1000;
static const uint32_t DEFAULT_MAX_PING_TIMES = 12;

void RegisterHelperActor::Init()
{
    Receive("Register", &RegisterHelperActor::Register);
    Receive("Registered", &RegisterHelperActor::Registered);
}

RegisterHelperActor::RegisterHelperActor(const std::string &name)
    : ActorBase(name + REGISTER_HELPER_SUFFIX), name_(name), registerInterval_(DEFAULT_REGISTER_TIMEOUT),
      receiveRegistered_(false)
{}

RegisterHelperActor::~RegisterHelperActor()
{
    if (pingPongDriver_ != nullptr) {
        (void)pingPongDriver_->Stop();
        pingPongDriver_ = nullptr;
    }
    heartbeatObserveDriver_ = nullptr;
}

void RegisterHelperActor::StartRegister(
    const std::string &name, const std::string &address, const std::string &msg, uint32_t maxRegistersTimes)
{
    YRLOG_DEBUG("send message(Register) to {}@{}", name + REGISTER_HELPER_SUFFIX, address);
    receiveRegistered_ = false;
    std::string registerMsg(msg);
    Send(litebus::AID(name + REGISTER_HELPER_SUFFIX, address), "Register", std::move(registerMsg));
    registerTimer_ = litebus::AsyncAfter(
        registerInterval_, GetAID(), &RegisterHelperActor::RetryRegister, name, address, msg, maxRegistersTimes - 1);
}

void RegisterHelperActor::RetryRegister(
    const std::string &name, const std::string &address, const std::string &msg, uint32_t retryTimes)
{
    if (receiveRegistered_) {
        YRLOG_INFO("registration already succeeded");
        return;
    }

    if (retryTimes <= 0) {
        YRLOG_ERROR("not receive Registered message, register timeout, exceed max retry times");
        if (registerTimeoutCb_ != nullptr) {
            registerTimeoutCb_();
        } else {
            YRLOG_WARN("register timeout callback function is null");
        }
        return;
    }

    YRLOG_ERROR("not receive Registered message, register timeout, retry times({})", retryTimes);
    std::string registerMsg(msg);
    Send(litebus::AID(name + REGISTER_HELPER_SUFFIX, address), "Register", std::move(registerMsg));
    registerTimer_ = litebus::AsyncAfter(
        registerInterval_, GetAID(), &RegisterHelperActor::RetryRegister, name, address, msg, retryTimes - 1);
}

void RegisterHelperActor::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_DEBUG("receive message({}) from {}", name, from.HashString());
    if (registerCb_ != nullptr) {
        registerCb_(msg);
    } else {
        YRLOG_WARN("register callback function is null");
    }
}

void RegisterHelperActor::Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_DEBUG("receive message({}) from {}", name, from.HashString());
    registeredFrom_ = from;
    if (receiveRegistered_) {
        YRLOG_DEBUG("already registered");
        return;
    }
    receiveRegistered_ = true;
    (void)litebus::TimerTools::Cancel(registerTimer_);
    if (registeredCb_ != nullptr) {
        registeredCb_(msg);
    } else {
        YRLOG_WARN("registered callback function is null");
    }
}

void RegisterHelperActor::SetRegisterInterval(const uint64_t interval)
{
    registerInterval_ = interval;
}

void RegisterHelperActor::SetRegisterCallback(const std::function<void(const std::string &)> &func)
{
    registerCb_ = func;
}

void RegisterHelperActor::SetRegisteredCallback(const std::function<void(const std::string &)> &func)
{
    registeredCb_ = func;
}

void RegisterHelperActor::SetRegisterTimeoutCallback(const std::function<void()> &func)
{
    registerTimeoutCb_ = func;
}

void RegisterHelperActor::SendRegistered(const std::string &name, const std::string &address, const std::string &msg)
{
    YRLOG_DEBUG("send message(Registered) to {}@{}", name + REGISTER_HELPER_SUFFIX, address);
    std::string registeredMsg(msg);
    Send(litebus::AID(name + REGISTER_HELPER_SUFFIX, address), "Registered", std::move(registeredMsg));
}

bool RegisterHelperActor::IsRegistered()
{
    return receiveRegistered_;
}

void RegisterHelperActor::SetPingPongDriver(const std::string &dstName, const std::string &dstAddress,
                                            const uint32_t &timeoutMs, const HeartbeatClient::TimeOutHandler &handler,
                                            const std::string &heartbeatName)
{
    YRLOG_DEBUG("set ping pong driver, name: {}, address: {}, timeoutMs : {}, GetAID().Name(): {}", dstName, dstAddress,
                timeoutMs, GetAID().Name());
    if (pingPongDriver_ == nullptr) {
        pingPongDriver_ = std::make_shared<HeartbeatClientDriver>(
            litebus::os::Join(componentName_, dstName, '-') + REGISTER_HELPER_SUFFIX, DEFAULT_MAX_PING_TIMES,
            timeoutMs / DEFAULT_MAX_PING_TIMES, handler);
    }
    litebus::AID heartBeatAID(HEARTBEAT_OBSERVER_BASENAME + heartbeatName, dstAddress);
    pingPongDriver_->Start(heartBeatAID);
}

void RegisterHelperActor::SetHeartbeatObserveDriver(const std::string &dstName, const std::string &dstAddress,
    const uint32_t &timeoutMs, const HeartbeatObserver::TimeOutHandler &handler, const std::string &heartbeatName)
{
    litebus::AID pingPongAID;
    pingPongAID.SetName(HEARTBEAT_CLIENT_BASENAME + dstName + REGISTER_HELPER_SUFFIX);
    pingPongAID.SetUrl(dstAddress);
    pingPongAID.SetProtocol(litebus::BUS_TCP);
    YRLOG_DEBUG("wait first ping from {}", pingPongAID.HashString());
    if (heartbeatObserveDriver_ == nullptr) {
        heartbeatObserveDriver_ = std::make_shared<HeartbeatObserveDriver>(heartbeatName);
    }

    heartbeatObserveDriver_->AddNewHeartbeatNode(pingPongAID, timeoutMs, handler);
}

void RegisterHelperActor::StopHeartbeatObserver()
{
    if (pingPongDriver_ != nullptr) {
        litebus::AID pingPongAID = pingPongDriver_->GetActorAID();
        heartbeatObserveDriver_->RemoveHeartbeatNode(pingPongAID);
    }
}

void RegisterHelperActor::StopPingPongDriver()
{
    if (pingPongDriver_ != nullptr) {
        (void)pingPongDriver_->Stop();
        pingPongDriver_ = nullptr;
    }
}
void RegisterHelperActor::SetComponentName(const std::string &componentName)
{
    componentName_ = componentName;
}

}  // namespace functionsystem