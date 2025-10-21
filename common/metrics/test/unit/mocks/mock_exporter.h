/*
* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
*/

#ifndef OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H
#define OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H

#include "metrics/exporters/exporter.h"

#include "gmock/gmock.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

class MockExporter : public MetricsExporter::Exporter {
public:
    MockExporter() {};
   ~MockExporter() = default;
   MOCK_METHOD(MetricsExporter::ExportResult, Export, (const std::vector<MetricsSdk::MetricData> &metricData), (override, noexcept));
   MOCK_METHOD(MetricsSdk::AggregationTemporality, GetAggregationTemporality, (MetricsSdk::InstrumentType instrumentType), (override, const, noexcept));
   MOCK_METHOD(bool, ForceFlush, (std::chrono::microseconds timeout), (override, noexcept));
   MOCK_METHOD(bool, Shutdown, (std::chrono::microseconds timeout), (override, noexcept));
   MOCK_METHOD(void, RegisterOnHealthChangeCb, (const std::function<void (bool)> &onChange), (override, noexcept));
};

}
#endif // OBSERVABILITY_METRICS_TEST_MOCK_EXPORTER_H