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

#include "api/include/meter.h"

#include <gtest/gtest.h>

#include <memory>

#include "sdk/include/storage.h"

namespace observability::test {

class MeterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        storage_ = std::make_shared<metrics::Storage>();
        processorActor_ = std::make_shared<metrics::ProcessorActor>();
        meter_ = std::make_shared<metrics::Meter>(storage_, processorActor_);
    }

    void TearDown() override
    {
    }
    std::shared_ptr<metrics::Storage> storage_ = nullptr;
    std::shared_ptr<metrics::Meter> meter_ = nullptr;
    std::shared_ptr<metrics::ProcessorActor> processorActor_ = nullptr;
};

/**
 * Feature: Meter
 * Description: Check Metric Rule
 */
TEST_F(MeterTest, CheckMetricCreateRule)
{
    // max name size 63
    std::string str(metrics::METRICS_NAME_MAX_SIZE + 1, 'a');
    auto invalidNameGauge1 = meter_->CreateGauge<uint32_t>(metrics::TitleOptions{ str });
    EXPECT_EQ(invalidNameGauge1, nullptr);
    // first char should be alpha
    auto invalidNameGauge2 = meter_->CreateGauge<uint32_t>(metrics::TitleOptions{ "_1234" });
    EXPECT_EQ(invalidNameGauge2, nullptr);
    // subsequent chars should be either of alphabets, digits, underscore, minus, dot
    auto invalidNameGauge3 = meter_->CreateGauge<double>(metrics::TitleOptions{ "12?34" });
    EXPECT_EQ(invalidNameGauge3, nullptr);

    // unit name size 63
    std::string str2(metrics::METRICS_UNIT_MAX_SIZE + 1, 'a');
    auto invalidUnitGauge1 = meter_->CreateGauge<uint64_t>(metrics::TitleOptions{ str2 });
    EXPECT_EQ(invalidUnitGauge1, nullptr);

    // description name size 512
    std::string str3(metrics::METRICS_DESCRIPTION_MAX_SIZE + 1, 'a');
    auto invalidDescriptionGauge1 = meter_->CreateGauge<int64_t>(metrics::TitleOptions{ str3 });
    EXPECT_EQ(invalidDescriptionGauge1, nullptr);
}

/**
 * Feature: Meter
 * Description: CreateIntervalGauge
 * Steps:
 * 1. Initialize meter
 * 2. Create a gauge and set value
 * 3. Collect metrics and verify metrics
 */
TEST_F(MeterTest, CreateIntervalGauge)
{
    auto interval = 1;
    auto gauge =
        meter_->CreateGauge<int64_t>(metrics::TitleOptions{ "a1234", "description1234", "unit1234" }, interval);
    EXPECT_NE(gauge, nullptr);
    int64_t gaugeValue = 1;
    gauge->Set(gaugeValue);
    auto metricsData = storage_->Collect(std::chrono::system_clock::now(), interval);
    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(gauge->Value(), gaugeValue);
    EXPECT_EQ(metricsData[0].name, "a1234");
    EXPECT_EQ(metricsData[0].description, "description1234");
    EXPECT_EQ(metricsData[0].unit, "unit1234");
}

double MockGetDiskUsage()
{
    static double value = 1;
    value += 1;
    return value;
}

void CallBack(observability::metrics::ObserveResult ob_res, observability::metrics::MetricValue refState)
{
    (void)refState;
    if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserverResultT<uint64_t>>>(ob_res)) {
        uint64_t value = static_cast<uint64_t>(MockGetDiskUsage());
        std::get<std::shared_ptr<observability::metrics::ObserverResultT<uint64_t>>>(ob_res)->Observe(value);
    }
}
/**
 * Feature: Meter
 * Description: CreateIntervalGaugeWithCallBack
 * Steps:
 * 1. Initialize meter
 * 2. Create a gauge with callback
 * 3. Collect metrics and verify metrics
 */
TEST_F(MeterTest, TestCreateIntervalGaugeWithCallBack)
{
    // Callback mode. The callback value is written to the corresponding metric every time.
    uint32_t interval = 2;
    auto diskGauge =
        meter_->CreateGauge<uint64_t>(metrics::TitleOptions{ "interval_2_disk_usage" }, interval, CallBack);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto metricsData = storage_->Collect(std::chrono::system_clock::now(), interval);
    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(diskGauge->Value(), static_cast<uint64_t>(2));
}

/**
 * Feature: Meter
 * Description: Create empty gauge
 * Steps:
 * 1. Initialize meter with disable metrics
 * 2. Create a gauge
 * 3. Collect metrics and verify metrics
 */
TEST_F(MeterTest, CreateEmptyGauge)
{
    auto meter = std::make_shared<metrics::Meter>(nullptr, nullptr, false);
    auto gauge = meter->CreateGauge<double>(metrics::TitleOptions{ "emptygauge", "emptygauge", "emptygauge" });
    gauge->Set(1);
    gauge->Increment(2);
    gauge->Decrement(3);
    *gauge += 4;
    *gauge -= 5;
    EXPECT_EQ(gauge->Value(), 0);

    gauge->AddLabel("label", "value");
    EXPECT_EQ(gauge->GetLabels().size(), static_cast<uint64_t>(0));
    gauge->DelLabelByKey("label");
}

}  // namespace observability::test
