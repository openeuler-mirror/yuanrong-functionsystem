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

#include "metrics/exporters/opentelemetry_exporter/attribute_utils.h"
#include "metrics/sdk/metric_data.h"

namespace observability::test {
namespace MetricsSdk = observability::sdk::metrics;
namespace MetricsExporter = observability::exporters::metrics;

namespace {
MetricsSdk::InstrumentDescriptor BuildGaugeDescriptor(const std::string &name)
{
    return { name, "test descriptor", "", MetricsSdk::InstrumentType::GAUGE, MetricsSdk::InstrumentValueType::UINT64 };
}
}  // namespace

TEST(OpenTelemetryAttributeUtilsTest, FlattensJsonAlarmLabelToStructuredAttributes)
{
    MetricsSdk::PointLabels labels;
    labels.emplace_back(std::pair{ "yrAlarmLabelKey",
                                   R"({"id":"YuanrongEtcdConnection00001","name":"yr_etcd_alarm","severity":5,"locationInfo":"cn-north-7","cause":"connect failed","startsAt":1727611921601,"endsAt":0,"annotations":"{\"err_msg\":\"connect failed\"}","op_type":"firing"})" });
    labels.emplace_back(std::pair{ "node_id", "node-1" });

    auto attributes = MetricsExporter::BuildPointAttributes(BuildGaugeDescriptor("yr_etcd_alarm"), labels);

    EXPECT_EQ(attributes.at("yr.event.type"), "alarm");
    EXPECT_EQ(attributes.at("yr.alarm.id"), "YuanrongEtcdConnection00001");
    EXPECT_EQ(attributes.at("yr.alarm.name"), "yr_etcd_alarm");
    EXPECT_EQ(attributes.at("yr.alarm.severity"), "5");
    EXPECT_EQ(attributes.at("yr.alarm.location_info"), "cn-north-7");
    EXPECT_EQ(attributes.at("yr.alarm.cause"), "connect failed");
    EXPECT_EQ(attributes.at("yr.alarm.starts_at"), "1727611921601");
    EXPECT_EQ(attributes.at("yr.alarm.ends_at"), "0");
    EXPECT_EQ(attributes.at("yr.alarm.annotations"), R"({"err_msg":"connect failed"})");
    EXPECT_EQ(attributes.at("yr.alarm.op_type"), "firing");
    EXPECT_EQ(attributes.at("node_id"), "node-1");
    EXPECT_EQ(attributes.at("yrAlarmLabelKey"), labels.front().second);
}

TEST(OpenTelemetryAttributeUtilsTest, FlattensLegacyAlarmGaugeLabelsToStructuredAttributes)
{
    MetricsSdk::PointLabels labels;
    labels.emplace_back(std::pair{ "id", "InitStsSdkErr00001" });
    labels.emplace_back(std::pair{ "name", "InitStsSdkErr" });
    labels.emplace_back(std::pair{ "level", "major" });
    labels.emplace_back(std::pair{ "source_tag", "pod|1.2.3.4|cluster|InitStsSdkErr" });
    labels.emplace_back(std::pair{ "op_type", "firing" });
    labels.emplace_back(std::pair{ "details", "Init sts err: timeout" });
    labels.emplace_back(std::pair{ "clear_type", "ADAC" });
    labels.emplace_back(std::pair{ "start_timestamp", "1710000000000" });
    labels.emplace_back(std::pair{ "end_timestamp", "0" });
    labels.emplace_back(std::pair{ "site", "cn-north-7" });

    auto attributes = MetricsExporter::BuildPointAttributes(BuildGaugeDescriptor("alarm_meter_gauge"), labels);

    EXPECT_EQ(attributes.at("yr.event.type"), "alarm");
    EXPECT_EQ(attributes.at("yr.alarm.id"), "InitStsSdkErr00001");
    EXPECT_EQ(attributes.at("yr.alarm.name"), "InitStsSdkErr");
    EXPECT_EQ(attributes.at("yr.alarm.severity"), "major");
    EXPECT_EQ(attributes.at("yr.alarm.source_tag"), "pod|1.2.3.4|cluster|InitStsSdkErr");
    EXPECT_EQ(attributes.at("yr.alarm.op_type"), "firing");
    EXPECT_EQ(attributes.at("yr.alarm.details"), "Init sts err: timeout");
    EXPECT_EQ(attributes.at("yr.alarm.clear_type"), "ADAC");
    EXPECT_EQ(attributes.at("yr.alarm.starts_at"), "1710000000000");
    EXPECT_EQ(attributes.at("yr.alarm.ends_at"), "0");
    EXPECT_EQ(attributes.at("site"), "cn-north-7");
}

TEST(OpenTelemetryAttributeUtilsTest, KeepsInvalidAlarmPayloadAsRawAttribute)
{
    MetricsSdk::PointLabels labels;
    labels.emplace_back(std::pair{ "yrAlarmLabelKey", "{not-json}" });
    labels.emplace_back(std::pair{ "tenant_id", "tenant-1" });

    auto attributes = MetricsExporter::BuildPointAttributes(BuildGaugeDescriptor("yr_obs_alarm"), labels);

    EXPECT_EQ(attributes.at("yr.event.type"), "alarm");
    EXPECT_EQ(attributes.at("yr.alarm.raw"), "{not-json}");
    EXPECT_EQ(attributes.at("tenant_id"), "tenant-1");
    EXPECT_EQ(attributes.at("yrAlarmLabelKey"), "{not-json}");
    EXPECT_EQ(attributes.count("yr.alarm.name"), 0);
}

}  // namespace observability::test
