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

#include "sdk/include/storage.h"

#include <gtest/gtest.h>

#include <thread>

#include "api/include/gauge.h"

const std::string name = "test_name";
const std::string asyncName = "async_test_name";
const std::string syncName = "sync_test_name";
namespace observability::test {

static void AsyncCallback(observability::metrics::ObserveResult ob_res, observability::metrics::MetricValue state)
{
    if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)) {
        double value = std::get<double>(state);
        std::get<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)->Observe(value);
    } else if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserverResultT<int64_t>>>(ob_res)) {
        int64_t value = std::get<int64_t>(state);
        std::get<std::shared_ptr<observability::metrics::ObserverResultT<int64_t>>>(ob_res)->Observe(value);
    } else {
    }
}

/**
 * Feature: Add Async Instrument.
 * Description: Add Async instrument to storage.
 * Steps:
 * 1. Add two gauge to storage with interval value1 by AddMetricAsync.
 * 2. Invoke Collect to get the collect value with param value1.
 * 3. Add one gauge to storage with interval value2 by AddMetricAsync.
 * 4. Invoke Collect to get the collect value with param value2.
 * Expectation: the gauge can be get as expected,
 * the value1 collect result length is 2, the value2 collect result length is 1
 */
TEST(StorageTest, store_add_async)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();

    int interval1 = 5;
    auto value1 = observability::metrics::MetricValue(0.5);
    auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
    storage_->AddMetricAsync(AsyncCallback, value1, gauge, interval1);

    auto gauge2 = std::make_shared<observability::metrics::Gauge<double>>(asyncName, asyncName, asyncName);
    auto value2 = observability::metrics::MetricValue(1.55);
    storage_->AddMetricAsync(
        [](observability::metrics::ObserveResult ob_res, observability::metrics::MetricValue state) {
            if (std::holds_alternative<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)) {
                double value = std::get<double>(state);
                std::get<std::shared_ptr<observability::metrics::ObserverResultT<double>>>(ob_res)->Observe(value);
            }
        },
        value2, gauge2, interval1);

    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsData = storage_->Collect(collect_time, interval1);

    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(2));
    EXPECT_EQ(metricsData[0].metricValue, value1);
    EXPECT_EQ(metricsData[1].metricValue, value2);

    int interval2 = 0;
    auto gauge3 = std::make_shared<observability::metrics::Gauge<int>>("new_3", "new_3", "new_3");
    int64_t val = 3;
    auto value3 = observability::metrics::MetricValue(val);
    storage_->AddMetricAsync(AsyncCallback, value3, gauge3, interval2);

    std::chrono::system_clock::time_point collect_time2 = std::chrono::system_clock::now();
    auto metricsData2 = storage_->Collect(collect_time2, interval1);
    EXPECT_EQ(metricsData2.size(), static_cast<uint64_t>(2));

    std::chrono::system_clock::time_point collect_time3 = std::chrono::system_clock::now();
    auto metricsData3 = storage_->Collect(collect_time3, interval2);
    EXPECT_EQ(metricsData3.size(), static_cast<uint64_t>(1));
}

/**
 * Feature: Add sync Instrument.
 * Description: Add sync instrument to storage.
 * Steps:
 * 1. Add two gauge to storage with interval value1 by AddMetric.
 * 2. Invoke Collect to get the collect value with param value1.
 * 3. Add one gauge to storage with interval value2 by AddMetricA.
 * 4. Invoke Collect to get the collect value with param value2.
 * Expectation: the gauge can be get as expected,
 * the value1 collect result length is 2, the value2 collect result length is 1
 */
TEST(StorageTest, store_add_sync)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();

    int interval1 = 5;
    auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
    storage_->AddMetric(gauge, interval1);
    double value1 = 2.22;
    gauge->Set(value1);

    auto gauge2 = std::make_shared<observability::metrics::Gauge<double>>(syncName, syncName, syncName);
    storage_->AddMetric(gauge2, interval1);
    double value2 = 3.09;
    gauge2->Set(value2);

    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsData = storage_->Collect(collect_time, interval1);

    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(2));
    EXPECT_EQ(std::get<double>(metricsData[0].metricValue), value1);
    EXPECT_EQ(std::get<double>(metricsData[1].metricValue), value2);

    int interval2 = 10;
    auto gauge3 = std::make_shared<observability::metrics::Gauge<double>>("new_3", "new_3", "new_3");
    double value3 = 3.33;
    storage_->AddMetric(gauge3, interval2);
    gauge2->Set(value3);

    std::chrono::system_clock::time_point collect_time3 = std::chrono::system_clock::now();
    auto metricsData3 = storage_->Collect(collect_time3, interval2);
    EXPECT_EQ(metricsData3.size(), static_cast<uint64_t>(1));
}

/**
 *Feature: Not Add Instrument.
 */
TEST(StorageTest, NoInstrumentTest)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();
    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsData = storage_->Collect(collect_time, 0);
    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(0));
    auto metricsData2 = storage_->Collect(collect_time, 1);
    EXPECT_EQ(metricsData2.size(), static_cast<uint64_t>(0));
}

