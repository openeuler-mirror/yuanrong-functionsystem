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

#ifndef OBSERVABILITY_TEST_PLUGIN_EXAMPLE_EXPORTER_H
#define OBSERVABILITY_TEST_PLUGIN_EXAMPLE_EXPORTER_H
#include <memory>

#include "metrics/plugin/exporter.h"
namespace observability::test::plugin {

class ExampleExporter final : public observability::exporters::metrics::Exporter,
                              public std::enable_shared_from_this<ExampleExporter> {
public:
    explicit ExampleExporter(std::string config);

    observability::exporters::metrics::ExportResult Export(
        const std::vector<observability::sdk::metrics::MetricData> &data) noexcept override;

    observability::sdk::metrics::AggregationTemporality GetAggregationTemporality(
        observability::sdk::metrics::InstrumentType instrumentType) const noexcept override;

    bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

    bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept override;

    void RegisterOnHealthChangeCb(const std::function<void(bool)>& /*onChange*/) noexcept override;

    ~ExampleExporter() override = default;
};
}  // namespace observability::test::plugin
#endif