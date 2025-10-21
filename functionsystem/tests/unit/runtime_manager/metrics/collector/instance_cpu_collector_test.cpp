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
#include <gmock/gmock.h>
#include <gmock/gmock-actions.h>
#include "runtime_manager/metrics/collector/instance_cpu_collector.h"

namespace functionsystem::test {

class MockProcFSTools : public ProcFSTools {
public:
    MOCK_METHOD(litebus::Option<std::string>, Read, (const std::string &path), (override));
};

class InstanceCPUCollectorTest : public ::testing::Test {
public:
    void SetUp() override{};
    void TearDown() override{};
};

/**
 * Feature: InstanceCPUCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * functionUrn-InstanceId-cpu
 */
TEST_F(InstanceCPUCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GenFilter(), "urn-id-CPU");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 * Get constructor parameter limit
 */
TEST_F(InstanceCPUCollectorTest, GetLimit)
{
    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn");
    EXPECT_EQ(collector->GetLimit().value.Get(), 1000.0);
    EXPECT_EQ(collector->GetLimit().instanceID.Get(), "id");
}

/**
 * Feature: InstanceCPUCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 * Get constructor parameter usage
 */
TEST_F(InstanceCPUCollectorTest, GetUsage)
{
    auto tools = std::make_shared<MockProcFSTools>();

    auto collector = std::make_shared<runtime_manager::InstanceCPUCollector>(1, "id", 1000.0, "urn");
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.Get(), 0);
    EXPECT_EQ(usage.instanceID.Get(), "id");
}
}