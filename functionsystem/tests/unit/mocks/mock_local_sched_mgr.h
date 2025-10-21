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

#ifndef TEST_UNIT_MOCKS_MOCK_LOCAL_SCHED_MGR_H
#define TEST_UNIT_MOCKS_MOCK_LOCAL_SCHED_MGR_H

#include "scheduler_manager/local_sched_mgr.h"

namespace functionsystem::test {

class MockLocalSchedMgr : public global_scheduler::LocalSchedMgr {
public:
    MockLocalSchedMgr(const std::string &name = "LocalSchedMgrActor")
        : global_scheduler::LocalSchedMgr(std::make_shared<global_scheduler::LocalSchedMgrActor>(name))
    {
    }
    MOCK_METHOD(void, Start, (), (override));
    MOCK_METHOD(void, Stop, (), (override));
    MOCK_METHOD(void, Registered,
                (const litebus::AID &dst, const litebus::Option<messages::ScheduleTopology> &topology),
                (const, override));
    MOCK_METHOD(Status, AddLocalSchedCallback, (const global_scheduler::CallbackAddFunc &func), (const, override));
    MOCK_METHOD(litebus::Future<Status>, DelLocalSchedCallback, (const global_scheduler::CallbackDelFunc &func),
                (const, override));
    MOCK_METHOD(litebus::Future<Status>, EvictAgentOnLocal,
                (const std::string &address, const std::shared_ptr<messages::EvictAgentRequest> &req), (override));
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_LOCAL_SCHED_MGR_H
