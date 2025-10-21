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
#include "example_exporter.h"

#include <iostream>

#include "metrics/plugin/exporter.h"

namespace observability::test::plugin {
ExampleExporter::ExampleExporter(std::string config)
{
    std::cout << "ExampleExporter Construct: " << config << std::endl;
}

observability::exporters::metrics::ExportResult ExampleExporter::Export(
    const std::vector<observability::sdk::metrics::MetricData> &data) noexcept
{
    std::cout << "ExampleExporter::Export called: " << data[0].collectionTs.time_since_epoch().count() << std::endl;
    return observability::exporters::metrics::ExportResult::SUCCESS;
}

observability::sdk::metrics::AggregationTemporality ExampleExporter::GetAggregationTemporality(
    observability::sdk::metrics::InstrumentType instrumentType) const noexcept
{
    (void)instrumentType;
    return observability::sdk::metrics::AggregationTemporality::DELTA;
}

bool ExampleExporter::ForceFlush(std::chrono::microseconds timeout) noexcept
{
    (void)timeout;
    return true;
}

bool ExampleExporter::Shutdown(std::chrono::microseconds timeout) noexcept
{
    (void)timeout;
    return true;
}

void ExampleExporter::RegisterOnHealthChangeCb(const std::function<void(bool)>& /*onChange*/) noexcept
{
}

}  // namespace observability::test::plugin