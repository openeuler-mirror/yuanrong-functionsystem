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
#include "runtime_manager/metrics/collector/system_proc_cpu_collector.h"

namespace functionsystem::test {

class SystemProcCPUCollectorTest : public ::testing::Test {};

/**
 * Feature: SystemProcCPUCollector
 * Description: Generate filter
 * Steps:
 * Expectation:
 * system-cpu
 */
TEST_F(SystemProcCPUCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::SystemProcCPUCollector>(0, nullptr);
    EXPECT_EQ(collector->GenFilter(), "system-CPU");
}

/**
 * Feature: SystemProcCPUCollector
 * Description: Get Limit
 * Steps:
 * Expectation:
 */
TEST_F(SystemProcCPUCollectorTest, GetLimit)
{
    auto collector = std::make_shared<runtime_manager::SystemProcCPUCollector>(100.0, nullptr);
    auto limit = collector->GetLimit();
    EXPECT_EQ(limit.value, 100.0);
    EXPECT_EQ(limit.instanceID.IsNone(), true);
}

/**
 * Feature: SystemCPUCollector
 * Description: Get Usage
 * Steps:
 * Expectation:
 */
TEST_F(SystemProcCPUCollectorTest, GetUsage)
{
    // given
    auto given = std::vector<litebus::Future<runtime_manager::Metrics>>{
        {runtime_manager::Metrics{120.0, 140.0, {"id-1"}, {}, runtime_manager::metrics_type::CPU}},
        {runtime_manager::Metrics{140.0, 160.0, {"id-1"}, {}, runtime_manager::metrics_type::MEMORY}},
        {runtime_manager::Metrics{180.0, 200.0, {"id-2"}, {}, runtime_manager::metrics_type::CPU}},
        {runtime_manager::Metrics{220.0, 240.0, {"id-2"}, {}, runtime_manager::metrics_type::MEMORY}},
    };


    auto collector = std::make_shared<runtime_manager::SystemProcCPUCollector>(
        100.0,
        [given = given]() -> std::vector<litebus::Future<runtime_manager::Metrics>> {
            return given;
        });
    auto usage = collector->GetUsage().Get();
    EXPECT_EQ(usage.value.Get(), 300.0);
    EXPECT_EQ(usage.instanceID.IsNone(), true);
}

}