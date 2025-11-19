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

#include "system_xpu_collector.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "heterogeneous_collector/gpu_probe.h"
#include "heterogeneous_collector/npu_probe.h"

namespace functionsystem::runtime_manager {
SystemXPUCollector::SystemXPUCollector(std::string &nodeID, const MetricsType &type,
                                       const std::shared_ptr<ProcFSTools>& procFSTools,
                                       const std::shared_ptr<XPUCollectorParams>& params)
    : BaseMetricsCollector(type, collector_type::SYSTEM)
{
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    uuid_ = uuid.ToString();
    auto cmdTool = std::make_shared<CmdTool>();
    if (type == metrics_type::NPU) {
        probe_ = std::make_shared<NpuProbe>(nodeID, procFSTools, cmdTool, params);
    } else {
        probe_ = std::make_shared<GpuProbe>(params->deviceInfoPath, cmdTool);
    }
}

litebus::Future<Metric> SystemXPUCollector::GetUsage() const
{
    (void)probe_->RefreshTopo(); // only once
    Metric metric;
    metric.value = probe_->GetUsage(); // count
    DevClusterMetrics devClusterMetrics = GetDevClusterMetrics(USAGE_INIT);
    devClusterMetrics.uuid = uuid_;
    devClusterMetrics.count = 0;
    metric.devClusterMetrics = devClusterMetrics;
    return metric;
}

Metric SystemXPUCollector::GetLimit() const
{
    (void)probe_->RefreshTopo(); // only once
    Metric metric;
    metric.value = probe_->GetLimit(); // count
    DevClusterMetrics devClusterMetrics = GetDevClusterMetrics(LIMIT_INIT);
    devClusterMetrics.uuid = uuid_;
    devClusterMetrics.count = probe_->GetLimit();
    metric.devClusterMetrics = devClusterMetrics;
    return metric;
}

std::string SystemXPUCollector::GenFilter() const
{
    // system-npu
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

DevClusterMetrics SystemXPUCollector::GetDevClusterMetrics(const std::string &initType) const
{
    DevClusterMetrics devClusterMetrics;
    (void)devClusterMetrics.intsInfo.insert({ resource_view::IDS_KEY, probe_->GetDevClusterIDs() });
    (void)devClusterMetrics.strInfo.insert({ dev_metrics_type::VENDOR_KEY, probe_->GetVendor() });
    (void)devClusterMetrics.strInfo.insert({ dev_metrics_type::PRODUCT_MODEL_KEY, probe_->GetProductModel() });
    (void)devClusterMetrics.strInfo.insert({ resource_view::DEV_CLUSTER_IPS_KEY,
                                          resource_view::CommaSepStr(probe_->GetDevClusterIPs()) });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::HETEROGENEOUS_MEM_KEY, probe_->GetHBM() });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::USED_HBM_KEY, probe_->GetUsedHBM() });
    (void)devClusterMetrics.strInfo.insert({ dev_metrics_type::PARTITION_KEY,
                                          resource_view::CommaSepStr(probe_->GetPartition()) });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::TOTAL_MEMORY_KEY, probe_->GetMemory() });
    (void)devClusterMetrics.intsInfo.insert({ dev_metrics_type::USED_MEM_KEY, probe_->GetUsedMemory() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::HETEROGENEOUS_STREAM_KEY, probe_->GetStream() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::HETEROGENEOUS_LATENCY_KEY, probe_->GetLatency() });
    (void)devClusterMetrics.intsInfo.insert({ resource_view::HEALTH_KEY, probe_->GetHealth(initType) });
    return devClusterMetrics;
}
}