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

#ifndef FUNCTIONSYSTEM_METRIC_CONTEXT_H
#define FUNCTIONSYSTEM_METRIC_CONTEXT_H

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_set>

#include "resource_type.h"
#include "metrics_constants.h"

namespace functionsystem {

namespace metrics {

using NodeLabelsType = std::map<std::string, std::map<std::string, uint64_t>>;

struct BillingInvokeOption {
    std::string functionName;
    std::string instanceID;
    std::map<std::string, std::string> invokeOptions;
};

struct BillingFunctionOption {
    std::map<std::string, std::string> schedulingExtensions;
    NodeLabelsType nodeLabels;
    std::vector<std::string> poolLabels;
    std::string cpuType;
};

struct BillingInstanceInfo {
    std::string invokeRequestId;
    std::map<std::string, std::string> customCreateOption;
    bool isSystemFunc = false;
    long long lastReportTimeMillis;
    long long startTimeMillis;
    long long endTimeMillis;
};

struct PodResource {
    resources::Resources capacity;
    resources::Resources allocatable;
    NodeLabelsType nodeLabels;
};

class MetricsContext {
public:
    MetricsContext() = default;
    ~MetricsContext() noexcept = default;

    std::string GetAttr(const std::string &attr) const;
    void SetAttr(const std::string &attr, const std::string &value);

    void SetEnabledInstruments(const std::unordered_set<YRInstrument> &enabledInstruments);

    const BillingInvokeOption &GetBillingInvokeOption(const std::string &requestID);

    void SetBillingInvokeOptions(const std::string &requestID, const std::map<std::string, std::string> &invokeOptions,
                                 const std::string &functionName, const std::string &instanceID);

    void SetBillingSchedulingExtensions(const std::map<std::string, std::string> &schedulingExtensions,
                                        const std::string &instanceID);

    void SetBillingNodeLabels(const std::string &instanceID, const NodeLabelsType &nodeLabels);

    void SetBillingPoolLabels(const std::string &instanceID, const std::vector<std::string> &labels);

    void SetBillingCpuType(const std::string &instanceID, const std::string &cpuType);

    const BillingInstanceInfo GetBillingInstance(const std::string &instanceID);

    const std::map<std::string, BillingInstanceInfo> GetBillingInstanceMap();

    const std::map<std::string, BillingInstanceInfo> GetExtraBillingInstanceMap();

    const std::map<std::string, std::string> GetCustomMetricsOption(const resource_view::InstanceInfo &instance);

    const std::map<std::string, PodResource> GetPodResourceMap();

    void InitBillingInstance(const std::string &instanceID, const std::map<std::string, std::string> &createOptions,
                             const bool isSystemFunc = false);
    void InitExtraBillingInstance(const std::string &instanceID,
                                  const std::map<std::string, std::string> &createOptions,
                                  const bool isSystemFunc = false);

    void SetBillingInstanceEndTime(const std::string &instanceID, const long long endTimeMillis);

    void SetBillingInstanceReportTime(const std::string &instanceID, const long long reportTimeMillis);

    void SetPodResource(const std::string &resourceID, const resources::ResourceUnit &unit);

    const BillingFunctionOption &GetBillingFunctionOption(const std::string &instanceID);

    void EraseBillingInvokeOptionItem(const std::string &requestID);

    void EraseBillingFunctionOptionItem(const std::string &instanceID);

    void EraseBillingInstanceItem(const std::string &instanceID);

    void EraseExtraBillingInstanceItem(const std::string &instanceID);

    void EraseBillingInstance();

    void EraseExtraBillingInstance();

    void DeletePodResource(const std::string &resourceID);
    void ErasePodResource();

    // for test
    [[maybe_unused]] std::map<std::string, BillingInvokeOption> GetBillingInvokeOptionsMap()
    {
        return billingInvokeOptionsMap_;
    }

    // for test
    [[maybe_unused]] std::map<std::string, BillingFunctionOption> GetBillingFunctionOptionsMap()
    {
        return billingFunctionOptionsMap_;
    }

    // for test
    [[maybe_unused]] std::unordered_set<YRInstrument> GetEnabledInstruments()
    {
        return enabledInstruments_;
    }

private:
    std::map<std::string, std::string> attribute_;

    std::unordered_set<YRInstrument> enabledInstruments_;

    // key: invoke request id
    std::map<std::string, BillingInvokeOption> billingInvokeOptionsMap_;

    // key: instance id
    std::map<std::string, BillingFunctionOption> billingFunctionOptionsMap_;

    // key: instance id
    std::map<std::string, BillingInstanceInfo> billingInstanceMap_;

    // key: instance id, to store instances which need to report extra data
    std::map<std::string, BillingInstanceInfo> extraBillingInstanceMap_;

    // key: agentID, value: resources of the agent
    std::map<std::string, PodResource> podResourceMap_;

    std::mutex invokeMtx_{};
    std::mutex functionMtx_{};
    std::mutex instanceMtx_{};
    std::mutex extraInstanceMtx_{};
    std::mutex podResourceMtx_{};
};
}  // namespace metrics
}  // namespace functionsystem
#endif  // FUNCTIONSYSTEM_METRIC_CONTEXT_H
