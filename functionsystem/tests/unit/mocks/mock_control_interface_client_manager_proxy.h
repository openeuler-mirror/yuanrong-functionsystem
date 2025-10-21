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

#ifndef TEST_UNIT_MOCKS_MOCK_SHARED_CLIENT_MANAGER_PROXY_H
#define TEST_UNIT_MOCKS_MOCK_SHARED_CLIENT_MANAGER_PROXY_H

#include "common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "gmock/gmock.h"

namespace functionsystem::test {

class MockControlInterfaceClientManagerProxy : public ControlInterfaceClientManagerProxy {
public:
    MockControlInterfaceClientManagerProxy() : ControlInterfaceClientManagerProxy(litebus::AID())
    {
    }
    ~MockControlInterfaceClientManagerProxy() = default;

    MOCK_METHOD(litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>, GetControlInterfacePosixClient,
                (const std::string &instanceID), (override));

    MOCK_METHOD(litebus::Future<std::shared_ptr<ControlInterfacePosixClient>>, NewControlInterfacePosixClient,
                (const std::string &instanceID, const std::string &runtimeID, const std::string &address,
                 std::function<void()> closeCb, int64_t timeoutSec, int32_t maxGrpcSize),
                (override));

    MOCK_METHOD(litebus::Future<Status>, DeleteClient, (const std::string &instanceID), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_SHARED_CLIENT_MANAGER_PROXY_H
