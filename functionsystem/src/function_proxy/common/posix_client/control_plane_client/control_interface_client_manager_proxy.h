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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_PLANE_CLIENT_CONTROL_INTERFACE_CLIENT_MANAGER_PROXY_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_PLANE_CLIENT_CONTROL_INTERFACE_CLIENT_MANAGER_PROXY_H

#include "async/future.hpp"
#include "common/rpc/stream/posix/control_client.h"
#include "control_interface_posix_client.h"
#include "litebus.hpp"
namespace functionsystem {
class ControlInterfaceClientManagerProxy {
public:
    explicit ControlInterfaceClientManagerProxy(const litebus::AID &aid)
        : aid_(aid), posixControlWrapper_(std::make_shared<grpc::PosixControlWrapper>())
    {
    }
    virtual ~ControlInterfaceClientManagerProxy() = default;

    virtual litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> GetControlInterfacePosixClient(
        const std::string &instanceID);

    virtual litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> NewControlInterfacePosixClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address,
        std::function<void()> closedCb, int64_t timeoutSec = 30, int32_t maxGrpcSize = grpc::DEFAULT_MAX_GRPC_SIZE);

    virtual litebus::Future<Status> DeleteClient(const std::string &instanceID);

    [[maybe_unused]] void BindPosixWrapper(std::shared_ptr<grpc::PosixControlWrapper> posixWrapper)
    {
        posixControlWrapper_ = std::move(posixWrapper);
    }

protected:
    litebus::AID aid_;
    std::shared_ptr<grpc::PosixControlWrapper> posixControlWrapper_;
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_PLANE_CLIENT_CONTROL_INTERFACE_CLIENT_MANAGER_PROXY_H
