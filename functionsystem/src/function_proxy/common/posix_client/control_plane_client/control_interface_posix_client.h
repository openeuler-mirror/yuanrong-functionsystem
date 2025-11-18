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

#ifndef FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_INTERFACE_POSIX_CLIENT_H
#define FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_INTERFACE_POSIX_CLIENT_H

#include "common/constants/constants.h"
#include "common/status/status.h"
#include "function_proxy/common/posix_client/base_client.h"

namespace functionsystem {
class ControlInterfacePosixClient : virtual public BaseClient {
public:
    explicit ControlInterfacePosixClient(const std::shared_ptr<grpc::PosixClient> &posix) : BaseClient(posix)
    {
    }
    ~ControlInterfacePosixClient() override = default;

    virtual litebus::Future<Status> Heartbeat(uint64_t timeMs);
    virtual litebus::Future<Status> Readiness();
    virtual litebus::Future<runtime::ShutdownResponse> Shutdown(runtime::ShutdownRequest &&request);
    virtual litebus::Future<runtime::SignalResponse> Signal(runtime::SignalRequest &&request);
    virtual litebus::Future<runtime::CheckpointResponse> Checkpoint(runtime::CheckpointRequest &&request);
    virtual litebus::Future<runtime::RecoverResponse> Recover(runtime::RecoverRequest &&request,
                                                              uint64_t timeoutMs = DEFAULT_RECOVER_TIMEOUT_MS);
};
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_POSIX_CLIENT_CONTROL_INTERFACE_POSIX_CLIENT_H
