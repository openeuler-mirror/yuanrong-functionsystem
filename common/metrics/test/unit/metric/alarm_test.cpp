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
#include <utils/os_utils.hpp>
#include <fstream>

#include <nlohmann/json.hpp>

#include "metrics/api/provider.h"
#include "metrics/sdk/metric_data.h"
#include "metrics/sdk/immediately_export_processor.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsApi = observability::api::metrics;
namespace MetricsExporter = observability::exporters::metrics;

class MockProcessor : public sdk::metrics::MetricPushProcessor {
public:
    explicit MockProcessor() {};
    ~MockProcessor() override = default;

    sdk::metrics::AggregationTemporality GetAggregationTemporality(
        sdk::metrics::InstrumentType /* instrumentType */) const noexcept override {
        return observability::sdk::metrics::AggregationTemporality::DELTA;
    };

    void Export(const sdk::metrics::MetricData &data) noexcept override
    {
        metricData_ = data;
    }
    MetricsSdk::MetricData metricData_;
};

class AlarmTest : public ::testing::Test {};

TEST_F(AlarmTest, CreateAndSetAlarm)
{
    auto mp = std::make_shared<MetricsSdk::MeterProvider>();
    auto mockProcessor = std::make_shared<MockProcessor>();
    mp->AddMetricProcessor(mockProcessor);
    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    auto meter = provider->GetMeter("FileExporterTest");
    auto aomAlarm = meter->CreateAlarm("test_alarm", "alarm test");
    EXPECT_NE(aomAlarm, nullptr);

    MetricsApi::AlarmInfo alarmInfo1;
    alarmInfo1.alarmName = "etcd_alarm";
    alarmInfo1.alarmSeverity = MetricsApi::AlarmSeverity::CRITICAL;
    alarmInfo1.cause = "etcd err";
    alarmInfo1.locationInfo = "cn-north-7, 192.0.0.1";
    alarmInfo1.startsAt = static_cast<long>(1727611921601);
    alarmInfo1.endsAt = static_cast<long>(1727611929601);
    nlohmann::json annotationJson;
    annotationJson["alarm_probableCause_zh_cn"] = "可能原因";
    annotationJson["alarm_fix_suggestion_zh_cn"] = "修复建议";
    alarmInfo1.customOptions["annotation"] = annotationJson.dump();

    aomAlarm->Set(alarmInfo1);
    EXPECT_EQ(mockProcessor->metricData_.instrumentDescriptor.name, "test_alarm");
    EXPECT_EQ(mockProcessor->metricData_.instrumentDescriptor.description, "alarm test");
    EXPECT_EQ(mockProcessor->metricData_.pointData.begin()->labels.size(), 1);
    auto alarmInfo1Json = nlohmann::json::parse(mockProcessor->metricData_.pointData.begin()->labels.begin()->second);
    EXPECT_EQ(alarmInfo1Json.at("name"), "etcd_alarm");
    EXPECT_EQ(alarmInfo1Json.at("severity"), 5);
    EXPECT_EQ(alarmInfo1Json.at("cause"), "etcd err");
    EXPECT_EQ(alarmInfo1Json.at("locationInfo"), "cn-north-7, 192.0.0.1");
    EXPECT_EQ(alarmInfo1Json.at("startsAt"), 1727611921601);
    EXPECT_EQ(alarmInfo1Json.at("endsAt"), 1727611929601);
    auto annotation1Json = nlohmann::json::parse(static_cast<std::string>(alarmInfo1Json.at("annotation")));
    EXPECT_EQ(annotation1Json.at("alarm_fix_suggestion_zh_cn"), "修复建议");
    EXPECT_EQ(annotation1Json.at("alarm_probableCause_zh_cn"), "可能原因");
}

TEST_F(AlarmTest, CreateAndSetEmptyAlarm)
{
    auto mp = std::make_shared<MetricsSdk::MeterProvider>();
    auto mockProcessor = std::make_shared<MockProcessor>();
    mp->AddMetricProcessor(mockProcessor);
    MetricsApi::Provider::SetMeterProvider(mp);
    auto provider = MetricsApi::Provider::GetMeterProvider();
    auto meter = provider->GetMeter("FileExporterTest");
    auto aomAlarm = meter->CreateAlarm("test_alarm", "alarm test");
    EXPECT_NE(aomAlarm, nullptr);

    MetricsApi::AlarmInfo alarmInfo1;

    aomAlarm->Set(alarmInfo1);
    EXPECT_EQ(mockProcessor->metricData_.instrumentDescriptor.name, "test_alarm");
    EXPECT_EQ(mockProcessor->metricData_.instrumentDescriptor.description, "alarm test");
    EXPECT_EQ(mockProcessor->metricData_.pointData.begin()->labels.size(), 1);
    auto alarmInfo1Json = nlohmann::json::parse(mockProcessor->metricData_.pointData.begin()->labels.begin()->second);
    EXPECT_TRUE(alarmInfo1Json.count("name") == 0);
    EXPECT_EQ(alarmInfo1Json.at("severity"), 0);
    EXPECT_TRUE(alarmInfo1Json.count("cause") == 0);
    EXPECT_TRUE(alarmInfo1Json.count("locationInfo") == 0);
    EXPECT_TRUE(alarmInfo1Json.count("startsAt") == 0);
    EXPECT_TRUE(alarmInfo1Json.count("endsAt") == 0);
}
}