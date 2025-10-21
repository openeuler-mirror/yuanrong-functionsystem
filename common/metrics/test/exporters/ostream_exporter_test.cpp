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

#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>
#include <utility>

#include "metrics/api/provider.h"
#include "metrics/plugin/dynamic_library_handle_unix.h"
#include "metrics/plugin/dynamic_load.h"
#include "metrics/sdk/immediately_export_processor.h"
#include "metrics/sdk/meter_provider.h"

using namespace observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;
namespace MetricsSDK = observability::sdk::metrics;

namespace observability::test::sdk {

class OstreamExporterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto mp = std::make_shared<MeterProvider>();
        std::string error;
        auto exporter = observability::plugin::metrics::LoadExporterFromLibrary(
            GetLibPath("libobservability-metrics-exporter-ostream.so"), "", error);

        MetricsSDK::ExportConfigs exportConfigs;
        exportConfigs.exporterName = "simpleExporter";
        exportConfigs.exportMode = MetricsSDK::ExportMode::IMMEDIATELY;
        exportConfigs.batchSize = 1;
        exportConfigs.failureQueueMaxSize = 2;
        exportConfigs.failureDataDir = GetCurrentPath();

        auto processor = std::make_unique<ImmediatelyExportProcessor>(std::move(exporter), exportConfigs);
        mp->AddMetricProcessor(std::move(processor));

        MetricsApi::Provider::SetMeterProvider(mp);
        auto provider = MetricsApi::Provider::GetMeterProvider();
        ASSERT_EQ(provider, mp);
        meter_ = provider->GetMeter("test");
    }

    void TearDown() override
    {
        meter_ = nullptr;
    }

    std::shared_ptr<MetricsApi::Meter> meter_ = nullptr;

private:
    std::string GetCurrentPath()
    {
        char path[1024];
        std::string curPath;
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len != -1) {
            path[len] = '\0';
            char *directoryPath = path;
            char *fileName = strrchr(path, '/');
            if (fileName) {
                *fileName = '\0';
            }
            curPath = std::string(directoryPath);
            std::cout << "Current Path: " << curPath << std::endl;
        }
        return curPath;
    }

    std::string GetLibPath(const std::string &libName)
    {
        std::string filePath = GetCurrentPath() + "/../lib/" + libName;
        std::cout << "filePath: " << filePath << std::endl;
        return filePath;
    }
};

TEST_F(OstreamExporterTest, UInt64CounterSet)
{
    auto counter =
        meter_->CreateUInt64Counter("total_requests_counter", "Total number of requests", "Number of Requests");

    uint64_t val = 1;
    counter->Set(val);
    EXPECT_EQ(counter->GetValue(), val);

    std::list<std::pair<std::string, std::string>> labels;
    labels.emplace_back(std::make_pair("component", "frontend"));

    val = 2;
    counter->Set(val, labels);
    EXPECT_EQ(counter->GetValue(), val);
    EXPECT_EQ(counter->GetLabels().front().first, "component");
    EXPECT_EQ(counter->GetLabels().front().second, "frontend");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::stringstream stdoutOutput;
    std::streambuf *sbuf = std::cout.rdbuf();
    std::cout.rdbuf(stdoutOutput.rdbuf());

    val = 3;
    labels.emplace_back(std::make_pair("instanceID", "aeode-1xd-5544sda"));
    counter->Set(val, labels, std::chrono::system_clock::now());
    EXPECT_EQ(counter->GetValue(), val);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout.rdbuf(sbuf);

    auto exporterJson = nlohmann::json::parse(stdoutOutput.str());
    EXPECT_EQ(exporterJson.at("Name"), "total_requests_counter");
    EXPECT_EQ(exporterJson.at("Description"), "Total number of requests");
    EXPECT_EQ(exporterJson.at("Unit"), "Number of Requests");
    EXPECT_EQ(exporterJson.at("Type"), "Counter");
    EXPECT_EQ(exporterJson.at("Data").at(0).at("Value"), val);
    EXPECT_EQ(exporterJson.at("Data").at(0).at("labels").at(0).at("component"), "frontend");
    EXPECT_EQ(exporterJson.at("Data").at(0).at("labels").at(0).at("instanceID"), "aeode-1xd-5544sda");
    std::cout << stdoutOutput.str() << std::endl;
}

