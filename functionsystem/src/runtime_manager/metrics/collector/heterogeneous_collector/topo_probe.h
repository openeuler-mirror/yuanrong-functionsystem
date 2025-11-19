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
#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_PROBE_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_PROBE_H

#include <memory>

#include "common/status/status.h"
#include "common/utils/cmd_tool.h"
#include "topo_info.h"

namespace functionsystem::runtime_manager {
const std::string LIMIT_INIT = "limit_init";
const std::string USAGE_INIT = "usage_init";

class TopoProbe {
public:
    explicit TopoProbe(std::shared_ptr<CmdTool> cmdTool);
    virtual ~TopoProbe() = default;

    virtual Status RefreshTopo() = 0;
    std::vector<std::string> GetPartition() const;
    std::vector<int> GetDevClusterIDs() const;
    std::vector<int> GetHBM() const;
    std::string GetVendor() const;
    std::string GetProductModel() const;
    std::vector<std::string> GetDevClusterIPs() const;
    std::vector<int> GetMemory() const;
    std::vector<int> GetHealth(const std::string &initType);
    std::vector<int> GetUsedHBM() const;
    std::vector<int> GetUsedMemory() const;
    std::vector<int> GetStream() const;
    std::vector<int> GetLatency() const;

    virtual size_t GetLimit() const = 0;
    virtual size_t GetUsage() const = 0;

protected:
    virtual void UpdateTopoPartition() = 0;
    virtual void UpdateDevTopo() = 0;
    virtual void UpdateHBM() = 0;
    virtual void UpdateMemory() = 0;
    virtual void UpdateUsedMemory() = 0;
    virtual void UpdateUsedHBM() {}
    virtual void UpdateDeviceIDs() {}
    virtual void UpdateDeviceIPs() {}
    virtual void UpdateProductModel() {}
    virtual void UpdateHealth() {}
    virtual void InitHook() {}

    std::vector<std::string> GetColumnValue(const std::string &columnStr);
    std::vector<std::string> GetLegend(const std::string &topoStr, size_t deviceNum);
    std::vector<std::vector<std::string>> GetTopoInfo(const std::vector<std::string> &topoStr, size_t gpuNum);
    std::vector<std::vector<int>> ConvertPartition(std::vector<std::vector<std::string>> topologyInfo) const;
    void UpdateTopoDevClusterIDs(const std::string &topoStr);

    std::shared_ptr<DevCluster> devInfo_;
    bool hasXPU_ = false;
    std::shared_ptr<CmdTool> cmdTool_;
    std::map<std::string, bool> initMap_ = {{LIMIT_INIT, false}, {USAGE_INIT, false}};

    mutable std::mutex refreshNpuInfoMtx_{};
};
}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_PROBE_H