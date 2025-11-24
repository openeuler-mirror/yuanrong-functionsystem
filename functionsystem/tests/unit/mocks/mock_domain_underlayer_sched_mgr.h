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

#ifndef TEST_UNIT_MOCKS_MOCK_DOMAIN_UNDERLAYER_SCHED_MGR_H
#define TEST_UNIT_MOCKS_MOCK_DOMAIN_UNDERLAYER_SCHED_MGR_H

#include <gmock/gmock.h>

#include "domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr.h"

namespace functionsystem::test {
class MockDomainUnderlayerSchedMgr : public domain_scheduler::UnderlayerSchedMgr {
public:
    MockDomainUnderlayerSchedMgr() : UnderlayerSchedMgr(litebus::AID())
    {
    }
    MOCK_METHOD(litebus::Future<std::shared_ptr<messages::ScheduleResponse>>, DispatchSchedule,
                (const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req), (override));
    MOCK_METHOD(void, UpdateUnderlayerTopo, (const messages::ScheduleTopology &req), (override));
    MOCK_METHOD(litebus::Future<bool>, IsRegistered, (const std::string &name), (override));
    MOCK_METHOD(litebus::Future<std::shared_ptr<messages::OnReserves>>, Reserves,
                (const std::string &selectedName, const std::shared_ptr<messages::Reserves> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, UnReserve,
                (const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, Bind,
                (const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, UnBind,
                (const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req), (override));
};
} // namespace functionsystem::test

#endif // TEST_UNIT_MOCKS_MOCK_DOMAIN_UNDERLAYER_SCHED_MGR_H
