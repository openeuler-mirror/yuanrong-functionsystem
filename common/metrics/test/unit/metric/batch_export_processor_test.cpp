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

#include <nlohmann/json.hpp>

#include <utils/os_utils.hpp>

#include "metrics/api/provider.h"
#include "metrics/sdk/batch_export_processor.h"
#include "metrics/sdk/meter_provider.h"
#include "metrics/plugin/dynamic_load.h"

using namespace observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;

namespace observability::test {

class BatchExportProcessorTest : public ::testing::Test {};

static std::string GetLibraryPath()
{
    char path[1024];
    std::string filePath;
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        char *directoryPath = path;
        char *fileName = strrchr(path, '/');
        if (fileName) {
            *fileName = '\0';
        }
        filePath = std::string(directoryPath) + "/../lib/libobservability-metrics-file-exporter.so";
        std::cout << "filePath: " << filePath << std::endl;
    }
    return filePath;
}

TEST_F(BatchExportProcessorTest, batchProcessor)
{
    std::string filePath = "/tmp";
    litebus::os::Rmdir(filePath);
    auto mp = std::make_shared<MeterProvider>();
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = filePath;
    jsonConfig["fileName"] = "metrics_standard_test.data";
    jsonConfig["maxFiles"] = 2;
    jsonConfig["maxSize"] = 1000;
    jsonConfig["contentType"] = 0;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto exportConfigs = ExportConfigs{ .exporterName = "batchExporter", .exportMode = ExportMode::BATCH };
    auto processor = std::make_unique<BatchExportProcessor>(std::move(exporter), exportConfigs);
    processor->GetAggregationTemporality(InstrumentType::COUNTER);
    mp->AddMetricProcessor(std::move(processor));
    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);
}

}  // namespace observability::test::exporter
