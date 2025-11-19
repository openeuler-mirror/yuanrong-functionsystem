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
#include "topo_probe.h"
#include <cstddef>
#include "partitioner.h"
#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

const int STREAM_DEFAULT_VAL = 110;
const int LATENCY_DEFAULT_VAL = 0;

TopoProbe::TopoProbe(std::shared_ptr<CmdTool> cmdTool) : cmdTool_(cmdTool){};

std::vector<std::string> TopoProbe::GetColumnValue(const std::string &columnStr)
{
    std::vector<std::string> columns;
    std::string column;
    bool appendFlag = true;
    for (char val : columnStr) {
        if (val == ' ' || val == '\t') {
            if (appendFlag) {
                (void)columns.emplace_back(column);
                column = "";
                appendFlag = false;
            }
            continue;
        }
        column += val;
        appendFlag = true;
    }
    return columns;
}

void TopoProbe::UpdateTopoDevClusterIDs(const std::string &topoStr)
{
    std::string legend;
    for (size_t i = 0; i < topoStr.size(); i++) {
        if (topoStr[i] == ' ' || topoStr[i] == '\t') {
            if (legend.empty()) {
                continue;
            }
            int num = 0;
            try {
                num = std::stoi(legend);
            } catch (const std::exception &e) {
                YRLOG_WARN("stoi fail, error:{}", e.what());
            }
            (void)devInfo_->devIDs.emplace_back(num);
            legend = "";
            continue;
        }
        if (topoStr[i] == 'C') {
            break;
        }
        if (topoStr[i] >= '0' && topoStr[i] <= '9') {
            legend += topoStr[i];
        }
    }
}

std::vector<std::string> TopoProbe::GetLegend(const std::string &topoStr, size_t deviceNum)
{
    std::vector<std::string> legends;
    std::string legend;
    bool appendFlag = true;
    for (size_t i = 0; legends.size() <= deviceNum && i < topoStr.size(); i++) {
        if (topoStr[i] == ' ' || topoStr[i] == '\t') {
            if (appendFlag) {
                (void)legends.emplace_back(legend);
                legend = "";
                appendFlag = false;
            }
            continue;
        }
        legend += topoStr[i];
        appendFlag = true;
    }
    // remove index, "GPU0" "NPU0" ...
    if (legends.size() > 1) {
        (void)legends.erase(legends.begin());
    }
    return legends;
}

std::vector<std::vector<int>> TopoProbe::ConvertPartition(std::vector<std::vector<std::string>> topologyInfo) const
{
    if (topologyInfo.empty()) {
        return {};  // 返回空的二维vector
    }

    size_t rows = topologyInfo.size();
    std::vector<std::vector<int>> res(rows, std::vector<int>(topologyInfo[0].size()));
    for (size_t i = 0; i < rows; i++) {
        if (rows != topologyInfo[i].size()) {
            YRLOG_ERROR("topo info matrix is not N x N, please check cmd: npu-smi info -t topo");
            return {};
        }
        for (size_t j = 0; j < topologyInfo[i].size(); j++) {
            auto key = topologyInfo[i][j];
            if (partitioner_info::TOPOLOGY_INFO.find(key) == partitioner_info::TOPOLOGY_INFO.end()) {
                YRLOG_ERROR("failed to get partition info {}", key);
                continue;
            }
            res[i][j] = partitioner_info::TOPOLOGY_INFO.at(key);
        }
    }
    return res;
}

std::vector<std::vector<std::string>> TopoProbe::GetTopoInfo(const std::vector<std::string> &topoStr, size_t gpuNum)
{
    std::vector<std::vector<std::string>> gpuTopo;
    for (size_t i = 1; i <= gpuNum && i < topoStr.size(); i++) {
        (void)gpuTopo.emplace_back(GetLegend(topoStr[i], gpuNum)); // legend-> "X" "PXI" ...
    }
    return gpuTopo;
}

std::vector<std::string> TopoProbe::GetPartition() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        return devInfo_->devPartition;
    }
    return std::vector<std::string>{ "" };
}

std::vector<int> TopoProbe::GetDevClusterIDs() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    return devInfo_ ? devInfo_->devIDs : std::vector<int>{};
}

std::vector<int> TopoProbe::GetHBM() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        return devInfo_->devLimitHBMs;
    }
    return std::vector<int>{};
}

std::string TopoProbe::GetVendor() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    return hasXPU_ && devInfo_ ? devInfo_->devVendor : "";
}

std::string TopoProbe::GetProductModel() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    return hasXPU_ && devInfo_ ? devInfo_->devProductModel : "";
}

std::vector<std::string> TopoProbe::GetDevClusterIPs() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    return hasXPU_ && devInfo_ ? devInfo_->devIPs : std::vector<std::string>{};
}

std::vector<int> TopoProbe::GetStream() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        std::vector<int> streams;
        auto deviceNum = devInfo_->devLimitHBMs.size();
        for (size_t i = 0; i < deviceNum; i++) {
            streams.push_back(STREAM_DEFAULT_VAL);
        }
        return streams;
    }
    return std::vector<int>{};
}

std::vector<int> TopoProbe::GetLatency() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        std::vector<int> latency;
        auto deviceNum = devInfo_->devLimitHBMs.size();
        for (size_t i = 0; i < deviceNum; i++) {
            latency.push_back(LATENCY_DEFAULT_VAL);
        }
        return latency;
    }
    return std::vector<int>{};
}

std::vector<int> TopoProbe::GetHealth(const std::string &initType)
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        auto iter = initMap_.find(initType);
        if (iter != initMap_.end() && !iter->second) { // if get firstly, return directly
            initMap_[initType] = true;  // if get nextTime, need to UpdateHealth
            return devInfo_->health;
        }
        UpdateHealth();
        return devInfo_->health;
    }
    return std::vector<int>{};
}

std::vector<int> TopoProbe::GetMemory() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        return devInfo_->devTotalMemory;
    }
    return std::vector<int>{};
}

std::vector<int> TopoProbe::GetUsedHBM() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        return devInfo_->devUsedHBM;
    }
    return std::vector<int>{};
}

std::vector<int> TopoProbe::GetUsedMemory() const
{
    std::lock_guard<std::mutex> lock(refreshNpuInfoMtx_);
    if (hasXPU_ && devInfo_) {
        return devInfo_->devUsedMemory;
    }
    return std::vector<int>{};
}
}