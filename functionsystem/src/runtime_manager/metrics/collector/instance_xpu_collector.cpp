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

#include "instance_xpu_collector.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

InstanceXPUCollector::InstanceXPUCollector(const InstInfoWithXPU &basicInstInfo, const MetricsType &type,
                                           const std::shared_ptr<ProcFSTools> &procFSTools,
                                           const std::shared_ptr<XPUCollectorParams> &params)
    : BaseInstanceCollector(basicInstInfo.pid, basicInstInfo.instanceID, 0.0, basicInstInfo.deployDir),
      BaseMetricsCollector(type, collector_type::INSTANCE, procFSTools)
{
    auto cmdTool = std::make_shared<CmdTool>();
    if (type == metrics_type::NPU) {
        probe_ = std::make_shared<NpuProbe>(basicInstInfo.nodeID, procFSTools, cmdTool, params);
    } else {
        probe_ = std::make_shared<GpuProbe>(params->deviceInfoPath, cmdTool);
    }
    auto userEnvs = basicInstInfo.info.runtimeconfig().userenvs();
    for (const auto &envIter : userEnvs) {
        if (envIter.first.find(RUNTIME_ENV_PREFIX, 0) == 0) {
            std::string key = Utils::TrimPrefix(envIter.first, RUNTIME_ENV_PREFIX);
            if (key == "NPU-DEVICE-IDS") {
                YRLOG_DEBUG("logic cardID is {}", envIter.second);
                ResolveLogicCardIDs(envIter.second);
                break;
            }
        }
    }
}

Metric InstanceXPUCollector::GetLimit() const
{
    YRLOG_DEBUG("instance npu collector get limit.");
    Metric metric;
    metric.value = limit_;
    metric.instanceID = instanceID_;
    return metric;
}

litebus::Future<Metric> InstanceXPUCollector::GetUsage() const
{
    YRLOG_DEBUG("instance npu collector get usage.");
    (void)probe_->RefreshTopo(); // only once
    Metric metric;
    metric.value = probe_->GetUsage(); // count

    DevClusterMetrics devClusterMetrics = GetDevClusterMetrics(USAGE_INIT);
    devClusterMetrics.uuid = uuid_;
    devClusterMetrics.count = 0;
    metric.devClusterMetrics = devClusterMetrics;
    return metric;
}


std::string InstanceXPUCollector::GenFilter() const
{
    // deployDir-instanceId-cpu
    return litebus::os::Join(litebus::os::Join(deployDir_, instanceID_, '-'), metricsType_, '-');
}

DevClusterMetrics InstanceXPUCollector::GetDevClusterMetrics(const std::string &initType) const
{
    DevClusterMetrics devClusterMetrics;
    (void)devClusterMetrics.strInfo.insert({ dev_metrics_type::PRODUCT_MODEL_KEY, probe_->GetProductModel() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::IDS_KEY, probe_->GetDevClusterIDs() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::USED_IDS_KEY, logicIDs_ });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::USED_HBM_KEY, probe_->GetUsedHBM() });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::USED_MEM_KEY, probe_->GetUsedMemory() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::HETEROGENEOUS_MEM_KEY, probe_->GetHBM() });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::TOTAL_MEMORY_KEY, probe_->GetMemory() });

    return devClusterMetrics;
}

void InstanceXPUCollector::ResolveLogicCardIDs(const std::string &env)
{
    auto logicIDs = litebus::strings::Split(env, ",");
    for (auto &id : logicIDs) {
        int idx = 0;
        try {
            idx = std::stoi(id);
        } catch (const std::exception &e) {
            YRLOG_WARN("invalid id: {}", id);
            continue;
        }
        logicIDs_.push_back(idx);
    }
}
}