TEST_F(OstreamExporterTest, UInt64CounterIncrement)
{
    auto counter =
        meter_->CreateUInt64Counter("total_requests_counter", "Total number of requests", "Number of Requests");

    uint64_t val = 99;
    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);

    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val * 2);
}

TEST_F(OstreamExporterTest, UInt64CounterReset)
{
    auto counter =
        meter_->CreateUInt64Counter("total_requests_counter", "Total number of requests", "Number of Requests");

    uint64_t val = 99;
    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);

    uint64_t expect = 0;
    counter->Reset();
    EXPECT_EQ(counter->GetValue(), expect);

    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);
}

TEST_F(OstreamExporterTest, UInt64CounterOperatorAdd)
{
    auto counter =
        meter_->CreateUInt64Counter("total_requests_counter", "Total number of requests", "Number of Requests");

    uint64_t val = 99;
    *(counter.get()) += val;
    EXPECT_EQ(counter->GetValue(), val);

    *(counter.get()) += val;
    EXPECT_EQ(counter->GetValue(), val * 2);
}

TEST_F(OstreamExporterTest, UInt64CounterOperatorSelfAdd)
{
    auto counter =
        meter_->CreateUInt64Counter("total_requests_counter", "Total number of requests", "Number of Requests");

    uint64_t expect = 1;
    ++(*counter);
    EXPECT_EQ(counter->GetValue(), expect);

    ++(*counter);
    EXPECT_EQ(counter->GetValue(), expect * 2);
}

TEST_F(OstreamExporterTest, DoubleCounterSet)
{
    auto counter =
        meter_->CreateDoubleCounter("total_requests_counter", "Total number of requests", "Number of Requests");

    double val = 1.0;
    counter->Set(val);
    EXPECT_EQ(counter->GetValue(), val);

    std::list<std::pair<std::string, std::string>> labels;
    labels.emplace_back(std::make_pair("component", "frontend"));

    val = 2.0;
    counter->Set(val, labels);
    EXPECT_EQ(counter->GetValue(), val);
    EXPECT_EQ(counter->GetLabels().front().first, "component");
    EXPECT_EQ(counter->GetLabels().front().second, "frontend");

    val = 3.0;
    labels.emplace_back(std::make_pair("instanceID", "aeode-1xd-5544sda"));
    counter->Set(val, labels, std::chrono::system_clock::now());
    EXPECT_EQ(counter->GetValue(), val);
}

TEST_F(OstreamExporterTest, DoubleCounterIncrement)
{
    auto counter =
        meter_->CreateDoubleCounter("total_requests_counter", "Total number of requests", "Number of Requests");

    double val = 99.0;
    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);

    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val * 2);
}

TEST_F(OstreamExporterTest, DoubleCounterReset)
{
    auto counter =
        meter_->CreateDoubleCounter("total_requests_counter", "Total number of requests", "Number of Requests");

    double val = 99;
    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);

    double expect = 0;
    counter->Reset();
    EXPECT_EQ(counter->GetValue(), expect);

    counter->Increment(val);
    EXPECT_EQ(counter->GetValue(), val);
}

TEST_F(OstreamExporterTest, DoubleCounterOperatorAdd)
{
    auto counter =
        meter_->CreateDoubleCounter("total_requests_counter", "Total number of requests", "Number of Requests");

    double val = 99;
    *(counter.get()) += val;
    EXPECT_EQ(counter->GetValue(), val);

    *(counter.get()) += val;
    EXPECT_EQ(counter->GetValue(), val * 2);
}

TEST_F(OstreamExporterTest, DoubleCounterOperatorSelfAdd)
{
    auto counter =
        meter_->CreateDoubleCounter("total_requests_counter", "Total number of requests", "Number of Requests");

    double expect = 1;
    ++(*counter);
    EXPECT_EQ(counter->GetValue(), expect);

    ++(*counter);
    EXPECT_EQ(counter->GetValue(), expect * 2);
}

}  // namespace observability::test::sdk