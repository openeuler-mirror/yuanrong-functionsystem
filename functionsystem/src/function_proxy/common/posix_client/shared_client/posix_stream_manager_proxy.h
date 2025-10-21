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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_SHARED_CLIENT_POSIX_STREAM_MANAGER_PROXY_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_SHARED_CLIENT_POSIX_STREAM_MANAGER_PROXY_H

#include "function_proxy/common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "function_proxy/common/posix_client/data_plane_client/data_interface_client_manager_proxy.h"
#include "function_proxy/common/posix_service/posix_service.h"

namespace functionsystem {

class PosixStreamManagerProxy : public ControlInterfaceClientManagerProxy, public DataInterfaceClientManagerProxy {
public:
    explicit PosixStreamManagerProxy(const litebus::AID &aid)
        : ControlInterfaceClientManagerProxy(aid),
          DataInterfaceClientManagerProxy(aid),
          aid_(aid) {}
    ~PosixStreamManagerProxy() override = default;

    litebus::Future<std::shared_ptr<DataInterfacePosixClient>> NewDataInterfacePosixClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address) override;

    litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> NewControlInterfacePosixClient(
        const std::string &instanceID, const std::string &runtimeID, const std::string &address,
        std::function<void()> closedCb, int64_t timeoutSec = 30,
        int32_t maxGrpcSize = grpc::DEFAULT_MAX_GRPC_SIZE) override;

    void UpdateControlInterfacePosixClient(const std::string &instanceID, const std::string &runtimeID,
                                                   const std::shared_ptr<grpc::PosixClient> &posix);

private:
    litebus::AID aid_;
};

} // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_SHARED_CLIENT_POSIX_STREAM_MANAGER_PROXY_H
