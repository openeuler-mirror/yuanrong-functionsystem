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
#include "runtime_manager/metrics/collector/instance_memory_collector.h"
#include "common/utils/proc_fs_tools.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class InstanceMemoryCollectorTest : public ::testing::Test {};

/**
 * Feature: InstanceMemoryCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * functionUrn-InstanceId-memory
 */
TEST_F(InstanceMemoryCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::InstanceMemoryCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GenFilter(), "urn-id-Memory");
}

/**
 * Feature: InstanceMemoryCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 * Get constructor parameter limit
 */
TEST_F(InstanceMemoryCollectorTest, GetLimit)
{
    auto collector = std::make_shared<runtime_manager::InstanceMemoryCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GetLimit().value, 1000.0);
    EXPECT_EQ(collector->GetLimit().instanceID, "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 * Get constructor parameter usage
 */
TEST_F(InstanceMemoryCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(::testing::Return(litebus::Option<std::string>{
            "Name:	init\n"
            "VmRSS:	     676 kB\n"
            "RssAnon:	     152 kB\n"
            "RssFile:	     524 kB\n"
            "VmData:	     712 kB\n"
            "VmStk:	     132 kB\n"
            "VmExe:	     444 kB\n"
        }));

    auto collector = std::make_shared<runtime_manager::InstanceMemoryCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.Get(), 0.66015625);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: read empty content
 * Steps:
 * Expectation:
 * {}
 */
TEST_F(InstanceMemoryCollectorTest, GetUsageWithEmptyContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(::testing::Return(litebus::Option<std::string>{}));

    auto collector = std::make_shared<runtime_manager::InstanceMemoryCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: read invalid content
 * Steps:
 * Expectation:
 * {}
 */
TEST_F(InstanceMemoryCollectorTest, GetUsageWithInvalidContent)
{
    auto tools = std::make_shared<MockProcFSTools>();
    EXPECT_CALL(*tools.get(), Read)
        .WillOnce(::testing::Return(litebus::Option<std::string>{
            "Name:	init\n"
            "VmRSS:\n"
        }));

    auto collector = std::make_shared<runtime_manager::InstanceMemoryCollector>(1, "id", 1000.0, "urn", tools);
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.IsNone(), true);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}

}