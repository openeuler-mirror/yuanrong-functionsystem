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

#include <gtest/gtest.h>
#include <gmock/gmock-actions.h>
#include <gmock/gmock.h>
#include "runtime_manager/metrics/collector/system_cpu_collector.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class SystemCPUCollectorTest : public ::testing::Test {};

/**
 * Feature: SystemCPUCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * system-cpu
 */
TEST_F(SystemCPUCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::SystemCPUCollector>();
    EXPECT_EQ(collector->GenFilter(), "system-CPU");
}

/**
 * Feature: SystemCPUCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 */
TEST_F(SystemCPUCollectorTest, GetLimit)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "100000"
        }))
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "100"
        }));

    auto collector = std::make_shared<runtime_manager::SystemCPUCollector>(tools);
    auto limit = collector->GetLimit();
    EXPECT_EQ(limit.value, 1.0);
    EXPECT_EQ(limit.instanceID.IsNone(), true);
}

/**
 * Feature: SystemCPUCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 */
TEST_F(SystemCPUCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "100"
        }))
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "700"
        }));

    auto collector = std::make_shared<runtime_manager::SystemCPUCollector>(tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value, 6000.0);
    EXPECT_EQ(usage.instanceID.IsNone(), true);
}

/**
 * Feature: SystemCPUCollector
 * Description: Get Usage
 * Steps: Set Empty Content
 * Expectation:
 * {}
 */
TEST_F(SystemCPUCollectorTest, GetUsageWithEmptyContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{}));

    auto collector = std::make_shared<runtime_manager::SystemCPUCollector>(tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.IsNone(), true);
}

}