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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H

#include <gmock/gmock.h>

#include "common/schedule_decision/scheduler.h"

namespace functionsystem::test {
class MockScheduler : public schedule_decision::Scheduler {
public:
    MockScheduler() : Scheduler()
    {
    }
    ~MockScheduler() = default;
    MOCK_METHOD(litebus::Future<schedule_decision::ScheduleResult>, ScheduleDecision,
                (const std::shared_ptr<messages::ScheduleRequest> &req),
                (override));

    MOCK_METHOD(litebus::Future<Status>, ScheduleConfirm,
                (const std::shared_ptr<messages::ScheduleResponse> &rsp, const resource_view::InstanceInfo &ins,
                 const schedule_decision::ScheduleResult &schedResult),
                (override));

    MOCK_METHOD(litebus::Future<Status>, RegisterPolicy, (const std::string &policyName), (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_H
