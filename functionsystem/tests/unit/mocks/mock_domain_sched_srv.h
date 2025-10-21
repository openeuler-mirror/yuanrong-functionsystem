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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_SRV_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_SRV_H

#include <gmock/gmock.h>

#include "domain_scheduler/domain_scheduler_service/domain_sched_srv.h"

namespace functionsystem::test {
class MockDomainSchedSrv : public domain_scheduler::DomainSchedSrv {
public:
    MockDomainSchedSrv() : DomainSchedSrv(litebus::AID())
    {
    }
    MOCK_METHOD(litebus::Future<Status>, NotifySchedAbnormal, (const messages::NotifySchedAbnormalRequest &req),
                (override));
    MOCK_METHOD(litebus::Future<Status>, NotifyWorkerStatus, (const messages::NotifyWorkerStatusRequest &req),
                (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<messages::ScheduleResponse>>, ForwardSchedule,
                (const std::shared_ptr<messages::ScheduleRequest> &req), (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_DOMAIN_SCHED_SRV_H
