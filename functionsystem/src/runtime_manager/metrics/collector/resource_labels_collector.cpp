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

#include "resource_labels_collector.h"

#include <nlohmann/json.hpp>

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {

ResourceLabelsCollector::ResourceLabelsCollector(const std::string &resourceLabelPath)
    : BaseMetricsCollector(metrics_type::LABELS, collector_type::SYSTEM, nullptr), resourceLabelPath_(resourceLabelPath)
{
    // set to empty at beginning
    initLabelsCache_.initLabels = litebus::Option<std::unordered_map<std::string, std::string>>();
    std::unordered_map<std::string, std::string> initLabelsMap;
    GetInitLabelsFromEnv(initLabelsMap);
    GetNodeIDLabelsFromEnv(initLabelsMap);
    GetHostIPLabelsFromEnv(initLabelsMap);
    GetLabelsFromFile(initLabelsMap);
    if (!initLabelsMap.empty()) {
        initLabelsCache_.initLabels = initLabelsMap;
    }
}

void ResourceLabelsCollector::GetInitLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap)
{
    auto initLabelsOpt = litebus::os::GetEnv(INIT_LABELS_ENV_KEY);
    if (initLabelsOpt.IsNone()) {
        YRLOG_WARN("initLabel env doesn't exist, skip it");
        return;
    }
    auto initLabelStr = initLabelsOpt.Get();
    if (initLabelStr.empty()) {
        YRLOG_INFO("initLabel is empty, skip it");
        return;
    }

    nlohmann::json initLabelsJson;
    try {
        initLabelsJson = nlohmann::json::parse(initLabelStr);
    } catch (nlohmann::json::parse_error &e) {
        YRLOG_ERROR(
            "failed to parse init labels, maybe not a valid json, reason: {}, id: {}, byte position: {}. Origin "
            "string: {}",
            e.what(), e.id, e.byte, initLabelStr);
        return;
    }

    for (auto &it : initLabelsJson.items()) {
        YRLOG_INFO("collected init label {}: {} from env", it.key(), initLabelsJson.at(it.key()).dump());
        labelsMap[it.key()] = initLabelsJson.at(it.key());
    }
}

void ResourceLabelsCollector::GetNodeIDLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap)
{
    auto nodeIDOpt = litebus::os::GetEnv(NODE_ID_LABEL_KEY);
    if (nodeIDOpt.IsNone()) {
        YRLOG_WARN("nodeID env doesn't exist, skip it");
        return;
    }

    auto nodeID = nodeIDOpt.Get();
    if (nodeID.empty()) {
        YRLOG_INFO("nodeID is empty, skip it");
        return;
    }

    YRLOG_INFO("collected init label {}: {} from env", NODE_ID_LABEL_KEY, nodeID);
    labelsMap[NODE_ID_LABEL_KEY] = nodeID;
}

void ResourceLabelsCollector::GetHostIPLabelsFromEnv(std::unordered_map<std::string, std::string> &labelsMap)
{
    auto hostIPOpt = litebus::os::GetEnv(HOST_IP_LABEL_KEY);
    if (hostIPOpt.IsNone()) {
        YRLOG_WARN("HostIP env doesn't exist, skip it");
        return;
    }

    auto hostIP = hostIPOpt.Get();
    if (hostIP.empty()) {
        YRLOG_INFO("hostIP is empty, skip it");
        return;
    }

    YRLOG_INFO("collected init label {}: {} from env", HOST_IP_LABEL_KEY, hostIP);
    labelsMap[HOST_IP_LABEL_KEY] = hostIP;
}

void ResourceLabelsCollector::GetLabelsFromFile(std::unordered_map<std::string, std::string> &labelsMap)
{
    if (!litebus::os::ExistPath(resourceLabelPath_)) {
        YRLOG_DEBUG("pod label path({}) not exist, skip getting labels from file", resourceLabelPath_);
        return;
    }

    auto podLabelsStr = litebus::os::Read(resourceLabelPath_);
    if (podLabelsStr.IsNone()) {
        YRLOG_WARN("failed to read labels from {}", resourceLabelPath_);
        return;
    }

    auto labelPairs = litebus::strings::Split(podLabelsStr.Get(), "\n");
    for (const auto &labelPair : labelPairs) {
        // key="value"
        auto splitPos = labelPair.find('=', 0);
        if (splitPos == std::string::npos) {
            YRLOG_ERROR("invalid label pair({})", labelPair);
            continue;
        }

        auto key = labelPair.substr(0, splitPos);
        auto valueWithQuote = labelPair.substr(splitPos + 1);
        if (valueWithQuote.empty() || valueWithQuote.at(0) != '"' ||
            valueWithQuote.at(valueWithQuote.size() - 1) != '"') {
            YRLOG_ERROR("invalid label value({})", valueWithQuote);
            continue;
        }
        auto value = valueWithQuote.substr(1, valueWithQuote.size() - 2);
        YRLOG_INFO("collected init label {}: {} from file", key, value);
        labelsMap[key] = value;
    }
}

std::string ResourceLabelsCollector::GenFilter() const
{
    // system-owner
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

litebus::Future<Metric> ResourceLabelsCollector::GetUsage() const
{
    return initLabelsCache_;
}

Metric ResourceLabelsCollector::GetLimit() const
{
    return initLabelsCache_;
}

}  // namespace functionsystem::runtime_manager
