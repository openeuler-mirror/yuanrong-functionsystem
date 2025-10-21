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

#include "metrics/api/metric_data.h"
#include "metrics/sdk/instruments.h"
#include "metrics/sdk/metric_pusher.h"
#include "sdk/include/observable_registry.h"
#include "../mocks/mock_pusher.h"

namespace observability::test {

const double MOCK_VALUES[] = { 0, 1, 2, 3, 4, 5 };
sdk::metrics::InstrumentDescriptor instrumentDescriptor = sdk::metrics::InstrumentDescriptor {
    .name = "test_metric",
    .description = "test metric desc",
    .unit = "ms",
    .type = sdk::metrics::InstrumentType::COUNTER,
    .valueType = sdk::metrics::InstrumentValueType::UINT64 };
const api::metrics::MetricLabels labels1 = { std::pair{"host", "127.0.0.1"}, std::pair{"label1", "l1"} };
const api::metrics::MetricLabels labels2 = { std::pair{"host", "127.0.0.1"}, std::pair{"label2", "l2"} };
const std::vector<std::pair<api::metrics::MetricLabels, uint64_t>> uint64Res = {
    std::pair{ labels1, 3 }, std::pair{ labels2, 5 }
};
const std::vector<std::pair<api::metrics::MetricLabels, int64_t>> intRes = {
    std::pair{ labels1, 3 }, std::pair{ labels2, 5 }
};
const std::vector<std::pair<api::metrics::MetricLabels, double>> doubleRes = {
    std::pair{ labels1, 3.0 }, std::pair{ labels2, 0.0 }
};
api::metrics::CallbackPtr cb = [](api::metrics::ObserveResult ob_res) {
    if (std::holds_alternative<std::shared_ptr<api::metrics::ObserveResultT<uint64_t>>>(ob_res)) {
        std::get<std::shared_ptr<api::metrics::ObserveResultT<uint64_t>>>(ob_res)->Observe(uint64Res);
    }
    if (std::holds_alternative<std::shared_ptr<api::metrics::ObserveResultT<int64_t>>>(ob_res)) {
        std::get<std::shared_ptr<api::metrics::ObserveResultT<int64_t>>>(ob_res)->Observe(intRes);
    }
    if (std::holds_alternative<std::shared_ptr<api::metrics::ObserveResultT<double>>>(ob_res)) {
        std::get<std::shared_ptr<api::metrics::ObserveResultT<double>>>(ob_res)->Observe(doubleRes);
    }
};


class ObservableRegistryTest : public testing::Test {
protected:
    static void SetUpTestCase()
    {
        mockPusher_ = std::make_shared<MockPusher>();
    }

    static void TearDownTestCase()
    {
        mockPusher_ = nullptr;
    }

    void SetUp()
    {
        std::vector<std::shared_ptr<sdk::metrics::PusherHandle>> vec;
        vec.push_back(mockPusher_);
        observableRegistry_ = std::make_shared<sdk::metrics::ObservableRegistry>(vec);
    }

    void TearDown()
    {
        observableRegistry_ = nullptr;
    }

    inline static std::shared_ptr<sdk::metrics::ObservableRegistry> observableRegistry_;
    inline static std::shared_ptr<MockPusher> mockPusher_;
};

TEST_F(ObservableRegistryTest, AddObservableInstrumentFirstTime)
{
    int interval = 10;
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval)->second.size() == 1);
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval) != observableRegistry_->GetCallbackIntervalMap().end());
    ASSERT_TRUE(observableRegistry_->GetCollectIntervalMap().find(interval)->second.begin()->name == "test_metric");
}

TEST_F(ObservableRegistryTest, AddObservableInstrumentSecondTime)
{
    int interval = 10;
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval)->second.size() == 2);
    ASSERT_TRUE(observableRegistry_->GetCollectIntervalMap().find(interval)->second.size() == 2);
}

TEST_F(ObservableRegistryTest, PushObservableUint64Res)
{
    int interval = 10;
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    EXPECT_CALL(*mockPusher_, Push).Times(2).WillRepeatedly(testing::Return());
    EXPECT_CALL(*mockPusher_, GetAggregationTemporality).WillRepeatedly(testing::Return(sdk::metrics::AggregationTemporality::DELTA));
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval)->second.size() == 1);
    observableRegistry_->Observe(interval);
}

TEST_F(ObservableRegistryTest, PushObservableDoubleRes)
{
    int interval = 10;
    instrumentDescriptor.valueType = sdk::metrics::InstrumentValueType::DOUBLE;
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    EXPECT_CALL(*mockPusher_, Push).Times(2).WillRepeatedly(testing::Return());
    EXPECT_CALL(*mockPusher_, GetAggregationTemporality).WillRepeatedly(testing::Return(sdk::metrics::AggregationTemporality::DELTA));
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval)->second.size() == 1);
    observableRegistry_->Observe(interval);
}

TEST_F(ObservableRegistryTest, PushObservableInt64Res)
{
    int interval = 10;
    instrumentDescriptor.valueType = sdk::metrics::InstrumentValueType::INT64;
    observableRegistry_->AddObservableInstrument(cb, instrumentDescriptor, interval);
    EXPECT_CALL(*mockPusher_, Push).Times(2).WillRepeatedly(testing::Return());
    EXPECT_CALL(*mockPusher_, GetAggregationTemporality).WillRepeatedly(testing::Return(sdk::metrics::AggregationTemporality::DELTA));
    ASSERT_TRUE(observableRegistry_->GetCallbackIntervalMap().find(interval)->second.size() == 1);
    observableRegistry_->Observe(interval);
}
}
