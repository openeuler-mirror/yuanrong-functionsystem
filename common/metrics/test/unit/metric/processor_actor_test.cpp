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

#include "api/include/processor_actor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>

#include "sdk/include/basic_exporter.h"

namespace observability::test {
const int SLEEP_TIME_3SEC = 3;
const int SLEEP_TIME_5SEC = 5;
const int TIMERS_SEC[] = { 1, 2, 3, 4 };

// mock export and collect method
class Mocker {
public:
    MOCK_METHOD(bool, Export, (const std::vector<metrics::MetricsData> &data));
    MOCK_METHOD(std::vector<metrics::MetricsData>, Collect,
                (std::chrono::system_clock::time_point timeStamp, int interval));
};

std::vector<metrics::MetricsData> GetMockMetricData()
{
    std::vector<metrics::MetricsData> metricDataList;
    metrics::MetricsData metricData = { .labels = { { "key", "value" } },
                                        .name = "mock_data",
                                        .description = "description",
                                        .unit = "unit",
                                        .metricType = "Gauge",
                                        .collectTimeStamp = std::chrono::system_clock::now(),
                                        .metricValue = metrics::MetricValue(1.0) };
    metricDataList.push_back(metricData);
    return metricDataList;
}

class ProcessorActorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        mockerPtr_ = std::make_shared<Mocker>();
        processorActorPtr_ = std::make_shared<metrics::ProcessorActor>();
        processorActorPtr_->RegisterExportFunc(std::bind(&Mocker::Export, mockerPtr_.get(), std::placeholders::_1));
        processorActorPtr_->RegisterCollectFunc(
            std::bind(&Mocker::Collect, mockerPtr_.get(), std::placeholders::_1, std::placeholders::_2));
        litebus::Spawn(processorActorPtr_);
    }

    void TearDown() override
    {
        ASSERT_NE(processorActorPtr_, nullptr);
        litebus::Terminate(processorActorPtr_->GetAID());
        litebus::Await(processorActorPtr_->GetAID());
        processorActorPtr_ = nullptr;
        mockerPtr_ = nullptr;
    }
    std::shared_ptr<metrics::ProcessorActor> processorActorPtr_;
    std::shared_ptr<Mocker> mockerPtr_;
};

/**
 * Feature: Metrics Basic Metric
 * Description: Check Basic Metric Initialize Info
 * 1. Set Export Mode: Simple(collect data then export directly)
 * 2. Register Timer(X): Data with a collection interval of X is processed every X seconds.
 * 3. Sleep a while, for verify result
 */
TEST_F(ProcessorActorTest, ProcessSimpleData)
{
    EXPECT_CALL(*mockerPtr_.get(), Collect(testing::_, testing::_))
        .WillRepeatedly(testing::Return(GetMockMetricData()));

    processorActorPtr_->SetExportMode(metrics::ExporterOptions{ metrics::ExporterOptions::Mode::SIMPLE });
    int shouldExportTimers = SLEEP_TIME_5SEC / 4 + SLEEP_TIME_5SEC / 3 + SLEEP_TIME_5SEC / 2 + SLEEP_TIME_5SEC / 1;
    EXPECT_CALL(*mockerPtr_.get(), Export(testing::_)).Times(::testing::AtLeast(shouldExportTimers));

    for (const auto &t : TIMERS_SEC) {
        processorActorPtr_->RegisterTimer(t);
    }
    std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME_5SEC));
}

/**
 * Feature: Process export batch data
 * Description: Check Processor Export Batch Data
 * 1. Set Export Mode: Batch
 * 2. Register Timer(X): Data with a collection interval of X is processed every X seconds.
 * 3. Sleep a while, for verify result
 */
TEST_F(ProcessorActorTest, ProcessBatchDataSetExportInterval)
{
    auto options = metrics::ExporterOptions{
        .mode = metrics::ExporterOptions::Mode::BATCH,
        .batchSize = 100,
        .batchIntervalSec = 1,
    };

    EXPECT_CALL(*mockerPtr_.get(), Collect(testing::_, 1))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly(testing::Return(GetMockMetricData()));
    EXPECT_CALL(*mockerPtr_.get(), Collect(testing::_, 2))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(testing::Return(GetMockMetricData()));

    int shouldExportTimes = SLEEP_TIME_3SEC / options.batchIntervalSec - 1;
    EXPECT_CALL(*mockerPtr_.get(), Export(testing::_)).Times(::testing::AtLeast(shouldExportTimes));

    processorActorPtr_->SetExportMode(options);
    processorActorPtr_->RegisterTimer(TIMERS_SEC[0]);
    processorActorPtr_->RegisterTimer(TIMERS_SEC[1]);

    std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME_3SEC));
}

/**
 * Feature: Process export batch data
 * Description: Check Processor Export Batch Data
 * 1. Set Export Mode: Batch, set batch size
 * 2. Register Timer(X): Data with a collection interval of X is processed every X seconds.
 * 3. Sleep a while, for verify result
 */

TEST_F(ProcessorActorTest, ProcessBatchDataSetExportBatch)
{
    auto options = metrics::ExporterOptions{
        .mode = metrics::ExporterOptions::Mode::BATCH,
        .batchSize = 2,
        .batchIntervalSec = 200,
    };

    EXPECT_CALL(*mockerPtr_.get(), Collect(testing::_, 1))
        .Times(::testing::AtLeast(2))
        .WillRepeatedly(testing::Return(GetMockMetricData()));
    EXPECT_CALL(*mockerPtr_.get(), Collect(testing::_, 2))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(testing::Return(GetMockMetricData()));

    int shouldExportTimes = (SLEEP_TIME_3SEC / 1 + SLEEP_TIME_3SEC / 2) / options.batchSize;
    EXPECT_CALL(*mockerPtr_.get(), Export(testing::_)).Times(::testing::AtLeast(shouldExportTimes));

    processorActorPtr_->SetExportMode(options);
    processorActorPtr_->RegisterTimer(TIMERS_SEC[0]);
    processorActorPtr_->RegisterTimer(TIMERS_SEC[1]);

    std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIME_3SEC));
}
}  // namespace observability::test
