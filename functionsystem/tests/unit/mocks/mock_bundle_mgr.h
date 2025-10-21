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

#ifndef TEST_UNIT_MOCK_BUNDLE_MGR_H
#define TEST_UNIT_MOCK_BUNDLE_MGR_H

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "function_proxy/local_scheduler/bundle_manager/bundle_mgr.h"
#include "function_proxy/local_scheduler/bundle_manager/bundle_mgr_actor.h"

namespace functionsystem::test {
using namespace local_scheduler;

class MockBundleMgr : public BundleMgr {
public:
    MockBundleMgr() : BundleMgr(nullptr)
    {
    }

    MOCK_METHOD(void, OnHealthyStatus, (const Status &status), (override));

    MOCK_METHOD(litebus::Future<Status>, SyncBundles, (const std::string &agentID), (override));

    MOCK_METHOD(litebus::Future<Status>, SyncFailedBundles,
                ((const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap)), (override));

    MOCK_METHOD(litebus::Future<Status>, NotifyFailedAgent, (const std::string &failedAgentID), (override));

    MOCK_METHOD(void, UpdateBundlesStatus, (const std::string &agentID, const resource_view::UnitStatus &status),
                (override));
};
}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCK_BUNDLE_MGR_H