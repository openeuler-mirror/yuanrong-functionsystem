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

#ifndef UT_MOCKS_MOCK_HEARTBEAT_H
#define UT_MOCKS_MOCK_HEARTBEAT_H

#include <gmock/gmock.h>

#include "heartbeat/heartbeat_observer_ctrl.h"

namespace functionsystem::test {
class MockHeartbeatObserverDriverCtrl : public HeartbeatObserverCtrl {
public:
    MockHeartbeatObserverDriverCtrl() : HeartbeatObserverCtrl()
    {
    }
    ~MockHeartbeatObserverDriverCtrl() = default;

    MOCK_METHOD(litebus::Future<Status>, Add,
                (const std::string &id, const std::string &address, const HeartbeatObserver::TimeOutHandler handler),
                (override));

    MOCK_METHOD(void, Delete, (const std::string &), (override));
};
}  // namespace functionsystem::test

#endif  // UT_MOCKS_MOCK_HEARTBEAT_H
