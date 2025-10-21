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

#ifndef UNIT_MOCKS_MOCK_LOCAL_GROUP_CTRL_H
#define UNIT_MOCKS_MOCK_LOCAL_GROUP_CTRL_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "local_scheduler/local_group_ctrl/local_group_ctrl.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
class MockGroupCtrl : public LocalGroupCtrl {
public:
    MockGroupCtrl() : LocalGroupCtrl(nullptr)
    {
    }
    ~MockGroupCtrl() override = default;

    MOCK_METHOD(litebus::Future<std::shared_ptr<CreateResponses>>, GroupSchedule,
                (const std::string &from, const std::shared_ptr<CreateRequests> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, Sync, (), (override));
    MOCK_METHOD(litebus::Future<Status>, Recover, (), (override));
};
}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_LOCAL_GROUP_CTRL_H
