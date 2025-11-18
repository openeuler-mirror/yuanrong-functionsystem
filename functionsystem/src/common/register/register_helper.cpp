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

#include "register_helper.h"

#include "async/async.hpp"
#include "common/status/status.h"
#include "litebus.hpp"

namespace functionsystem {

RegisterHelper::RegisterHelper(const std::string &name)
{
    actor_ = std::make_shared<RegisterHelperActor>(name);
    litebus::Spawn(actor_);
}

RegisterHelper::~RegisterHelper()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

void RegisterHelper::StartRegister(const std::string &name, const std::string &address, const std::string &msg,
                                   uint32_t maxRegistersTimes)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::StartRegister, name, address, msg, maxRegistersTimes);
}

void RegisterHelper::SetRegisterInterval(const uint64_t interval)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetRegisterInterval, interval);
}

void RegisterHelper::SetRegisterCallback(const std::function<void(const std::string &)> &func)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetRegisterCallback, func);
}

void RegisterHelper::SetRegisteredCallback(const std::function<void(const std::string &)> &func)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetRegisteredCallback, func);
}

void RegisterHelper::SetRegisterTimeoutCallback(const std::function<void()> &func)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetRegisterTimeoutCallback, func);
}

void RegisterHelper::SendRegistered(const std::string &name, const std::string &address, const std::string &msg)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::SendRegistered, name, address, msg);
}

litebus::Future<bool> RegisterHelper::IsRegistered()
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &RegisterHelperActor::IsRegistered);
}

void RegisterHelper::SetPingPongDriver(const std::string &dstName, const std::string &dstAddress,
                                       const uint32_t &timeoutMs, const HeartbeatClient::TimeOutHandler &handler,
                                       const std::string &heartbeatName)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetPingPongDriver, dstName, dstAddress, timeoutMs,
                          handler, heartbeatName);
}

void RegisterHelper::SetHeartbeatObserveDriver(const std::string &dstName, const std::string &dstAddress,
                                               const uint32_t &timeoutMs,
                                               const HeartbeatObserver::TimeOutHandler &handler,
                                               const std::string &heartbeatName)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &RegisterHelperActor::SetHeartbeatObserveDriver, dstName, dstAddress,
                          timeoutMs, handler, heartbeatName);
}

void RegisterHelper::StopPingPongDriver()
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::StopPingPongDriver);
}

void RegisterHelper::StopHeartbeatObserver()
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &RegisterHelperActor::StopHeartbeatObserver);
}

void RegisterHelper::SetComponentName(const std::string &componentName)
{
    ASSERT_IF_NULL(actor_);
    actor_->SetComponentName(componentName);
}

}  // namespace functionsystem