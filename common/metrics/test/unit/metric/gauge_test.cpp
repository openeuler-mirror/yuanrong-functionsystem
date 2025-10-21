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

#include "api/include/gauge.h"

#include <gtest/gtest.h>

namespace observability::test {
const double NUM = 10;
class GaugeTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        gaugePtr_ = std::make_shared<metrics::Gauge<double>>("name");
    }

    void TearDown() override
    {
        gaugePtr_ = nullptr;
    }

    std::shared_ptr<metrics::Gauge<double>> gaugePtr_ = nullptr;
};

/**
 * Feature: Metrics Gauge
 * Description: Check Gauge Initialize Info
 */
TEST_F(GaugeTest, GetValue)
{
    EXPECT_EQ(gaugePtr_->Value(), static_cast<double>(0));
    EXPECT_EQ(gaugePtr_->GetName(), "name");
    EXPECT_EQ(gaugePtr_->GetDescription(), "");
    EXPECT_EQ(gaugePtr_->GetUnit(), "");
    EXPECT_EQ(gaugePtr_->GetValueType(), metrics::ValueType::DOUBLE);
    EXPECT_EQ(gaugePtr_->GetMetricType(), metrics::MetricType::GAUGE);
}

/**
 * Feature: Metrics Gauge
 * Description: Test Gauge Set Value
 */
TEST_F(GaugeTest, SetValue)
{
    gaugePtr_->Set(NUM);
    EXPECT_EQ(gaugePtr_->Value(), NUM);
}

/**
 * Feature: Metrics Gauge
 * Description: Test Gauge Increment Value
 */
TEST_F(GaugeTest, Increment)
{
    gaugePtr_->Increment(NUM);
    EXPECT_EQ(gaugePtr_->Value(), NUM);
}

/**
 * Feature: Metrics Gauge
 * Description: Test Gauge Decrement Value
 */
TEST_F(GaugeTest, Decrement)
{
    gaugePtr_->Decrement(NUM);
    EXPECT_EQ(gaugePtr_->Value(), NUM * -1);
}

/**
 * Feature: Metrics Gauge
 * Description: Test Gauge Overload Calc
 */
TEST_F(GaugeTest, OverLoadCalc)
{
    auto gauge = metrics::Gauge<double>("name");
    gauge += NUM;
    gauge -= NUM;
    EXPECT_EQ(gaugePtr_->Value(), static_cast<double>(0));
}

/**
 * Feature: Metrics Gauge
 * Description: Create EmptyGauge when disable Metrics
 */
TEST_F(GaugeTest, EmptyGauge)
{
    auto gauge = metrics::EmptyGauge<double>();
    gauge.Set(NUM);
    gauge.Increment(NUM);
    gauge.Decrement(NUM);
    gauge += NUM;
    gauge -= NUM;
    EXPECT_EQ(gauge.Value(), static_cast<double>(0));

    gauge.AddLabel("label", "value");
    EXPECT_EQ(gauge.GetLabels().size(), static_cast<uint64_t>(0));
    gauge.DelLabelByKey("label");
}

}  // namespace observability::test
