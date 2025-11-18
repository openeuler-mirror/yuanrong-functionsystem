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

#include "metrics_context.h"

#include <nlohmann/json.hpp>

#include "common/constants/constants.h"
#include "common/logs/logging.h"

namespace functionsystem {
namespace metrics {
const std::vector<std::string> SYSTEM_FUNCTION_KEYWORD = { "faasscheduler", "faasfrontend", "faascontroller",
                                                           "faasmanager" };

std::string MetricsContext::GetAttr(const std::string &attr) const
{
    auto it = attribute_.find(attr);
    if (it != attribute_.end()) {
        return it->second;
    }
    return "";
}
void MetricsContext::SetAttr(const std::string &attr, const std::string &value)
{
    attribute_.insert_or_assign(attr, value);
}

void MetricsContext::SetEnabledInstruments(const std::unordered_set<YRInstrument> &enabledInstruments)
{
    enabledInstruments_ = enabledInstruments;
}

const BillingInvokeOption &MetricsContext::GetBillingInvokeOption(const std::string &requestID)
{
    std::lock_guard<std::mutex> lock(invokeMtx_);
    auto it = billingInvokeOptionsMap_.find(requestID);
    if (it != billingInvokeOptionsMap_.end()) {
        return it->second;
    }
    return billingInvokeOptionsMap_[requestID];
}

void MetricsContext::SetBillingInvokeOptions(const std::string &requestID,
                                             const std::map<std::string, std::string> &invokeOptions,
                                             const std::string &functionName, const std::string &instanceID)
{
    if (enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        return;
    }
    YRLOG_DEBUG("set billing invoke options of function: {}", functionName);
    std::lock_guard<std::mutex> lock(invokeMtx_);
    BillingInvokeOption billingInvokeOption;
    billingInvokeOption.functionName = functionName;
    billingInvokeOption.invokeOptions = invokeOptions;
    billingInvokeOption.instanceID = instanceID;
    billingInvokeOptionsMap_[requestID] = billingInvokeOption;

    auto it = billingInstanceMap_.find(instanceID);
    if (it == billingInstanceMap_.end()) {
        YRLOG_WARN("Can not set instance invoke requestID because {} not found in billingInstanceMap", instanceID);
        return;
    }
    it->second.invokeRequestId = requestID;
}

void MetricsContext::SetBillingSchedulingExtensions(const std::map<std::string, std::string> &schedulingExtensions,
                                                    const std::string &instanceID)
{
    if (enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        return;
    }
    YRLOG_DEBUG("set billing scheduling extensions of instance: {}", instanceID);
    std::lock_guard<std::mutex> lock(functionMtx_);
    auto it = billingFunctionOptionsMap_.find(instanceID);
    if (it != billingFunctionOptionsMap_.end()) {
        it->second.schedulingExtensions = schedulingExtensions;
        return;
    }
    BillingFunctionOption billingFunctionOption;
    billingFunctionOption.schedulingExtensions = schedulingExtensions;
    billingFunctionOptionsMap_[instanceID] = billingFunctionOption;
}

void MetricsContext::SetBillingNodeLabels(const std::string &instanceID, const NodeLabelsType &nodeLabels)
{
    if (enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end() &&
        enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        return;
    }
    std::lock_guard<std::mutex> lock(functionMtx_);
    auto it = billingFunctionOptionsMap_.find(instanceID);
    if (it != billingFunctionOptionsMap_.end()) {
        it->second.nodeLabels = nodeLabels;
        return;
    }
    BillingFunctionOption billingFunctionOption;
    billingFunctionOption.nodeLabels = nodeLabels;
    billingFunctionOptionsMap_[instanceID] = billingFunctionOption;
}

void MetricsContext::SetBillingPoolLabels(const std::string &instanceID, const std::vector<std::string> &labels)
{
    if (enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        return;
    }
    YRLOG_DEBUG("set billing pool labels of instance: {}", instanceID);
    std::lock_guard<std::mutex> lock(functionMtx_);
    auto it = billingFunctionOptionsMap_.find(instanceID);
    if (it != billingFunctionOptionsMap_.end()) {
        it->second.poolLabels = labels;
        return;
    }
    BillingFunctionOption billingFunctionOption;
    billingFunctionOption.poolLabels = labels;
    billingFunctionOptionsMap_[instanceID] = billingFunctionOption;
}

void MetricsContext::SetBillingCpuType(const std::string &instanceID, const std::string &cpuType)
{
    if (enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end() &&
        enabledInstruments_.find(YRInstrument::YR_APP_INSTANCE_BILLING_INVOKE_LATENCY) == enabledInstruments_.end()) {
        return;
    }
    YRLOG_DEBUG("set billing cpu type of instance: {}", instanceID);
    std::lock_guard<std::mutex> lock(functionMtx_);
    auto it = billingFunctionOptionsMap_.find(instanceID);
    if (it != billingFunctionOptionsMap_.end()) {
        it->second.cpuType = cpuType;
        return;
    }
    BillingFunctionOption billingFunctionOption;
    billingFunctionOption.cpuType = cpuType;
    billingFunctionOptionsMap_[instanceID] = billingFunctionOption;
}

void MetricsContext::SetPhysicalMetrics(const std::string &agentID, const std::string &nodeID,
                                        const resources::ResourceUnit &unit)
{
    std::lock_guard<std::mutex> lock(physicalMetricsMtx_);
    agentPhysicalMetrics_.agentID = agentID;
    agentPhysicalMetrics_.nodeID = nodeID;
    agentPhysicalMetrics_.unit = unit;
}

const BillingFunctionOption &MetricsContext::GetBillingFunctionOption(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(functionMtx_);
    auto it = billingFunctionOptionsMap_.find(instanceID);
    if (it != billingFunctionOptionsMap_.end()) {
        return it->second;
    }
    return billingFunctionOptionsMap_[instanceID];
}

void MetricsContext::EraseBillingInvokeOptionItem(const std::string &requestID)
{
    std::lock_guard<std::mutex> lock(invokeMtx_);
    billingInvokeOptionsMap_.erase(requestID);
}

void MetricsContext::EraseBillingFunctionOptionItem(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(functionMtx_);
    billingFunctionOptionsMap_.erase(instanceID);
}

const std::map<std::string, BillingInstanceInfo> MetricsContext::GetBillingInstanceMap()
{
    return billingInstanceMap_;
}

const std::map<std::string, BillingInstanceInfo> MetricsContext::GetExtraBillingInstanceMap()
{
    return extraBillingInstanceMap_;
}

const BillingInstanceInfo MetricsContext::GetBillingInstance(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    auto it = billingInstanceMap_.find(instanceID);
    if (it != billingInstanceMap_.end()) {
        return it->second;
    }
    return billingInstanceMap_[instanceID];
}

const PhysicalMetrics MetricsContext::GetPhysicalMetrics()
{
    std::lock_guard<std::mutex> lock(physicalMetricsMtx_);
    return agentPhysicalMetrics_;
}

const std::map<std::string, PodResource> MetricsContext::GetPodResourceMap()
{
    return podResourceMap_;
}

const std::map<std::string, InstanceMetrics> MetricsContext::GetInstanceMetricsMap()
{
    return instanceMetricsMap_;
}

const litebus::Option<NodeLabelsType> MetricsContext::GetNodeLabelById(const std::string &functionAgentId)
{
    std::lock_guard<std::mutex> lock(podResourceMtx_);
    auto it = podResourceMap_.find(functionAgentId);
    if (it != podResourceMap_.end()) {
        return it->second.nodeLabels;
    }
    return litebus::None();
}

const std::map<std::string, std::string> MetricsContext::GetCustomMetricsOption(
    const resource_view::InstanceInfo &instance)
{
    std::map<std::string, std::string> customMetricsOptions = {};
    auto extension = instance.scheduleoption().extension();
    if (extension.find(YR_METRICS_KEY) != extension.end()) {
        try {
            auto contentJson = nlohmann::json::parse(extension.find(YR_METRICS_KEY)->second);
            for (const auto &item : contentJson.items()) {
                (void)customMetricsOptions.insert({ item.key(), item.value() });
            }
        } catch (const std::exception &e) {
            YRLOG_WARN("Failed to parse YR_Metrics string, exception e.what():{}", e.what());
            customMetricsOptions.insert({ YR_METRICS_KEY, extension.find(YR_METRICS_KEY)->second });
        }
    }
    return customMetricsOptions;
}

void MetricsContext::InitBillingInstance(const std::string &instanceID, const std::string &functionAgentId,
                                         const std::map<std::string, std::string> &createOptions,
                                         const bool isSystemFunc)
{
    if (enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end()
        || isSystemFunc) {
        return;
    }

    auto nowMillis =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    {
        std::lock_guard<std::mutex> lock(instanceMtx_);
        BillingInstanceInfo billingInstanceInfo;
        billingInstanceInfo.startTimeMillis = nowMillis;
        billingInstanceInfo.lastReportTimeMillis = nowMillis;
        billingInstanceInfo.endTimeMillis = 0;
        billingInstanceInfo.customCreateOption = createOptions;
        billingInstanceInfo.isSystemFunc = isSystemFunc;
        billingInstanceInfo.functionAgentId = functionAgentId;
        billingInstanceMap_[instanceID] = billingInstanceInfo;
    }

    auto nodeLabelsOption = GetNodeLabelById(functionAgentId);
    if (nodeLabelsOption.IsSome()) {
        SetBillingNodeLabels(instanceID, nodeLabelsOption.Get());
    } else {
        YRLOG_WARN("There is no nodeLabels in resource unit({})", functionAgentId);
    }
    YRLOG_DEBUG("Init billing instance {}, start time: {}, custom create option size {}", instanceID, nowMillis,
                createOptions.size());
}

void MetricsContext::InitExtraBillingInstance(const std::string &instanceID, const std::string &functionAgentId,
                                              const std::map<std::string, std::string> &createOptions,
                                              const bool isSystemFunc)
{
    if (enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end()
        || isSystemFunc) {
        return;
    }

    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    {
        std::lock_guard<std::mutex> lock(extraInstanceMtx_);
        BillingInstanceInfo billingInstanceInfo;
        billingInstanceInfo.startTimeMillis = 0;
        billingInstanceInfo.isSystemFunc = isSystemFunc;
        billingInstanceInfo.lastReportTimeMillis = 0;
        billingInstanceInfo.endTimeMillis = nowMillis;
        billingInstanceInfo.customCreateOption = createOptions;
        billingInstanceInfo.functionAgentId = functionAgentId;
        extraBillingInstanceMap_[instanceID] = billingInstanceInfo;
    }
    auto nodeLabelsOption = GetNodeLabelById(functionAgentId);
    if (nodeLabelsOption.IsSome()) {
        SetBillingNodeLabels(instanceID, nodeLabelsOption.Get());
    } else {
        YRLOG_WARN("There is no nodeLabels in resource unit({}) for extra billing", functionAgentId);
    }
    YRLOG_DEBUG("Init extra billing instance {}, end time: {}, custom create option size {}", instanceID, nowMillis,
                createOptions.size());
}

bool MetricsContext::IsSystemFunc(const std::string &agentId)
{
    for (const auto &keyword : SYSTEM_FUNCTION_KEYWORD) {
        if (agentId.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}

void MetricsContext::InitInstanceMetrics(const messages::UpdateResourcesRequest &req)
{
    resources::ResourceUnit resourceUnit = req.resourceunit();
    const google::protobuf::Map<std::string, resources::InstanceInfo> instanceMap = resourceUnit.instances();

    std::lock_guard<std::mutex> lock(instanceMetricsMtx_);

    // clear all previous data
    instanceMetricsMap_.clear();
    for (auto instance : instanceMap) {
        std::string instanceId = instance.first;
        resources::InstanceInfo instanceInfo = instance.second;
        std::string agentId = resourceUnit.id();
        if (IsSystemFunc(agentId)) {
            continue;
        }

        InstanceMetrics instanceMetrics;
        instanceMetrics.instanceId = instanceId;
        instanceMetrics.agentId = agentId;

        auto nodeLables = resourceUnit.nodelabels();
        if (auto it = nodeLables.find("NODE_ID"); it != nodeLables.end()) {
            if (auto items = it->second.items(); !items.empty()) {
                instanceMetrics.nodeId = items.begin()->first;
            }
        }

        instanceMetrics.resources = instanceInfo.actualuse();

        instanceMetricsMap_[instanceId] = instanceMetrics;
    }
}

void MetricsContext::SetBillingInstanceEndTime(const std::string &instanceID, const long long endTimeMillis)
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    auto it = billingInstanceMap_.find(instanceID);
    if (it == billingInstanceMap_.end()) {
        YRLOG_WARN("Can not set instance end time because {} not found in billingInstanceMap", instanceID);
        return;
    }
    // endTime has been set
    if (it->second.endTimeMillis > 0) {
        YRLOG_DEBUG("{} Instance end time has been set: {}", instanceID, it->second.endTimeMillis);
        return;
    }
    it->second.endTimeMillis = endTimeMillis;
    YRLOG_DEBUG("{} Set instance end time: {}", instanceID, it->second.endTimeMillis);
}

void MetricsContext::SetBillingInstanceReportTime(const std::string &instanceID, const long long reportTimeMillis)
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    auto it = billingInstanceMap_.find(instanceID);
    if (it == billingInstanceMap_.end()) {
        YRLOG_WARN("Can not set instance report time because {} not found in billingInstanceMap", instanceID);
        return;
    }
    it->second.lastReportTimeMillis = reportTimeMillis;
    YRLOG_DEBUG("{} Set instance report time: {}", instanceID, it->second.lastReportTimeMillis);
}

void MetricsContext::SetPodResource(const std::string &resourceID, const resources::ResourceUnit &unit)
{
    if (enabledInstruments_.find(YRInstrument::YR_POD_RESOURCE) == enabledInstruments_.end()
        && enabledInstruments_.find(YRInstrument::YR_INSTANCE_RUNNING_DURATION) == enabledInstruments_.end()) {
        return;
    }

    // system function agent has node label resource.owner
    if (auto iter = unit.nodelabels().find(RESOURCE_OWNER_KEY);
        iter != unit.nodelabels().end() && !iter->second.items().contains(DEFAULT_OWNER_VALUE)) {
        YRLOG_DEBUG("resource {} belong to system function, skip", resourceID);
        return;
    }

    NodeLabelsType nodeLabels;
    {
        std::lock_guard<std::mutex> lock(podResourceMtx_);
        podResourceMap_[resourceID].capacity = unit.capacity();
        podResourceMap_[resourceID].allocatable = unit.allocatable();

        // add labels kv
        podResourceMap_[resourceID].nodeLabels.clear();
        for (const auto &kv : unit.nodelabels()) {
            std::map<std::string, uint64_t> itemsMap;
            for (const auto &ite : kv.second.items()) {
                itemsMap[ite.first] = ite.second;
            }
            podResourceMap_[resourceID].nodeLabels[kv.first] = itemsMap;
        }
        nodeLabels = podResourceMap_[resourceID].nodeLabels;
    }

    UpdateInstanceNodeLabels(resourceID, nodeLabels);
    UpdateExtraInstanceNodeLabels(resourceID, nodeLabels);
}

void MetricsContext::UpdateInstanceNodeLabels(const std::string &agentId, const NodeLabelsType &nodeLabels)
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    for (auto &it : billingInstanceMap_) {
        if (it.second.functionAgentId == agentId) {
            SetBillingNodeLabels(it.first, nodeLabels);
        }
    }
}

void MetricsContext::UpdateExtraInstanceNodeLabels(const std::string &agentId, const NodeLabelsType &nodeLabels)
{
    std::lock_guard<std::mutex> lock(extraInstanceMtx_);
    for (auto &it : extraBillingInstanceMap_) {
        if (it.second.functionAgentId == agentId) {
            SetBillingNodeLabels(it.first, nodeLabels);
        }
    }
}

void MetricsContext::EraseBillingInstanceItem(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    billingInstanceMap_.erase(instanceID);
}

void MetricsContext::EraseExtraBillingInstanceItem(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(extraInstanceMtx_);
    extraBillingInstanceMap_.erase(instanceID);
}

void MetricsContext::EraseBillingInstance()
{
    std::lock_guard<std::mutex> lock(instanceMtx_);
    billingInstanceMap_.clear();
}

void MetricsContext::EraseExtraBillingInstance()
{
    std::lock_guard<std::mutex> lock(extraInstanceMtx_);
    extraBillingInstanceMap_.clear();
}

void MetricsContext::DeletePodResource(const std::string &resourceID)
{
    std::lock_guard<std::mutex> lock(podResourceMtx_);
    podResourceMap_.erase(resourceID);
}

void MetricsContext::ErasePodResource()
{
    std::lock_guard<std::mutex> lock(podResourceMtx_);
    podResourceMap_.clear();
}

void MetricsContext::SetAlarm(const std::string &alarmKey, const observability::api::metrics::AlarmInfo &alarmInfo)
{
    std::lock_guard<std::mutex> lock(alarmMtx_);
    alarmInfoMap_[alarmKey] = alarmInfo;
}

void MetricsContext::EraseAlarm(const std::string &alarmKey)
{
    std::lock_guard<std::mutex> lock(alarmMtx_);
    alarmInfoMap_.erase(alarmKey);
}

bool MetricsContext::IsAlarmExisted(const std::string &alarmKey)
{
    std::lock_guard<std::mutex> lock(alarmMtx_);
    return alarmInfoMap_.find(alarmKey) != alarmInfoMap_.end();
}

const std::map<std::string, observability::api::metrics::AlarmInfo> MetricsContext::GetAlarmInfoMap()
{
    return alarmInfoMap_;
}

}  // namespace metrics
}  // namespace functionsystem