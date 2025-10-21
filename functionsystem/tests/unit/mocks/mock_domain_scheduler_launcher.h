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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_LAUNCHER_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_LAUNCHER_H

#include <gmock/gmock.h>

#include "domain_scheduler/include/domain_scheduler_launcher.h"

namespace functionsystem::test {

class MockDomainSchedulerLauncher : public domain_scheduler::DomainSchedulerLauncher {
public:
    explicit MockDomainSchedulerLauncher()
        : domain_scheduler::DomainSchedulerLauncher(
              domain_scheduler::DomainSchedulerParam{ "identity", "globalAddress" })
    {
    }
    MOCK_METHOD(Status, Start, (), (override));
    MOCK_METHOD(Status, Stop, (), (override));
    MOCK_METHOD(void, Await, (), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHEDULER_LAUNCHER_H
