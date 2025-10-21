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
#include "runtime_manager/metrics/collector/system_memory_collector.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class SystemMemoryCollectorTest : public ::testing::Test {};

/**
 * Feature: SystemMemoryCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * system-memory
 */
TEST_F(SystemMemoryCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::SystemMemoryCollector>();
    EXPECT_EQ(collector->GenFilter(), "system-Memory");
}

/**
 * Feature: SystemMemoryCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 */
TEST_F(SystemMemoryCollectorTest, GetLimit)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "1051648"
        }));

    auto collector = std::make_shared<runtime_manager::SystemMemoryCollector>(tools);
    auto limit = collector->GetLimit();
    EXPECT_EQ(limit.value, 1.0029296875);
    EXPECT_EQ(limit.instanceID.IsNone(), true);
}

/**
 * Feature: SystemMemoryCollector
 * Description: Get Limit
 * Steps: Give empty content
 * Expectation:
 * {}
 */
TEST_F(SystemMemoryCollectorTest, GetLimitWithEmptyContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{}));

    auto collector = std::make_shared<runtime_manager::SystemMemoryCollector>(tools);
    auto limit = collector->GetLimit();
    EXPECT_EQ(limit.value.IsNone(), true);
    EXPECT_EQ(limit.instanceID.IsNone(), true);
}

/**
 * Feature: SystemMemoryCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 */
TEST_F(SystemMemoryCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{
            "1051648"
        }));

    auto collector = std::make_shared<runtime_manager::SystemMemoryCollector>(tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value, 1.0029296875);
    EXPECT_EQ(usage.instanceID.IsNone(), true);
}

/**
 * Feature: SystemMemoryCollector
 * Description: Get Usage
 * Steps: Give empty content
 * Expectation:
 */
TEST_F(SystemMemoryCollectorTest, GetUsageWithEmptyContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(testing::Return(litebus::Option<std::string>{}));

    auto collector = std::make_shared<runtime_manager::SystemMemoryCollector>(tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.IsNone(), true);
}

}