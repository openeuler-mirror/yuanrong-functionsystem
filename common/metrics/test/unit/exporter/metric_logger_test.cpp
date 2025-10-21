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
#include <fstream>
#include <gtest/gtest.h>
#include <utils/os_utils.hpp>

#include "exporters/file_exporter/include/metric_logger.h"
#include "exporters/file_exporter/metric_logger.cpp"
namespace observability::test {
using namespace observability::metrics;

class MetricLoggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FileParam fileParam = { "/tmp/metrics", "metric_logger_test", 3  };
        metricLogger = std::make_shared<MetricLogger>(fileParam);
    }

    void TearDown() override
    {
        metricLogger = nullptr;
        std::remove("/tmp/metrics/metric_logger_test.data");
    }

    std::shared_ptr<MetricLogger> metricLogger = nullptr;

};

TEST_F(MetricLoggerTest, RecordTest)
{
    const std::string &str = "test_log";
    metricLogger->Record(str);
}

TEST_F(MetricLoggerTest, FormatTimePoint)
{
    EXPECT_NO_THROW(observability::metrics::FormatTimePoint());
}
}
