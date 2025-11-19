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

#ifndef COMMON_PARTITIONER_H
#define COMMON_PARTITIONER_H

#include <vector>
#include <stack>
#include <string>
#include <iostream>
#include <map>
#include "common/logs/logging.h"
namespace functionsystem::runtime_manager {
namespace partitioner_info {
const int TOPOLOGY_X = 0;
const int TOPOLOGY_SYS = 1;
const int TOPOLOGY_NODE = 2;
const int TOPOLOGY_PHB = 3;
const int TOPOLOGY_PXB = 4;
const int TOPOLOGY_PIX = 5;
const int TOPOLOGY_NV = 6;
const int TOPOLOGY_HCCS = 6;
const int GPU_TOPOLOGY_NVLINK_START = 10;
const int GPU_TOPOLOGY_NVLINK_END = 50;
const int LOW_SPEED_BANDWIDTH_MAPPING = 100;
const int HIGH_SPEED_BANDWIDTH_MAPPING = 50;

const std::map<std::string, int> TOPOLOGY_INFO = {
    { "X", 0},
    { "SYS", 1},
    { "NODE", 2},
    { "PHB", 3},
    { "PXB", 4 },
    { "PIX", 5 },
    { "NV#", 6 },
    { "HCCS", 6},
    { "NVLINKSTART", 10 },
    { "NVLINKEND", 50 },
};

struct PartitionInfo {
    explicit PartitionInfo(int start, int connectType, size_t totalSlots)
        :start(start), connectType(connectType), totalSlots(totalSlots)
    {
    }
    int start;
    int connectType;
    size_t totalSlots;
};
}

class Partitioner {
public:
    std::vector<int> GetPartition(std::vector<std::vector<int>> topoInfo);

    struct TopoFindParams {
        std::vector<bool> &visited;
        std::vector<std::vector<int>> &topoInfo;
        std::vector<int> &partindices;
        const partitioner_info::PartitionInfo &info;
        std::stack<int> stack;
        std::vector<int> retArr;
        size_t currentIndex;
    };

private:
    std::vector<int> GetSortedIndices(std::vector<std::vector<int>> topoInfo, std::vector<int> partIndices,
                                      int connectType, size_t totalSlots);
    std::vector<int> TopoFind(std::vector<bool> &visited, std::vector<std::vector<int>> &topoInfo,
                              std::vector<int> &partindices, const partitioner_info::PartitionInfo &info) const;
    inline void InitializeStartNode(TopoFindParams &params) const;
    inline void InitializeStackAndRetArr(TopoFindParams &params) const;
    inline void ProcessTopology(TopoFindParams &params) const;
    inline size_t GetCurrentIndex(const TopoFindParams &params) const;
    inline bool ShouldStopProcessing(const TopoFindParams &params) const;
    inline void ProcessNeighbors(TopoFindParams &params) const;
    inline bool IsValidNeighbor(const TopoFindParams &params, size_t neighborIndex) const;
    inline void FillRemainingSlots(TopoFindParams &params) const;
    std::vector<int> Slice(std::vector<int> const &v, const int &m, const int &n) const;
    std::vector<std::vector<int>> GetTopoVecWithBandwidth(std::vector<std::vector<int>> topoInfo) const;
    inline void ProcessTopoInfo(std::vector<std::vector<int>> &topoInfo, size_t devNum) const;
    inline void NormalizeTopoInfo(std::vector<std::vector<int>> &topoInfo, size_t devNum) const;
    inline void UpdateTopoInfo(std::vector<std::vector<int>> &topoInfo, size_t devNum, int curMinNum, int minNum) const;
    int GetMinNum(const std::vector<std::vector<int>> &topoInfo, const int &minMum) const;
    int GetMaxNum(const std::vector<std::vector<int>> &topoInfo) const;
};
}

#endif // COMMON_PARTITIONER_H
