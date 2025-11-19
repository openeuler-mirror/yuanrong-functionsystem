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
#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_NPU_PROBE_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_NPU_PROBE_H

#include <nlohmann/json.hpp>

#include "topo_probe.h"
#include "common/status/status.h"
#include "common/utils/proc_fs_tools.h"

namespace functionsystem::runtime_manager {
const int32_t DEFAULT_VDEVICE_NUMBER = 0;
struct VdeviceConfig {
    std::string nodeName;
    int32_t number{ DEFAULT_VDEVICE_NUMBER };
    std::vector<std::string> vDeviceIDs;
    std::vector<std::string> vDevicePartition;
};

class NpuProbe : public TopoProbe {
public:
    NpuProbe(std::string node, const std::shared_ptr<ProcFSTools> &procFSTools, std::shared_ptr<CmdTool> cmdTool,
             const std::shared_ptr<XPUCollectorParams> &params);
    NpuProbe() = default;
    ~NpuProbe() override;

    Status RefreshTopo() override;
    size_t GetUsage() const override;
    size_t GetLimit() const override;

    // for test
    std::shared_ptr<DevCluster> GetClusterInfo()
    {
        return devInfo_;
    }

protected:
    void UpdateTopoPartition() override;
    void UpdateDevTopo() override;
    void UpdateHBM() override;
    void UpdateUsedHBM() override;
    void UpdateMemory() override;
    void UpdateUsedMemory() override;
    void UpdateProductModel() override;
    void UpdateDeviceIDs() override;
    void UpdateDeviceIPs() override;
    void UpdateHealth() override;
    void InitHook() override;

private:
    using NPUCollectFunc = Status (NpuProbe::*)();
    using NPUParseFunc = Status (NpuProbe::*)();

    Status LoadTopoInfo();

    void InitDevInfo();
    Status NPUCollectCount();
    Status NPUCollectHBM();
    Status NPUCollectSFMD();
    Status NPUCollectTopo();
    Status NPUCollectAll();

    std::string GetNpuType();
    Status ParseNPU910B();
    Status ParseNPU910C();
    Status ParseNPU310P3();

    Status OnGetNPUInfo(bool countMode);
    Status GetNPUCountInfo();
    Status GetNPUSmiInfo();
    Status GetNPUIPInfo();
    Status GetNPUTopoInfo();
    Status GetDeviceIPsFromHccnTool();

    Status BuildTopoConfigMap(const nlohmann::json &config);
    void AddLdLibraryPathForNpuCmd(const std::string &ldLibraryPath);
    bool IsNpuTopoCommandValid(std::vector<std::string> lines);
    size_t npuNum_ = 0;
    std::string nodeID;
    bool init = false;
    std::shared_ptr<ProcFSTools> procFSTools_;

    std::string getNpuTopoInfoCmd_ = "";            // npu-smi info -t topo
    std::string getNpuStandardInfoCmd_ = "";        // npu-smi info
    std::string getNpuIPInfoCmd_ = "";              // hccn_tool
    std::vector<std::string> npuSmiCmdOutput_;  // npu-smi info
    std::string npuDeviceInfoPath_;
    std::shared_ptr<XPUCollectorParams> params_;

    std::map<std::string, NPUCollectFunc> collectFuncMap_ = { { NPU_COLLECT_COUNT, &NpuProbe::NPUCollectCount },
                                                              { NPU_COLLECT_HBM, &NpuProbe::NPUCollectHBM },
                                                              { NPU_COLLECT_SFMD, &NpuProbe::NPUCollectSFMD },
                                                              { NPU_COLLECT_TOPO, &NpuProbe::NPUCollectTopo },
                                                              { NPU_COLLECT_ALL, &NpuProbe::NPUCollectAll } };

    std::map<std::string, NPUParseFunc> parseNPUFuncMap_ = { { NPU910B, &NpuProbe::ParseNPU910B },
                                                              { NPU910C, &NpuProbe::ParseNPU910C },
                                                              { NPU310P3, &NpuProbe::ParseNPU310P3 } };
    std::unique_ptr<std::thread> refreshThread_;

    std::atomic<bool> refreshFlag_{true};
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_NPU_PROBE_H