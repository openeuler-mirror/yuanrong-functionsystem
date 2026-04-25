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

#include "snapshot_cache.h"

#include <algorithm>
#include "common/logs/logging.h"

namespace functionsystem::snap_manager {

void SnapshotCache::Put(const std::string &snapshotID, const SnapshotMetadata &meta)
{
    // Remove old entry if exists (to update function mapping)
    Remove(snapshotID);

    // Add to cache
    snapshotCache_[snapshotID] = meta;

    // Update function mapping
    const auto &functionID = GetFunctionID(meta);
    if (!functionID.empty()) {
        functionSnapshots_[functionID].insert(snapshotID);
    }

    // Update functionKey index
    const auto &fkIndex = MakeFunctionKeyIndex(meta);
    if (!fkIndex.empty()) {
        functionKeySnapshots_[fkIndex].insert(snapshotID);
    }
}

std::optional<SnapshotMetadata> SnapshotCache::Get(const std::string &snapshotID) const
{
    auto it = snapshotCache_.find(snapshotID);
    if (it != snapshotCache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool SnapshotCache::Remove(const std::string &snapshotID)
{
    auto it = snapshotCache_.find(snapshotID);
    if (it == snapshotCache_.end()) {
        return false;
    }

    // Remove from function mapping
    const auto &functionID = GetFunctionID(it->second);
    if (!functionID.empty()) {
        auto funcIt = functionSnapshots_.find(functionID);
        if (funcIt != functionSnapshots_.end()) {
            funcIt->second.erase(snapshotID);
            if (funcIt->second.empty()) {
                functionSnapshots_.erase(funcIt);
            }
        }
    }

    // Remove from functionKey index
    const auto &fkIndex = MakeFunctionKeyIndex(it->second);
    if (!fkIndex.empty()) {
        auto fkIt = functionKeySnapshots_.find(fkIndex);
        if (fkIt != functionKeySnapshots_.end()) {
            fkIt->second.erase(snapshotID);
            if (fkIt->second.empty()) {
                functionKeySnapshots_.erase(fkIt);
            }
        }
    }

    // Remove from cache
    snapshotCache_.erase(it);
    return true;
}

std::vector<SnapshotMetadata> SnapshotCache::GetByFunction(const std::string &functionID) const
{
    std::vector<SnapshotMetadata> result;
    auto it = functionSnapshots_.find(functionID);
    if (it != functionSnapshots_.end()) {
        for (const auto &snapshotID : it->second) {
            auto snapIt = snapshotCache_.find(snapshotID);
            if (snapIt != snapshotCache_.end()) {
                result.push_back(snapIt->second);
            }
        }
    }
    return result;
}

std::unordered_set<std::string> SnapshotCache::GetSnapshotIDs(const std::string &functionID) const
{
    auto it = functionSnapshots_.find(functionID);
    if (it != functionSnapshots_.end()) {
        return it->second;
    }
    return {};
}

void SnapshotCache::Clear()
{
    snapshotCache_.clear();
    functionSnapshots_.clear();
    functionKeySnapshots_.clear();
}

std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>>
    SnapshotCache::GetSnapshotsWithTime(const std::string &functionID) const
{
    std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>> result;

    auto it = functionSnapshots_.find(functionID);
    if (it == functionSnapshots_.end()) {
        return result;
    }

    for (const auto &snapshotID : it->second) {
        auto snapIt = snapshotCache_.find(snapshotID);
        if (snapIt != snapshotCache_.end()) {
            int64_t createTime = GetCreateTime(snapIt->second);
            result.emplace_back(createTime, snapshotID, snapIt->second);
        }
    }

    // Sort by create time (oldest first)
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });

    return result;
}

std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>> SnapshotCache::GetAllSnapshotsWithTime() const
{
    std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>> result;

    for (const auto &[snapshotID, meta] : snapshotCache_) {
        int64_t createTime = GetCreateTime(meta);
        result.emplace_back(createTime, snapshotID, meta);
    }

    return result;
}

std::string SnapshotCache::GetFunctionID(const SnapshotMetadata &meta)
{
    return meta.instanceinfo().function();
}

int64_t SnapshotCache::GetCreateTime(const SnapshotMetadata &meta)
{
    const auto &createTime = meta.snapshotinfo().createtime();
    if (createTime.empty()) {
        return 0;
    }
    try {
        return std::stoll(createTime);
    } catch (const std::exception &) {
        return 0;
    }
}

std::string SnapshotCache::MakeFunctionKeyIndex(const SnapshotMetadata &meta)
{
    if (!meta.has_functionkey()) {
        return "";
    }
    const auto &fk = meta.functionkey();
    if (fk.tenantid().empty() || fk.functiontype().empty()) {
        return "";
    }
    return fk.tenantid() + "/" + fk.functiontype();
}

std::vector<SnapshotMetadata> SnapshotCache::GetByFunctionKey(
    const std::string &tenantID, const std::string &functionType, const std::string &ns) const
{
    std::string key = tenantID + "/" + functionType;
    std::vector<SnapshotMetadata> result;
    auto it = functionKeySnapshots_.find(key);
    if (it == functionKeySnapshots_.end()) {
        return result;
    }
    for (const auto &snapshotID : it->second) {
        auto snapIt = snapshotCache_.find(snapshotID);
        if (snapIt == snapshotCache_.end()) {
            continue;
        }
        if (!ns.empty() && snapIt->second.functionkey().namespace_() != ns) {
            continue;
        }
        result.push_back(snapIt->second);
    }
    return result;
}

std::vector<std::string> SnapshotCache::GetByFunctionKeyCheckpointIDs(
    const std::string &tenantID, const std::string &functionType, const std::string &ns) const
{
    std::string key = tenantID + "/" + functionType;
    std::vector<std::tuple<int64_t, std::string>> idTimePairs;
    auto it = functionKeySnapshots_.find(key);
    if (it == functionKeySnapshots_.end()) {
        return {};
    }
    for (const auto &snapshotID : it->second) {
        auto snapIt = snapshotCache_.find(snapshotID);
        if (snapIt == snapshotCache_.end()) {
            continue;
        }
        if (!ns.empty() && snapIt->second.functionkey().namespace_() != ns) {
            continue;
        }
        int64_t createTime = GetCreateTime(snapIt->second);
        idTimePairs.emplace_back(createTime, snapshotID);
    }
    std::sort(idTimePairs.begin(), idTimePairs.end(),
              [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
    std::vector<std::string> result;
    for (const auto &pair : idTimePairs) {
        result.push_back(std::get<1>(pair));
    }
    return result;
}

std::vector<SnapshotMetadata> SnapshotCache::GetByTenant(const std::string &tenantID) const
{
    std::vector<SnapshotMetadata> result;
    std::string prefix = tenantID + "/";
    for (const auto &[key, ids] : functionKeySnapshots_) {
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }
        for (const auto &snapshotID : ids) {
            auto it = snapshotCache_.find(snapshotID);
            if (it != snapshotCache_.end()) {
                result.push_back(it->second);
            }
        }
    }
    return result;
}

std::vector<std::string> SnapshotCache::GetByTenantCheckpointIDs(const std::string &tenantID) const
{
    std::vector<std::tuple<int64_t, std::string>> idTimePairs;
    std::string prefix = tenantID + "/";
    for (const auto &[key, ids] : functionKeySnapshots_) {
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }
        for (const auto &snapshotID : ids) {
            auto it = snapshotCache_.find(snapshotID);
            if (it != snapshotCache_.end()) {
                int64_t createTime = GetCreateTime(it->second);
                idTimePairs.emplace_back(createTime, snapshotID);
            }
        }
    }
    std::sort(idTimePairs.begin(), idTimePairs.end(),
              [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
    std::vector<std::string> result;
    for (const auto &pair : idTimePairs) {
        result.push_back(std::get<1>(pair));
    }
    return result;
}

}  // namespace functionsystem::snap_manager