/**
 * Feature: Add Instrument With Sync And Async.
 * Description: Add sync and async instrument to storage.
 * Steps:
 * 1. Add one gauge to storage with interval value1 by AddMetricAsync.
 * 2. Add one gauge to storage with interval value1 by AddMetric.
 * 3. Add one gauge to storage with interval value2 by AddMetricAsync.
 * 4. Add one gauge to storage with interval value2 by AddMetric.
 * 4. Invoke Collect to get the collect value with param value1 and value2.
 * Expectation: the gauge can be get as expected,
 * the value1 collect result length is 2, value is correct with the added.
 * the value2 collect result length is 2, value is correct with the added.
 */
TEST(StorageTest, store_add_multi)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();

    int interval1 = 5;
    auto value1 = observability::metrics::MetricValue(0.5);
    auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
    storage_->AddMetricAsync(AsyncCallback, value1, gauge, interval1);

    auto gauge2 = std::make_shared<observability::metrics::Gauge<double>>(syncName, syncName, syncName);
    storage_->AddMetric(gauge2, interval1);
    double value2 = 2.22;
    gauge2->Set(value2);

    int interval2 = 9;
    auto value3 = observability::metrics::MetricValue(4.55);
    auto gauge3 = std::make_shared<observability::metrics::Gauge<double>>(syncName, syncName, syncName);
    storage_->AddMetricAsync(AsyncCallback, value3, gauge3, interval2);

    auto gauge4 = std::make_shared<observability::metrics::Gauge<double>>("name_new_sync_multi", "name_new_sync_multi",
                                                                          "name_new_sync_multi");
    storage_->AddMetric(gauge4, interval2);
    double value4 = 4.66;
    gauge4->Set(value4);

    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsData = storage_->Collect(collect_time, interval1);
    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(2));
    EXPECT_EQ(metricsData[0].metricValue, value1);
    EXPECT_EQ(std::get<double>(metricsData[1].metricValue), value2);

    std::chrono::system_clock::time_point collect_time2 = std::chrono::system_clock::now();
    auto metricsData2 = storage_->Collect(collect_time2, interval2);
    EXPECT_EQ(metricsData.size(), static_cast<uint64_t>(2));
    EXPECT_EQ(metricsData2[0].metricValue, value3);
    EXPECT_EQ(std::get<double>(metricsData2[1].metricValue), value4);
}

/**
 * Feature: Change Instrument Value.
 * Description: Change the value then check if works.
 * Steps:
 * 1. Add one gauge to storage with interval value1 by AddMetric.
 * 2. Set the gauge value.
 * 3. Collect the metric data.
 * 4. Set the gauge value again.
 * 5. Collect the metric data again.
 * Expectation: the gauge value is synchronous as expected.
 */
TEST(StorageTest, change_gauge_value)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();
    int interval1 = 5;
    auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
    storage_->AddMetric(gauge, interval1);
    double value1 = 0.5;
    gauge->Set(value1);

    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsData1 = storage_->Collect(collect_time, interval1);
    EXPECT_EQ(metricsData1.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(std::get<double>(metricsData1[0].metricValue), value1);

    double value2 = 1.1;
    gauge->Set(value2);
    auto metricsData2 = storage_->Collect(collect_time, interval1);
    EXPECT_EQ(metricsData2.size(), static_cast<uint64_t>(1));
    EXPECT_EQ(std::get<double>(metricsData2[0].metricValue), value2);
}

void thread_example(std::shared_ptr<observability::metrics::Gauge<double>> gauge)
{
    gauge->Set(gauge->Value() + 1);
}

TEST(StorageTest, multi_thread)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();
    std::vector<std::thread> threads;
    std::vector<std::shared_ptr<observability::metrics::Gauge<double>>> gaugeList;
    int interval = 1;
    for (int i = 0; i < 1000; i++) {
        std::string name = "test_" + std::to_string(i);
        auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
        storage_->AddMetric(gauge, interval);
        threads.emplace_back(&thread_example, gauge);
        gaugeList.push_back(gauge);
    }

    for (auto &thread : threads) {
        thread.join();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
    auto metricsDataList = storage_->Collect(collect_time, 1);
    EXPECT_EQ(metricsDataList.size(), static_cast<uint64_t>(1000));
    EXPECT_EQ(std::get<double>(metricsDataList[0].metricValue), 1);

    std::vector<std::thread> threads2;
    for (int i = 0; i < 1000; i++) {
        threads2.emplace_back(&thread_example, gaugeList[i]);
    }
    for (auto &thread : threads2) {
        thread.join();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::chrono::system_clock::time_point collect_time2 = std::chrono::system_clock::now();
    auto metricsDataList2 = storage_->Collect(collect_time2, 1);
    EXPECT_EQ(metricsDataList2.size(), static_cast<uint64_t>(1000));
    EXPECT_EQ(std::get<double>(metricsDataList2[0].metricValue), 2);
}

}  // namespace observability::test