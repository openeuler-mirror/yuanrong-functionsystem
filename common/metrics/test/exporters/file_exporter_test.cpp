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

#include <glob.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <thread>

#include <utils/os_utils.hpp>

#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/batch_export_processor.h"
#include "metrics/sdk/meter_provider.h"
#include "metrics/sdk/metric_processor.h"

namespace observability::test::exporter {
namespace MetricsApi = observability::api::metrics;
namespace MetricsSDK = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

const sdk::metrics::InstrumentDescriptor instrumentDescriptor = sdk::metrics::InstrumentDescriptor {
    .name = "test_metric",
    .description = "test metric desc",
    .unit = "ms",
    .type = sdk::metrics::InstrumentType::COUNTER,
    .valueType = sdk::metrics::InstrumentValueType::DOUBLE };
const std::list<std::pair<std::string, std::string>> pointLabels1 = { std::pair {"instance_id", "ins001"},
                                                                      std::pair {"job_id", "job001"} };
const std::vector<MetricsSDK::PointData> pointData = { { .labels = pointLabels1, .value = (double)10 } };
const MetricsSDK::MetricData metricData = { .instrumentDescriptor = instrumentDescriptor,
                                            .aggregationTemporality = sdk::metrics::AggregationTemporality::UNSPECIFIED,
                                            .collectionTs = std::chrono::system_clock::now(), .pointData = pointData };
void Glob(const std::string &pathPattern, std::vector<std::string> &paths)
{
    glob_t result;

    int ret = glob(pathPattern.c_str(), GLOB_TILDE | 1, nullptr, &result);
    switch (ret) {
        case 0:
            break;
        case GLOB_NOMATCH:
            globfree(&result);
            return;
        case GLOB_NOSPACE:
            globfree(&result);
            std::cerr << "failed to glob files, reason: out of memory." << std::endl;
            return;
        default:
            globfree(&result);
            std::cerr << "failed to glob files, pattern: " << pathPattern << ", errno:" << ret;
            return;
    }

    for (size_t i = 0; i < result.gl_pathc; ++i) {
        (void)paths.emplace_back(result.gl_pathv[i]);
    }

    globfree(&result);
    return;
};

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

void GenDir(const std::string &path)
{
    if (!litebus::os::ExistPath(path)) {
        litebus::os::Mkdir(path);
    }
}

TEST(FileExporterTest, ExportWithoutRolling)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    GenDir(dir);
    auto mp = std::make_shared<MetricsSDK::MeterProvider>();
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_without_rolling.data";
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 10;
    jsonConfig["contentType"] = "LABELS";
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto exportConfigs = MetricsSDK::ExportConfigs{ .exporterName = "simpleExporter", .exportMode = MetricsSDK::ExportMode::IMMEDIATELY };
    auto processor = std::make_unique<MetricsSDK::ImmediatelyExportProcessor>(std::move(exporter), exportConfigs);
    mp->AddMetricProcessor(std::move(processor));
    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    auto meter = provider->GetMeter("FileExporterTest");
    auto longGauge = meter->CreateUInt64Gauge("test_gauge", "description", "m");

    for (int i = 0; i < 1000; i++) {
        std::list<std::pair<std::string, std::string>> labels;
        labels.emplace_back(std::make_pair("id", "metrics_id"));
        labels.emplace_back(std::make_pair("name", "metrics_name"));
        labels.emplace_back(std::make_pair("level", "critical"));
        longGauge->Set(i, labels);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::string> tarFiles;
    std::stringstream ss;
    ss << dir << "/file_exporter_without_rolling"
       << "\\."
       << "*[0-9]\\.data\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, tarFiles);

    size_t target = 0;
    EXPECT_EQ(target, tarFiles.size());
    litebus::os::Rmdir(dir);
}

TEST(FileExporterTest, ExportWithRolling)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    GenDir(dir);
    auto mp = std::make_shared<MetricsSDK::MeterProvider>();
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_with_rolling.data";
    jsonConfig["rolling"]["enable"] = true;
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 1;
    jsonConfig["rolling"]["compress"] = true;
    jsonConfig["contentType"] = "STANDARD";
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto exportConfigs = MetricsSDK::ExportConfigs{ .exporterName = "simpleExporter", .exportMode = MetricsSDK::ExportMode::BATCH };
    auto processor = std::make_unique<MetricsSDK::BatchExportProcessor>(std::move(exporter), exportConfigs);
    mp->AddMetricProcessor(std::move(processor));

    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    auto meter = provider->GetMeter("FileExporterTest");

    auto longGauge = meter->CreateUInt64Gauge("test_gauge", "description", "m");
    for (int i = 0; i < 20000; i++) {
        std::list<std::pair<std::string, std::string>> labels;
        labels.emplace_back(std::make_pair("id", "metrics_id"));
        labels.emplace_back(std::make_pair("name", "metrics_name"));
        labels.emplace_back(std::make_pair("level", "critical"));
        longGauge->Set(i, labels);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::string> tarFiles;
    std::stringstream ss;
    ss << dir << "/file_exporter_with_rolling"
       << "\\."
       << "*[0-9]\\.data\\.gz";
    std::string pattern = ss.str();
    Glob(pattern, tarFiles);

    size_t target = 2;
    EXPECT_EQ(target, tarFiles.size());
    litebus::os::Rmdir(dir);
}

TEST(FileExporterTest, ExportWithoutWriting)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    GenDir(dir);
    auto mp = std::make_shared<MetricsSDK::MeterProvider>();
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_without_write.data";
    jsonConfig["rolling"]["enable"] = true;
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 1024 * 10;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto exportConfigs = MetricsSDK::ExportConfigs{ .exporterName = "simpleExporter", .exportMode = MetricsSDK::ExportMode::IMMEDIATELY };
    auto processor = std::make_unique<MetricsSDK::ImmediatelyExportProcessor>(std::move(exporter), exportConfigs);
    mp->AddMetricProcessor(std::move(processor));

    MetricsApi::Provider::SetMeterProvider(mp);
    litebus::os::Rmdir(dir);
}
TEST(FileExporterTest, Export)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    GenDir(dir);
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_without_write.data";
    jsonConfig["rolling"]["enable"] = true;
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 1024 * 10;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);

    std::vector<MetricsSDK::MetricData> vec = { metricData };
    auto res = exporter->Export(vec);
    EXPECT_EQ(res, observability::exporters::metrics::ExportResult::SUCCESS);
    litebus::os::Rmdir(dir);
}

TEST(FileExporterTest, GetAggregationTemporality)
{
    const std::string dir = "/metrics_test";
    litebus::os::Rmdir(dir);
    GenDir(dir);
    nlohmann::json jsonConfig;
    jsonConfig["fileDir"] = dir;
    jsonConfig["fileName"] = "file_exporter_without_write.data";
    jsonConfig["rolling"]["enable"] = true;
    jsonConfig["rolling"]["maxFiles"] = 3;
    jsonConfig["rolling"]["maxSize"] = 1024 * 10;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto res = exporter->GetAggregationTemporality(sdk::metrics::InstrumentType::GAUGE);
    EXPECT_EQ(res, sdk::metrics::AggregationTemporality::DELTA);

    auto resFlush = exporter->ForceFlush(std::chrono::microseconds());
    EXPECT_TRUE(resFlush);
    auto resShutdown = exporter->Shutdown(std::chrono::microseconds());
    EXPECT_TRUE(resShutdown);
    litebus::os::Rmdir(dir);
}

}  // namespace observability::test::exporter
