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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_INSTANCE_CTRL_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_INSTANCE_CTRL_H

#include <gmock/gmock.h>

#include "domain_scheduler/instance_control/instance_ctrl.h"

namespace functionsystem::test {
class MockDomainInstanceCtrl : public domain_scheduler::InstanceCtrl {
public:
    MockDomainInstanceCtrl() : InstanceCtrl(litebus::AID())
    {
    }
    MOCK_METHOD(litebus::Future<std::shared_ptr<messages::ScheduleResponse>>, Schedule,
                (const std::shared_ptr<messages::ScheduleRequest> &req), (override));

    MOCK_METHOD(void, UpdateMaxSchedRetryTimes, (const uint32_t &retrys), (override));
    MOCK_METHOD(litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>>, GetSchedulerQueue, (),
                (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DOMAIN_INSTANCE_CTRL_H
