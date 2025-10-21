/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
*/

#ifndef OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H
#define OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H

#include "metrics/sdk/metric_pusher.h"

#include "gmock/gmock.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;

class MockPusher : public sdk::metrics::PusherHandle {
public:
    MockPusher() {};
    ~MockPusher() = default;
    MOCK_METHOD(void, Push, (const sdk::metrics::MetricData &metricData), (override, noexcept));
    MOCK_METHOD(MetricsSdk::AggregationTemporality, GetAggregationTemporality, (MetricsSdk::InstrumentType instrumentType), (override, noexcept));
};

}
#endif // OBSERVABILITY_METRICS_TEST_MOCK_PUSHER_H