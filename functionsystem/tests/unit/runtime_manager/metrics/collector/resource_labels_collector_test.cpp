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

#include "runtime_manager/metrics/collector/resource_labels_collector.h"

#include <gtest/gtest.h>

#include <fstream>

#include "utils/future_test_helper.h"

namespace functionsystem::test {

class ResourceLabelsCollectorTest : public ::testing::Test {};

/**
 * Feature: ResourceLabelsCollector
 * Description: Generate filter
 * Steps:
 * Expectation: system-InitLabels
 */
TEST_F(ResourceLabelsCollectorTest, GenFilter)
{
    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>("/tmp/home/sn/podInfo/labels");
    EXPECT_EQ(collector->GenFilter(), "system-InitLabels");
}

/**
 * Feature: ResourceLabelsCollector
 * Description: Get Limit
 * Steps: mock envs
 * Expectation: get correct result
 */
TEST_F(ResourceLabelsCollectorTest, GetLabelsOK)
{
    litebus::os::SetEnv(runtime_manager::INIT_LABELS_ENV_KEY, R"({"a":"b", "c":"d"})");
    litebus::os::SetEnv(runtime_manager::NODE_ID_LABEL_KEY, "123");

    if (!litebus::os::ExistPath("/tmp/home/sn/podInfo")) {
        litebus::os::Mkdir("/tmp/home/sn/podInfo");
    }

    std::ofstream outfile;
    outfile.open("/tmp/home/sn/podInfo/labels");
    outfile << "e=\"f\"\ng=\"h\"";
    outfile.close();

    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>("/tmp/home/sn/podInfo/labels");
    auto limit = collector->GetLimit();
    EXPECT_TRUE(limit.initLabels.IsSome());
    auto initLabels = limit.initLabels.Get();
    EXPECT_EQ(initLabels.size(), static_cast<uint32_t>(5));
    EXPECT_TRUE(initLabels.find("a") != initLabels.end());
    EXPECT_EQ(initLabels.find("a")->second, "b");
    EXPECT_TRUE(initLabels.find("c") != initLabels.end());
    EXPECT_EQ(initLabels.find("c")->second, "d");

    EXPECT_TRUE(initLabels.find("e") != initLabels.end());
    EXPECT_EQ(initLabels.find("e")->second, "f");
    EXPECT_TRUE(initLabels.find("g") != initLabels.end());
    EXPECT_EQ(initLabels.find("g")->second, "h");

    EXPECT_TRUE(initLabels.find(runtime_manager::NODE_ID_LABEL_KEY) != initLabels.end());
    EXPECT_EQ(initLabels.find(runtime_manager::NODE_ID_LABEL_KEY)->second, "123");
    litebus::os::UnSetEnv(runtime_manager::INIT_LABELS_ENV_KEY);
    litebus::os::UnSetEnv(runtime_manager::NODE_ID_LABEL_KEY);
    litebus::os::Rm("/tmp/home/sn/podInfo/labels");
}

/**
 * Feature: ResourceLabelsCollector
 * Description: Get usage
 * Steps: mock envs
 * Expectation: get correct result
 */
TEST_F(ResourceLabelsCollectorTest, GetUsage)
{
    litebus::os::SetEnv(runtime_manager::INIT_LABELS_ENV_KEY, R"({"a":"b", "c":"d"})");
    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>();
    auto usageFuture = collector->GetUsage();
    ASSERT_AWAIT_READY(usageFuture);
    EXPECT_TRUE(usageFuture.Get().initLabels.IsSome());
    auto initLabels = usageFuture.Get().initLabels.Get();
    EXPECT_EQ(initLabels.size(), static_cast<uint32_t>(2));
    EXPECT_TRUE(initLabels.find("a") != initLabels.end());
    EXPECT_EQ(initLabels.find("a")->second, "b");
    EXPECT_TRUE(initLabels.find("c") != initLabels.end());
    EXPECT_EQ(initLabels.find("c")->second, "d");
    litebus::os::UnSetEnv(runtime_manager::INIT_LABELS_ENV_KEY);
}

/**
 * Feature: ResourceLabelsCollector
 * Description: GetLabelsInvalid
 * Steps: mock invalid json string environment var
 * Expectation: initLabels is None
 */
TEST_F(ResourceLabelsCollectorTest, GetLabelsInvalid)
{
    litebus::os::SetEnv(runtime_manager::INIT_LABELS_ENV_KEY, R"({x"a":"b", "c":"d"})");
    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>();
    auto limit = collector->GetLimit();
    EXPECT_TRUE(limit.initLabels.IsNone());
    litebus::os::UnSetEnv(runtime_manager::INIT_LABELS_ENV_KEY);
}

/**
 * Feature: ResourceLabelsCollector
 * Description: GetLabelsEmpty
 * Steps: mock empty string environment var
 * Expectation: initLabels is None
 */
TEST_F(ResourceLabelsCollectorTest, GetLabelsEmpty)
{
    litebus::os::SetEnv(runtime_manager::INIT_LABELS_ENV_KEY, "");
    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>();
    auto limit = collector->GetLimit();
    EXPECT_TRUE(limit.initLabels.IsNone());
    litebus::os::UnSetEnv(runtime_manager::INIT_LABELS_ENV_KEY);
}

/**
 * Feature: ResourceLabelsCollector
 * Description: GetLabelsNotExists
 * Steps: don't mock environment var
 * Expectation: initLabels is None
 */
TEST_F(ResourceLabelsCollectorTest, GetLabelsNotExists)
{
    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>();
    auto limit = collector->GetLimit();
    EXPECT_TRUE(limit.initLabels.IsNone());
}

TEST_F(ResourceLabelsCollectorTest, GetLabelsFromEnv)
{
    litebus::os::SetEnv(runtime_manager::NODE_ID_LABEL_KEY, "NODE_ID");
    litebus::os::SetEnv(runtime_manager::HOST_IP_LABEL_KEY, "HOST_IP");

    auto collector = std::make_shared<runtime_manager::ResourceLabelsCollector>();
    auto usageFuture = collector->GetUsage();
    ASSERT_AWAIT_READY(usageFuture);
    EXPECT_TRUE(usageFuture.Get().initLabels.IsSome());
    auto initLabels = usageFuture.Get().initLabels.Get();
    EXPECT_EQ(initLabels.size(), static_cast<uint32_t>(2));
    EXPECT_TRUE(initLabels.find("NODE_ID") != initLabels.end());
    EXPECT_EQ(initLabels.find("NODE_ID")->second, "NODE_ID");
    EXPECT_TRUE(initLabels.find("HOST_IP") != initLabels.end());
    EXPECT_EQ(initLabels.find("HOST_IP")->second, "HOST_IP");

    litebus::os::UnSetEnv(runtime_manager::NODE_ID_LABEL_KEY);
    litebus::os::UnSetEnv(runtime_manager::HOST_IP_LABEL_KEY);
}
}  // namespace functionsystem::test