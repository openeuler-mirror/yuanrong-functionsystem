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

#include <stack>
#include <cmath>
#include <climits>

#include "partitioner.h"

namespace functionsystem::runtime_manager {
using namespace partitioner_info;
const int MIN_NUM = 999;
const int DICHOTOMY = 2;
const int MAX_TOPO_SIZE = 1000;

std::vector<int> Partitioner::GetPartition(std::vector<std::vector<int>> topoInfo)
{
    std::vector<int> partition;
    if (topoInfo.empty()) {
        return partition;
    }
    if (topoInfo.size() > MAX_TOPO_SIZE) {
        YRLOG_ERROR("topo size {} is oversize", topoInfo.size());
        return partition;
    }
    if (topoInfo.size() == 1) {
        partition.push_back(0);
        return partition;
    }
    topoInfo = GetTopoVecWithBandwidth(topoInfo);
    auto maxBandWidth = GetMaxNum(topoInfo);
    auto totalslots = pow(2, ceil(log2((double(topoInfo.size())))));
    std::vector<int> partIndices;
    for (size_t i = 0; i < topoInfo.size(); ++i) {
        partIndices.push_back(static_cast<int>(i));
    }
    return GetSortedIndices(topoInfo, partIndices, maxBandWidth, size_t(totalslots));
}

std::vector<int> Partitioner::GetSortedIndices(std::vector<std::vector<int>> topoInfo, std::vector<int> partIndices,
                                               int connectType, size_t totalSlots)
{
    std::vector<int> retArr;
    if (partIndices.size() == 1 || connectType <= 1) {
        return partIndices;
    }
    std::vector<bool> visited(partIndices.size());
    size_t leftSize = 0;
    for (size_t i = 0; i < partIndices.size() && i < topoInfo.size(); ++i) {
        if (visited[i]) {
            continue;
        }
        if (i < topoInfo[i].size() && topoInfo[i][i] == partitioner_info::TOPOLOGY_X) {
            PartitionInfo info(i, connectType, totalSlots);
            auto temArr = TopoFind(visited, topoInfo, partIndices, info);
            if (retArr.empty()) {
                leftSize = temArr.size();
            }
            for (int val: temArr) {
                retArr.push_back(val);
            }
        }
    }
    size_t arrLen = retArr.size();
    if (totalSlots >= arrLen) {
        for (size_t i = 0; i < totalSlots - arrLen; ++i) {
            retArr.push_back(-1);
        }
    } else {
        YRLOG_WARN("totalSlots is smaller than arrLen");
    }
    auto leftIndices = GetSortedIndices(topoInfo, Slice(retArr, 0, static_cast<int>(leftSize)),
                                        connectType - 1, leftSize);
    auto rightindices =
        GetSortedIndices(topoInfo, Slice(retArr, static_cast<int>(leftSize),
                                         static_cast<int>(retArr.size())), connectType - 1, totalSlots - leftSize);
    std::vector<int> retVet;
    (void)retVet.insert(retVet.end(), leftIndices.begin(), leftIndices.end());
    (void)retVet.insert(retVet.end(), rightindices.begin(), rightindices.end());
    return retVet;
}

std::vector<int> Partitioner::TopoFind(std::vector<bool> &visited, std::vector<std::vector<int>> &topoInfo,
                                       std::vector<int> &partindices, const PartitionInfo &info) const
{
    TopoFindParams params{visited, topoInfo, partindices, info, {}, {}, 0};
    InitializeStartNode(params);
    if (info.connectType <= 0) {
        return partindices;
    }

    InitializeStackAndRetArr(params);
    ProcessTopology(params);
    FillRemainingSlots(params);

    return params.retArr;
}

void Partitioner::InitializeStartNode(TopoFindParams &params) const
{
    auto index = static_cast<unsigned short>(params.info.start);
    if (index < params.visited.size()) {
        params.visited[index] = true;
    }
}

void Partitioner::InitializeStackAndRetArr(TopoFindParams &params) const
{
    auto index = static_cast<unsigned short>(params.info.start);
    if (index < params.partindices.size()) {
        params.stack.push(params.partindices[index]);
        params.retArr.push_back(params.partindices[index]);
    }
}

void Partitioner::ProcessTopology(TopoFindParams &params) const
{
    while (!params.stack.empty()) {
        params.currentIndex = GetCurrentIndex(params);
        if (ShouldStopProcessing(params)) {
            break;
        }

        ProcessNeighbors(params);
    }
}

size_t Partitioner::GetCurrentIndex(const TopoFindParams &params) const
{
    return (params.stack.top() > 0) ? static_cast<size_t>(params.stack.top()) : 0;
}

bool Partitioner::ShouldStopProcessing(const TopoFindParams &params) const
{
    return params.retArr.size() >= params.partindices.size() / DICHOTOMY ||
           params.retArr.size() >= params.info.totalSlots / DICHOTOMY;
}

void Partitioner::ProcessNeighbors(TopoFindParams &params) const
{
    if (params.visited.size() != params.partindices.size()) {
        return;
    }

    for (size_t j = 0; j < params.partindices.size(); ++j) {
        if (IsValidNeighbor(params, j)) {
            if (!params.visited[j]) {
                params.retArr.push_back(params.partindices[j]);
                params.stack.push(params.partindices[j]);
                params.visited[j] = true;
            }
        }
        if (params.retArr.size() >= params.info.totalSlots / DICHOTOMY) {
            break;
        }
    }
}

bool Partitioner::IsValidNeighbor(const TopoFindParams &params, size_t neighborIndex) const
{
    if (neighborIndex >= params.partindices.size() || params.currentIndex >= params.topoInfo.size()) {
        return false;
    }
    auto index = static_cast<unsigned short>(params.partindices[neighborIndex]);
    return params.partindices[neighborIndex] != -1 && neighborIndex != params.currentIndex
           && params.topoInfo[params.currentIndex].size() > index
           && params.topoInfo[params.currentIndex][index] < params.info.connectType;
}

void Partitioner::FillRemainingSlots(TopoFindParams &params) const
{
    auto arrLen = params.retArr.size();
    if (arrLen >= params.info.totalSlots / DICHOTOMY) {
        return;
    }
    for (size_t i = 0; i < params.info.totalSlots / DICHOTOMY - arrLen; ++i) {
        params.retArr.push_back(-1);
    }
}

std::vector<std::vector<int>> Partitioner::GetTopoVecWithBandwidth(std::vector<std::vector<int>> topoInfo) const
{
    auto devNum = topoInfo.size();
    ProcessTopoInfo(topoInfo, devNum);
    NormalizeTopoInfo(topoInfo, devNum);
    return topoInfo;
}

void Partitioner::ProcessTopoInfo(std::vector<std::vector<int>>& topoInfo, size_t devNum) const
{
    size_t mLen = topoInfo.size();
    for (size_t i = 0; i < devNum && i < mLen; ++i) {
        for (size_t j = 0; j < devNum && j < topoInfo[i].size(); ++j) {
            if (topoInfo[i][j] >= TOPOLOGY_SYS && topoInfo[i][j] <= TOPOLOGY_PIX) {
                topoInfo[i][j] = LOW_SPEED_BANDWIDTH_MAPPING - topoInfo[i][j];
            }

            if (topoInfo[i][j] >= GPU_TOPOLOGY_NVLINK_START && topoInfo[i][j] <= GPU_TOPOLOGY_NVLINK_END) {
                topoInfo[i][j] = HIGH_SPEED_BANDWIDTH_MAPPING - topoInfo[i][j];
            }
        }
    }
}

void Partitioner::NormalizeTopoInfo(std::vector<std::vector<int>>& topoInfo, size_t devNum) const
{
    int minNum = 1;
    int curMinNum = INT_MAX;
    while (curMinNum != MIN_NUM) {
        curMinNum = GetMinNum(topoInfo, minNum);
        UpdateTopoInfo(topoInfo, devNum, curMinNum, minNum);
        minNum++;
    }
}

void Partitioner::UpdateTopoInfo(std::vector<std::vector<int>> &topoInfo, size_t devNum, int curMinNum,
                                 int minNum) const
{
    size_t mLen = topoInfo.size();
    for (size_t i = 0; i < devNum && i < mLen; ++i) {
        for (size_t j = 0; j < devNum && j < topoInfo[i].size(); ++j) {
            if (topoInfo[i][j] == curMinNum) {
                topoInfo[i][j] = minNum;
            }
        }
    }
}

int Partitioner::GetMinNum(const std::vector<std::vector<int>> &topoInfo, const int &minMum) const
{
    size_t devNum = topoInfo.size();
    int curMinNum = 999;

    for (size_t i = 0; i < devNum; i++) {
        for (size_t j = 0; j < devNum && j < topoInfo[i].size(); j++) {
            if (topoInfo[i][j] >= minMum && topoInfo[i][j] < curMinNum) {
                curMinNum = topoInfo[i][j];
            }
        }
    }
    return curMinNum;
}

int Partitioner::GetMaxNum(const std::vector<std::vector<int>> &topoInfo) const
{
    size_t devNum = topoInfo.size();
    int curMaxNum = -1;

    for (size_t i = 0; i < devNum; i++) {
        for (size_t j = 0; j < devNum && j < topoInfo[i].size(); j++) {
            if (topoInfo[i][j] > curMaxNum) {
                curMaxNum = topoInfo[i][j];
            }
        }
    }
    return curMaxNum;
}

std::vector<int> Partitioner::Slice(std::vector<int> const &v, const int &m, const int &n) const
{
    auto first = v.cbegin() + m;
    auto last = v.cbegin() + n;
    if (m >= n) {
        YRLOG_WARN("Invalid index in Slice");
        return {};
    }

    std::vector<int> vec(first, last);
    return vec;
}
}
