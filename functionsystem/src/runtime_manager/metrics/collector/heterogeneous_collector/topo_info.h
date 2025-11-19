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
#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_INFO_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_INFO_H

#include <string>
#include <vector>
#include "common/utils/proc_fs_tools.h"

namespace functionsystem::runtime_manager {
const static std::string DEV_TYPE_GPU = "gpu";  // NOLINT Graphics Processing Unit
const static std::string DEV_TYPE_NPU = "npu";  // NOLINT Neural-network Processing Unit

const static std::string DEV_VENDOR_HUAWEI = "huawei.com";  // NOLINT
const static std::string DEV_VENDOR_NVIDIA = "nvidia.com";  // NOLINT

const static std::string NPU_COLLECT_COUNT = "count";
const static std::string NPU_COLLECT_HBM = "hbm";
const static std::string NPU_COLLECT_SFMD = "sfmd";
const static std::string NPU_COLLECT_TOPO = "topo";
const static std::string NPU_COLLECT_ALL = "all";

// for cardType
const std::string NPU910B = "NPU910B";
const std::string NPU910C = "NPU910C";
const std::string NPU310P3 = "NPU310P3";

struct DevCluster {
    std::string devType;          // gpu, npu
    std::string devVendor;        // nvidia.com, huawei.com
    std::string devProductModel;  // Ascend310, Ascend310P, Ascend910A, Ascend910B1

    std::vector<int> devIDs;  // example: [0,1,2,3...]
    std::vector<std::string> devIPs;  // [192.168.100.1, 192.168.101.1...]

    std::vector<std::string> devPartition;          // tree
    std::vector<std::vector<std::string>> devTopo;  // 长度为N*N

    std::vector<int> devUsedMemory;
    std::vector<int> devTotalMemory;

    std::vector<int> devUsedHBM;
    std::vector<int> devLimitHBMs;  // [3000, 3000, 3000, 3000]
    std::vector<int> health;
};

struct XPUCollectorParams {
    std::string ldLibraryPath;
    std::string deviceInfoPath;
    std::string collectMode;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_METRICS_COLLECTOR_HETEROGENEOUS_COLLECTOR_TOPO_INFO_H