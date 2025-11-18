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

#ifndef TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_H
#define TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_H

#include <gmock/gmock.h>
#include "function_master/global_scheduler/global_sched.h"
#include "utils/port_helper.h"

namespace functionsystem::test {

class MockGlobalSched : public global_scheduler::GlobalSched {
public:
    MockGlobalSched() : global_scheduler::GlobalSched()
    {
    }
    MOCK_METHOD(Status, Start, (std::shared_ptr<global_scheduler::GlobalSchedActor> globalSchedActor), (override));
    MOCK_METHOD(Status, Stop, (), (const, override));
    MOCK_METHOD(void, InitManager,
                (std::unique_ptr<global_scheduler::DomainSchedMgr> domainSchedMgr,
                 std::unique_ptr<global_scheduler::LocalSchedMgr> localSchedMgr),
                (override));
    MOCK_METHOD(litebus::Future<messages::QueryAgentInfoResponse>, QueryAgentInfo,
                (const std::shared_ptr<messages::QueryAgentInfoRequest> &req), (override));

    MOCK_METHOD(litebus::Future<messages::QueryInstancesInfoResponse>, GetSchedulingQueue,
                (const std::shared_ptr<messages::QueryInstancesInfoRequest> &req), (override));

    MOCK_METHOD(litebus::Future<Status>, EvictAgent,
                (const std::string &localID, const std::shared_ptr<messages::EvictAgentRequest> &req), (override));
    MOCK_METHOD(litebus::Future<messages::QueryResourcesInfoResponse>, QueryResourcesInfo,
                (const std::shared_ptr<messages::QueryResourcesInfoRequest> &req), (override));

    MOCK_METHOD(litebus::Future<Status>, Schedule, (const std::shared_ptr<messages::ScheduleRequest> &), (override));
    MOCK_METHOD(litebus::Future<messages::GroupResponse>, GroupSchedule,
                (const std::shared_ptr<messages::GroupInfo> &, uint32_t), (override));
    MOCK_METHOD(void, LocalSchedAbnormalCallback, (const global_scheduler::LocalSchedAbnormalCallbackFunc &),
                (override));
    MOCK_METHOD(void, BindCheckLocalAbnormalCallback, (const global_scheduler::CheckLocalAbnormalCallbackFunc &),
                (override));
    MOCK_METHOD(void, AddLocalSchedAbnormalNotifyCallback,
                (const std::string &, const global_scheduler::LocalSchedAbnormalCallbackFunc &), (override));
    MOCK_METHOD((litebus::Future<litebus::Option<std::string>>), GetLocalAddress, (const std::string &), (override));
    MOCK_METHOD((litebus::Future<litebus::Option<NodeInfo>>), GetRootDomainInfo, (), (override));
    MOCK_METHOD((litebus::Future<std::unordered_set<std::string>>), QueryNodes, (), (override));
    MOCK_METHOD(void, BindLocalDeleteCallback, (const global_scheduler::LocalDeleteCallbackFunc &), (override));
    MOCK_METHOD(void, BindLocalAddCallback, (const global_scheduler::LocalAddCallbackFunc &), (override));

    void ReturnDefaultLocalAddress()
    {
        uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
        ON_CALL(*this, GetLocalAddress)
            .WillByDefault(testing::Return(litebus::Option<std::string>("127.0.0.1:" + std::to_string(port))));
    }
};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_GLOBAL_SCHED_H
