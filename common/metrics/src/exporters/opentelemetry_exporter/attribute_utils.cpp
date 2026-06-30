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

#include "metrics/exporters/opentelemetry_exporter/attribute_utils.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "common/include/constant.h"

namespace observability::exporters::metrics {
namespace {
constexpr char LEGACY_ALARM_GAUGE_NAME[] = "alarm_meter_gauge";
constexpr char EVENT_TYPE_KEY[] = "yr.event.type";
constexpr char EVENT_TYPE_ALARM[] = "alarm";

constexpr char ALARM_ID_KEY[] = "yr.alarm.id";
constexpr char ALARM_NAME_KEY[] = "yr.alarm.name";
constexpr char ALARM_SEVERITY_KEY[] = "yr.alarm.severity";
constexpr char ALARM_LOCATION_INFO_KEY[] = "yr.alarm.location_info";
constexpr char ALARM_CAUSE_KEY[] = "yr.alarm.cause";
constexpr char ALARM_STARTS_AT_KEY[] = "yr.alarm.starts_at";
constexpr char ALARM_ENDS_AT_KEY[] = "yr.alarm.ends_at";
constexpr char ALARM_TIMEOUT_KEY[] = "yr.alarm.timeout";
constexpr char ALARM_RAW_KEY[] = "yr.alarm.raw";
constexpr char ALARM_ATTR_PREFIX[] = "yr.alarm.";

std::string JsonValueToString(const nlohmann::json &value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float()) {
        return value.dump();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

void InsertIfNotEmpty(std::map<std::string, std::string> &attributes, const std::string &key, const std::string &value)
{
    if (!value.empty()) {
        attributes[key] = value;
    }
}

void FillJsonAlarmAttributes(const nlohmann::json &alarmJson, std::map<std::string, std::string> &attributes)
{
    attributes[EVENT_TYPE_KEY] = EVENT_TYPE_ALARM;
    if (alarmJson.contains("id")) {
        InsertIfNotEmpty(attributes, ALARM_ID_KEY, JsonValueToString(alarmJson.at("id")));
    }
    if (alarmJson.contains("name")) {
        InsertIfNotEmpty(attributes, ALARM_NAME_KEY, JsonValueToString(alarmJson.at("name")));
    }
    if (alarmJson.contains("severity")) {
        InsertIfNotEmpty(attributes, ALARM_SEVERITY_KEY, JsonValueToString(alarmJson.at("severity")));
    }
    if (alarmJson.contains("locationInfo")) {
        InsertIfNotEmpty(attributes, ALARM_LOCATION_INFO_KEY, JsonValueToString(alarmJson.at("locationInfo")));
    }
    if (alarmJson.contains("cause")) {
        InsertIfNotEmpty(attributes, ALARM_CAUSE_KEY, JsonValueToString(alarmJson.at("cause")));
    }
    if (alarmJson.contains("startsAt")) {
        InsertIfNotEmpty(attributes, ALARM_STARTS_AT_KEY, JsonValueToString(alarmJson.at("startsAt")));
    }
    if (alarmJson.contains("endsAt")) {
        InsertIfNotEmpty(attributes, ALARM_ENDS_AT_KEY, JsonValueToString(alarmJson.at("endsAt")));
    }
    if (alarmJson.contains("timeout")) {
        InsertIfNotEmpty(attributes, ALARM_TIMEOUT_KEY, JsonValueToString(alarmJson.at("timeout")));
    }

    for (const auto &[key, value] : alarmJson.items()) {
        if (key == "id" || key == "name" || key == "severity" || key == "locationInfo" || key == "cause" ||
            key == "startsAt" || key == "endsAt" || key == "timeout") {
            continue;
        }
        InsertIfNotEmpty(attributes, std::string(ALARM_ATTR_PREFIX) + key, JsonValueToString(value));
    }
}

void FillLegacyAlarmAttributes(const observability::sdk::metrics::PointLabels &labels,
                               std::map<std::string, std::string> &attributes)
{
    attributes[EVENT_TYPE_KEY] = EVENT_TYPE_ALARM;
    for (const auto &[key, value] : labels) {
        if (key == "id") {
            InsertIfNotEmpty(attributes, ALARM_ID_KEY, value);
        } else if (key == "name") {
            InsertIfNotEmpty(attributes, ALARM_NAME_KEY, value);
        } else if (key == "level") {
            InsertIfNotEmpty(attributes, ALARM_SEVERITY_KEY, value);
        } else if (key == "start_timestamp") {
            InsertIfNotEmpty(attributes, ALARM_STARTS_AT_KEY, value);
        } else if (key == "end_timestamp") {
            InsertIfNotEmpty(attributes, ALARM_ENDS_AT_KEY, value);
        } else {
            InsertIfNotEmpty(attributes, std::string(ALARM_ATTR_PREFIX) + key, value);
        }
    }
}
}  // namespace

std::map<std::string, std::string> BuildPointAttributes(
    const observability::sdk::metrics::InstrumentDescriptor &descriptor,
    const observability::sdk::metrics::PointLabels &labels)
{
    std::map<std::string, std::string> attributes;
    for (const auto &[key, value] : labels) {
        attributes[key] = value;
    }

    const auto alarmLabelIt = std::find_if(labels.begin(), labels.end(), [](const auto &label) {
        return label.first == observability::metrics::ALARM_LABEL_KEY;
    });
    if (alarmLabelIt != labels.end()) {
        attributes[EVENT_TYPE_KEY] = EVENT_TYPE_ALARM;
        try {
            FillJsonAlarmAttributes(nlohmann::json::parse(alarmLabelIt->second), attributes);
        } catch (const std::exception &) {
            InsertIfNotEmpty(attributes, ALARM_RAW_KEY, alarmLabelIt->second);
        }
        return attributes;
    }

    if (descriptor.name == LEGACY_ALARM_GAUGE_NAME) {
        FillLegacyAlarmAttributes(labels, attributes);
    }
    return attributes;
}

}  // namespace observability::exporters::metrics
