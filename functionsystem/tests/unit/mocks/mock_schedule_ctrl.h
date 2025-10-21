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

#ifndef UNIT_MOCKS_MOCK_SCHEDULE_CTRL_H
#define UNIT_MOCKS_MOCK_SCHEDULE_CTRL_H

#include <gmock/gmock.h>

#include "function_accessor/schedule/schedule_ctrl.h"

namespace functionsystem::test {
using namespace functionsystem::function_accessor;

class MockScheduleCtrl : public ScheduleCtrl {
public:
    MockScheduleCtrl(const std::shared_ptr<ScheduleActor> &scheduleActor) : ScheduleCtrl(scheduleActor)
    {
    }

    MOCK_METHOD(litebus::Future<CreateResponse>, Schedule, (const std::shared_ptr<CreateRequest> &createRequest),
                (override));
};
}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_SCHEDULE_CTRL_H
