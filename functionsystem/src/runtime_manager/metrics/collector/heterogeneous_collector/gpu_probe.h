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
#ifndef RUNTIME_MANAGER_METRICS_TOPOPROBE_GPU_PROBE_H
#define RUNTIME_MANAGER_METRICS_TOPOPROBE_GPU_PROBE_H

#include "topo_probe.h"
#include "common/status/status.h"

namespace functionsystem::runtime_manager {
class GpuProbe : public TopoProbe {
public:
    explicit GpuProbe(const std::string &ldLibraryPath, std::shared_ptr<CmdTool> cmdTool);
    ~GpuProbe() override = default;
    Status RefreshTopo() override;
    size_t GetLimit() const override;
    size_t GetUsage() const override;

protected:
    void UpdateTopoPartition() override;
    void UpdateDevTopo() override;
    void UpdateHBM() override;
    void UpdateMemory() override;
    void UpdateUsedMemory() override;
    void UpdateProductModel() override;
    void UpdateHealth() override;

private:
    void AddLdLibraryPathForGpuCmd(const std::string &ldLibraryPath);
    size_t gpuNum_ = 0;
    bool init = false;
    std::string getGpuNumCmd;
    std::string getGpuTopoInfoCmd;
    std::string getGpuInfoCmd;
    std::string queryGpuOrUnitInfoCmd_;
};
}

#endif // RUNTIME_MANAGER_METRICS_TOPOPROBE_GPU_PROBE_H