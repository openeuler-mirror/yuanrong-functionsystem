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

#include "api/include/basic_metric.h"

#include <gtest/gtest.h>

#include "sdk/include/metrics_data.h"

namespace observability::test {

class BasicMetricTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        metricPtr_ =
            std::make_shared<metrics::BasicMetric>("name", "description", "unit", metrics::MetricType::COUNTER);
    }

    void TearDown() override
    {
        metricPtr_ = nullptr;
    }

    std::shared_ptr<metrics::BasicMetric> metricPtr_ = nullptr;
};

/**
 * Feature: Metrics Basic Metric
 * Description: Check Basic Metric Initialize Info
 */
TEST_F(BasicMetricTest, GetValue)
{
    EXPECT_EQ(metricPtr_->GetName(), "name");
    EXPECT_EQ(metricPtr_->GetDescription(), "description");
    EXPECT_EQ(metricPtr_->GetUnit(), "unit");
    EXPECT_EQ(metricPtr_->GetValueType(), metrics::ValueType::UNKNOWN);
    EXPECT_EQ(metricPtr_->GetMetricType(), metrics::MetricType::COUNTER);
}

/**
 * Feature: Metrics Basic Metric
 * Description: Basic Metric Label Test
 */
TEST_F(BasicMetricTest, LabelTest)
{
    auto labels = metricPtr_->GetLabels();
    EXPECT_TRUE(labels.empty());

    metricPtr_->AddLabel("NodeID", "127.0.0.1");
    labels = metricPtr_->GetLabels();
    EXPECT_TRUE(!labels.empty());
    auto it = labels.find("NodeID");
    EXPECT_EQ(it->second, "127.0.0.1");

    metricPtr_->DelLabelByKey("NodeID");
    labels = metricPtr_->GetLabels();
    EXPECT_TRUE(labels.empty());
}

/**
 * Feature: Metric Value Test
 * Description: Test Metric Value is or not equal the truth.
 */
TEST_F(BasicMetricTest, ValueTest)
{
    auto metricValue1 = observability::metrics::MetricValue(1.55);
    EXPECT_EQ("1.55", metrics::ToString(metricValue1));

    int64_t value2 = 11;
    auto metricValue2 = observability::metrics::MetricValue(value2);
    EXPECT_EQ("11", metrics::ToString(metricValue2));
}

TEST_F(BasicMetricTest, TimestampTest)
{
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    metricPtr_->SetTimestamp(timestamp);
    EXPECT_EQ(metricPtr_->GetTimestamp(), timestamp);
}

}  // namespace observability::test
