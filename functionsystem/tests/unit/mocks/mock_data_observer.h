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

#ifndef TEST_UNIT_MOCKS_MOCK_DATA_OBSERVER_H
#define TEST_UNIT_MOCKS_MOCK_DATA_OBSERVER_H

#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"
#include "gmock/gmock.h"

namespace functionsystem::test {

class MockDataObserver : public function_proxy::DataPlaneObserver {
public:
    MockDataObserver() : function_proxy::DataPlaneObserver(nullptr)
    {
    }
    ~MockDataObserver() override
    {
    }

    MOCK_METHOD(litebus::Future<Status>, SubscribeInstanceEvent,
                (const std::string &subscriber, const std::string &targetInstance, bool ignoreNonExist), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DATA_OBSERVER_H
