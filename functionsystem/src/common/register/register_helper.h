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

#ifndef COMMON_REGISTER_REGISTER_HELPER_H
#define COMMON_REGISTER_REGISTER_HELPER_H

#include <memory>

#include "async/future.hpp"
#include "register_helper_actor.h"

namespace functionsystem {

class RegisterHelper {
public:
    explicit RegisterHelper(const std::string &name);
    virtual ~RegisterHelper();

    // Methods for downstream.
    void StartRegister(const std::string &name, const std::string &address, const std::string &msg,
                       uint32_t maxRegistersTimes);
    void SetRegisterInterval(const uint64_t interval);
    litebus::Future<bool> IsRegistered();
    void SetRegisteredCallback(const std::function<void(const std::string &)> &func);
    void SetRegisterTimeoutCallback(const std::function<void()> &func);
    void SetPingPongDriver(const std::string &dstName, const std::string &dstAddress, const uint32_t &timeoutMs,
                           const HeartbeatClient::TimeOutHandler &handler, const std::string &heartbeatName);

    // Methods for upstream.
    virtual void SendRegistered(const std::string &name, const std::string &address, const std::string &msg);
    void SetRegisterCallback(const std::function<void(const std::string &)> &func);
    void SetHeartbeatObserveDriver(const std::string &dstName, const std::string &dstAddress, const uint32_t &timeoutMs,
                                   const HeartbeatObserver::TimeOutHandler &handler, const std::string &heartbeatName);
    void StopPingPongDriver();
    void StopHeartbeatObserver();
    /* *
     * set component name
     * @return
     */
    void SetComponentName(const std::string &componentName);

private:
    std::shared_ptr<RegisterHelperActor> actor_;
};

}  // namespace functionsystem

#endif  // COMMON_REGISTER_REGISTER_HELPER_H
