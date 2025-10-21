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

#ifndef UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_H
#define UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "local_scheduler/local_scheduler_service/local_sched_srv.h"

namespace functionsystem::test {
using namespace functionsystem::local_scheduler;
class MockLocalSchedSrv : public LocalSchedSrv {
public:
    explicit MockLocalSchedSrv()
        : LocalSchedSrv(std::make_shared<LocalSchedSrvActor>(LocalSchedSrvActor::Param{
              .nodeID = "nodeA",
              .globalSchedAddress = "127.0.0.1",
              .isK8sEnabled = false,
              .registerCycleMs = 100,
              .pingTimeOutMs = 1000,
          }))
    {
    }
    ~MockLocalSchedSrv() override = default;

    MOCK_METHOD(litebus::Future<messages::ScheduleResponse>, ForwardSchedule, (const std::shared_ptr<messages::ScheduleRequest> &req),
                (const, override));
    MOCK_METHOD(void, NotifyEvictResult, (const std::shared_ptr<messages::EvictAgentResult> &req),
                (override));
    MOCK_METHOD(void, DeletePod, (const std::string &agentID, const std::string &reqID, const std::string &msg),
                (override));
    MOCK_METHOD(litebus::Future<messages::GroupResponse>, ForwardGroupSchedule,
                (const std::shared_ptr<messages::GroupInfo> &groupInfo), (override));
    MOCK_METHOD(litebus::Future<Status>, KillGroup,
                (const std::shared_ptr<messages::KillGroup> &killReq), (override));
    MOCK_METHOD(litebus::Future<messages::ForwardKillResponse>, ForwardKillToInstanceManager,
                (const std::shared_ptr<messages::ForwardKillRequest> &req), (override));
    MOCK_METHOD(litebus::Future<Status>, GracefulShutdown, (), (override));
    MOCK_METHOD(litebus::Future<Status>, IsRegisteredToGlobal, (), (override));
    MOCK_METHOD(litebus::Future<std::string>, QueryMasterIP, (), (override));
};
}  // namespace functionsystem::test

#endif  // UNIT_MOCKS_MOCK_LOCAL_SCHED_SRV_H
