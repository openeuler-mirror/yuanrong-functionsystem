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
#include "exporters/file_exporter/include/file_exporter.h"

#include <gtest/gtest.h>

#include <iostream>
#include <thread>

#include "api/include/gauge.h"
#include "exporters/file_exporter/include/file_utils.h"
#include "sdk/include/storage.h"

namespace observability::test {

using namespace observability::metrics;

class FileExporterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        FileParam fileParam = { "/tmp/metrics", "file_exporter_test", 3, 3 };
        fileExporterPtr = std::make_shared<FileExporter>(fileParam);
    }

    void TearDown() override
    {
        EXPECT_TRUE(fileExporterPtr->ForceFlush());
        fileExporterPtr->Finalize();
        fileExporterPtr = nullptr;
        DeleteFile();
    }

    void DeleteFile()
    {
        std::vector<std::string> files;
        std::stringstream ss;
        ss << "/tmp/metrics/file_exporter_test"
           << "\\."
           << "*[0-9]\\.data"
           << "\\.gz";
        std::string pattern = ss.str();
        Glob(pattern, files);

        for (auto &file : files) {
            std::remove(file.c_str());
        }
        std::remove("/tmp/metrics/file_exporter_test.data");
    }

    std::shared_ptr<FileExporter> fileExporterPtr = nullptr;
};

void thread_example1(std::shared_ptr<observability::metrics::Gauge<double>> gauge)
{
    gauge->Set(rand() / double(RAND_MAX));
}

void thread_collect1(std::shared_ptr<observability::metrics::Storage> storage_,
                     std::vector<std::shared_ptr<observability::metrics::Gauge<double>>> gaugeList, const int interval,
                     std::shared_ptr<FileExporter> fileExporterPtr)
{
    for (int i = 0; i < 10 / interval; i++) {
        std::vector<std::thread> threads;
        for (uint64_t k = 0; k < gaugeList.size(); k++) {
            threads.emplace_back(&thread_example1, gaugeList[k]);
        }
        for (auto &thread : threads) {
            thread.join();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval * 1000));
        std::chrono::system_clock::time_point collect_time = std::chrono::system_clock::now();
        auto metricsDataList = storage_->Collect(collect_time, interval);
        std::cout << "----------------------------------" << std::endl;
        for (uint64_t j = 0; j < metricsDataList.size(); j++) {
            std::cout << "第" << i + 1 << "次collect:   name: " << metricsDataList[j].name
                      << ",value: " << std::get<double>(metricsDataList[j].metricValue) << std::endl;
        }
        fileExporterPtr->Export(metricsDataList);
    }
}

/**
 * Feature: Storage Metrics Data Export
 * Description: Test metrics data export from storage
 * Steps:
 * 1. Set two type interval in storage, add some gauges in them.
 * 2. Execute two thread, collect metrics data for interval with 1s and 5s.
 * 3. Use file Exporter to export then.
 * 4. Check the file output manually.
 */
TEST_F(FileExporterTest, StorageExport)
{
    auto storage_ = std::make_shared<observability::metrics::Storage>();
    std::vector<std::shared_ptr<observability::metrics::Gauge<double>>> gaugeList_1s;
    for (int i = 0; i < 5; i++) {
        int interval = 1;
        std::string name = "test_1s_" + std::to_string(i);
        auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
        storage_->AddMetric(gauge, interval);
        gaugeList_1s.push_back(gauge);
    }
    std::vector<std::shared_ptr<observability::metrics::Gauge<double>>> gaugeList_5s;
    for (int i = 0; i < 5; i++) {
        int interval = 5;
        std::string name = "test_5s_" + std::to_string(i);
        auto gauge = std::make_shared<observability::metrics::Gauge<double>>(name, name, name);
        storage_->AddMetric(gauge, interval);
        gaugeList_5s.push_back(gauge);
    }
    std::thread thread_collect_1(&thread_collect1, storage_, gaugeList_1s, 1, fileExporterPtr);
    std::thread thread_collect_5(&thread_collect1, storage_, gaugeList_5s, 5, fileExporterPtr);

    thread_collect_5.join();
    thread_collect_1.join();

    fileExporterPtr->ForceFlush();
}

/**
 * Feature: Single Metrics Data Export
 * Description: Test metrics data export simply once.
 * Steps:
 * 1. Create two metric data into a vector.
 * 2. Execute Export function to export data.
 */
TEST_F(FileExporterTest, SingleExport)
{
    observability::metrics::MetricsData metricsData = { .labels = {},
                                                        .name = "name1",
                                                        .description = "name1",
                                                        .unit = "name1",
                                                        .metricType = "Gauge",
                                                        .collectTimeStamp = std::chrono::system_clock::now(),
                                                        .metricValue = MetricValue(0.1) };

    observability::metrics::MetricsData metricsData2 = { .labels = {},
                                                         .name = "name2",
                                                         .description = "name2",
                                                         .unit = "name2",
                                                         .metricType = "Gauge",
                                                         .collectTimeStamp = std::chrono::system_clock::now(),
                                                         .metricValue = MetricValue(0.2) };
    std::vector<observability::metrics::MetricsData> metricsDataList = { metricsData, metricsData2 };
    fileExporterPtr->Export(metricsDataList);
}

}  // namespace observability::test