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

#ifndef MOCK_INSTANCE_MANAGER_H
#define MOCK_INSTANCE_MANAGER_H

#include <gmock/gmock.h>

#include "function_master/instance_manager/instance_manager.h"

namespace functionsystem::test {
class MockInstanceManager : public instance_manager::InstanceManager {
public:
    MockInstanceManager() : InstanceManager(nullptr){};
    ~MockInstanceManager() override = default;
    MOCK_METHOD((litebus::Future<std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>>>),
                GetInstanceInfoByInstanceID, (const std::string &), (override));
};
}  // namespace functionsystem::test

#endif  // MOCK_INSTANCE_MANAGER_H