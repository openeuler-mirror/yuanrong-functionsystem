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

#include <memory>
#include <nlohmann/json.hpp>
#include <utility>

#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_library_handle_unix.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/meter_provider.h"

using namespace observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;

namespace observability::test::sdk {

class MeterProviderSDKTest : public ::testing::Test {};

static std::string GetLibPath(const std::string &libName)
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
        filePath = std::string(directoryPath) + "/../lib/" + libName;
        std::cout << "filePath: " << filePath << std::endl;
    }
    return filePath;
}

static std::string GetLibraryPath()
{
    const std::string libName = "libobservability-metrics-file-exporter.so";
    return GetLibPath(libName);
}

TEST_F(MeterProviderSDKTest, FileExportStandard)
{
    auto mp = std::make_shared<MeterProvider>();

    nlohmann::json jsonConfig;
    jsonConfig["path"] = "/tmp";
    jsonConfig["fileName"] = "metrics_standard_test.data";
    jsonConfig["maxFiles"] = 2;
    jsonConfig["maxSize"] = 1000;
    jsonConfig["contentType"] = 0;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto processor = std::make_unique<ImmediatelyExportProcessor>(std::move(exporter));
    mp->AddMetricProcessor(std::move(processor));

    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);
    auto meter = provider->GetMeter("test");
    auto longGauge = meter->CreateUInt64Gauge("test_gauge", "description", "k");

    longGauge->Set(1);
    std::list<std::pair<std::string, std::string>> labels;
    labels.emplace_back(std::make_pair("id", "metrics_id"));
    labels.emplace_back(std::make_pair("name", "metrics_name"));
    labels.emplace_back(std::make_pair("level", "critical"));
    longGauge->Set(2, labels);
}

TEST_F(MeterProviderSDKTest, FileExportLabels)
{
    auto mp = std::make_shared<MeterProvider>();

    nlohmann::json jsonConfig;
    jsonConfig["path"] = "/tmp";
    jsonConfig["fileName"] = "metrics_labels_test.data";
    jsonConfig["maxFiles"] = 2;
    jsonConfig["maxSize"] = 1000;
    jsonConfig["contentType"] = 1;
    std::string error;
    auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto processor = std::make_unique<ImmediatelyExportProcessor>(std::move(exporter));
    mp->AddMetricProcessor(std::move(processor));

    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);
    auto meter = provider->GetMeter("test");
    auto longGauge = meter->CreateUInt64Gauge("test_gauge");

    std::list<std::pair<std::string, std::string>> labels;
    labels.emplace_back(std::make_pair("id", "metrics_id"));
    labels.emplace_back(std::make_pair("name", "metrics_name"));
    labels.emplace_back(std::make_pair("level", "critical"));
    longGauge->Set(0, labels);
}

TEST_F(MeterProviderSDKTest, MultiFileExport)
{
    auto mp = std::make_shared<MeterProvider>();

    nlohmann::json jsonConfig;
    jsonConfig["path"] = "/tmp";
    jsonConfig["fileName"] = "multi_metrics_standard_test.data";
    jsonConfig["maxFiles"] = 2;
    jsonConfig["maxSize"] = 1000;
    jsonConfig["contentType"] = 0;
    std::string error;
    auto exporter1 =
        observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto processor1 = std::make_unique<ImmediatelyExportProcessor>(std::move(exporter1));
    mp->AddMetricProcessor(std::move(processor1));

    jsonConfig["path"] = "/tmp";
    jsonConfig["fileName"] = "multi_metrics_labels_test.data";
    jsonConfig["maxFiles"] = 2;
    jsonConfig["maxSize"] = 1000;
    jsonConfig["contentType"] = 1;
    auto exporter2 =
        observability::plugin::metrics::LoadExporterFromLibrary(GetLibraryPath(), jsonConfig.dump(), error);
    auto processor2 = std::make_unique<ImmediatelyExportProcessor>(std::move(exporter2));
    mp->AddMetricProcessor(std::move(processor2));

    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);
    auto meter = provider->GetMeter("test");
    auto longGauge = meter->CreateUInt64Gauge("test_gauge");

    std::list<std::pair<std::string, std::string>> labels1;
    labels1.emplace_back(std::make_pair("id", "metrics_id1"));
    labels1.emplace_back(std::make_pair("name", "metrics_name1"));
    labels1.emplace_back(std::make_pair("level", "critical"));
    longGauge->Set(0, labels1);

    std::list<std::pair<std::string, std::string>> labels2;
    labels2.emplace_back(std::make_pair("id", "metrics_id2"));
    labels2.emplace_back(std::make_pair("name", "metrics_name2"));
    labels2.emplace_back(std::make_pair("level", "critical"));
    longGauge->Set(1, labels2);
}

TEST_F(MeterProviderSDKTest, GetMeters)
{
    auto mp = std::make_shared<MeterProvider>();
    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    EXPECT_EQ(provider, mp);

    EXPECT_NE(provider->GetMeter(""), nullptr);
    EXPECT_NE(provider->GetMeter("meter_one"), nullptr);
    EXPECT_NE(provider->GetMeter("meter_two"), nullptr);

    EXPECT_EQ(provider->GetMeter("meter_one"), provider->GetMeter("meter_one"));
    EXPECT_NE(provider->GetMeter("meter_one"), provider->GetMeter("meter_two"));
}

}  // namespace observability::test::sdk