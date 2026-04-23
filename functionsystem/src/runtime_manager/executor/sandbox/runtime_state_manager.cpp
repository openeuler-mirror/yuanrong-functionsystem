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

#include "runtime_state_manager.h"

#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {

// ── Registration ──────────────────────────────────────────────────────────────

void RuntimeStateManager::Register(SandboxInfo info)
{
    const std::string &runtimeID = info.runtimeID;
    YRLOG_DEBUG("RuntimeStateManager::Register runtimeID({})", runtimeID);
    sandboxes_[runtimeID] = std::move(info);
}

void RuntimeStateManager::Unregister(const std::string &runtimeID)
{
    YRLOG_DEBUG("RuntimeStateManager::Unregister runtimeID({})", runtimeID);
    sandboxes_.erase(runtimeID);
    inProgressStarts_.erase(runtimeID);
    pendingDeletes_.erase(runtimeID);
    // Note: warmUpMap_ is intentionally NOT cleared here.
    // WarmUp state has its own lifecycle via RegisterWarmUp/UnregisterWarmUp.
}

// ── Queries ───────────────────────────────────────────────────────────────────

std::optional<SandboxInfo> RuntimeStateManager::Find(const std::string &runtimeID) const
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool RuntimeStateManager::IsActive(const std::string &runtimeID) const
{
    return sandboxes_.count(runtimeID) > 0;
}

bool RuntimeStateManager::HasSandbox(const std::string &runtimeID) const
{
    return sandboxes_.count(runtimeID) > 0;
}

std::string RuntimeStateManager::GetSandboxID(const std::string &runtimeID) const
{
    auto it = sandboxes_.find(runtimeID);
    return it != sandboxes_.end() ? it->second.sandboxID : std::string{};
}

std::string RuntimeStateManager::GetCheckpointID(const std::string &runtimeID) const
{
    auto it = sandboxes_.find(runtimeID);
    return it != sandboxes_.end() ? it->second.checkpointID : std::string{};
}

void RuntimeStateManager::SetCheckpointID(const std::string &runtimeID, const std::string &checkpointID)
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        YRLOG_WARN("RuntimeStateManager::SetCheckpointID runtimeID({}) not found", runtimeID);
        return;
    }
    it->second.checkpointID = checkpointID;
}

void RuntimeStateManager::ClearCheckpointID(const std::string &runtimeID)
{
    auto it = sandboxes_.find(runtimeID);
    if (it != sandboxes_.end()) {
        it->second.checkpointID.clear();
    }
}

std::map<std::string, messages::RuntimeInstanceInfo> RuntimeStateManager::GetAllInstanceInfos() const
{
    std::map<std::string, messages::RuntimeInstanceInfo> result;
    for (const auto &[id, info] : sandboxes_) {
        result[id] = info.instanceInfo;
    }
    return result;
}

std::unordered_map<std::string, SandboxInfo> RuntimeStateManager::GetAllSandboxes() const
{
    return sandboxes_;
}

std::string RuntimeStateManager::GetPortMappingsJson(const std::string &runtimeID) const
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        return "";
    }
    return it->second.portMappingsJson;
}

// ── Partial updates ───────────────────────────────────────────────────────────

void RuntimeStateManager::UpdateSandboxID(const std::string &runtimeID, const std::string &sandboxID)
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        YRLOG_WARN("RuntimeStateManager::UpdateSandboxID runtimeID({}) not found", runtimeID);
        return;
    }
    it->second.sandboxID = sandboxID;
}

void RuntimeStateManager::UpdateCheckpoint(const std::string &runtimeID, const std::string &checkpointID)
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        YRLOG_WARN("RuntimeStateManager::UpdateCheckpoint runtimeID({}) not found", runtimeID);
        return;
    }
    it->second.checkpointID = checkpointID;
}

void RuntimeStateManager::UpdatePortMappings(const std::string &runtimeID, const std::string &portMappingsJson)
{
    auto it = sandboxes_.find(runtimeID);
    if (it == sandboxes_.end()) {
        YRLOG_WARN("RuntimeStateManager::UpdatePortMappings runtimeID({}) not found", runtimeID);
        return;
    }
    it->second.portMappingsJson = portMappingsJson;
}

// ── In-progress start tracking ────────────────────────────────────────────────

void RuntimeStateManager::MarkStartInProgress(const std::string &runtimeID,
                                               litebus::Future<messages::StartInstanceResponse> future)
{
    inProgressStarts_.emplace(runtimeID, std::move(future));
}

void RuntimeStateManager::MarkStartDone(const std::string &runtimeID)
{
    inProgressStarts_.erase(runtimeID);
}

bool RuntimeStateManager::IsStartInProgress(const std::string &runtimeID) const
{
    return inProgressStarts_.count(runtimeID) > 0;
}

std::optional<litebus::Future<messages::StartInstanceResponse>> RuntimeStateManager::GetInProgressFuture(
    const std::string &runtimeID) const
{
    auto it = inProgressStarts_.find(runtimeID);
    if (it == inProgressStarts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// ── Pending-delete tracking ───────────────────────────────────────────────────

void RuntimeStateManager::MarkPendingDelete(const std::string &runtimeID)
{
    pendingDeletes_.insert(runtimeID);
}

void RuntimeStateManager::ClearPendingDelete(const std::string &runtimeID)
{
    pendingDeletes_.erase(runtimeID);
}

bool RuntimeStateManager::IsPendingDelete(const std::string &runtimeID) const
{
    return pendingDeletes_.count(runtimeID) > 0;
}

// ── Warm-up state ─────────────────────────────────────────────────────────────

void RuntimeStateManager::RegisterWarmUp(const std::string &runtimeID, runtime::v1::FunctionRuntime proto)
{
    warmUpMap_[runtimeID] = std::move(proto);
}

void RuntimeStateManager::UnregisterWarmUp(const std::string &runtimeID)
{
    warmUpMap_.erase(runtimeID);
}

bool RuntimeStateManager::IsWarmUp(const std::string &runtimeID) const
{
    return warmUpMap_.count(runtimeID) > 0;
}

std::optional<runtime::v1::FunctionRuntime> RuntimeStateManager::GetWarmUp(const std::string &runtimeID) const
{
    auto it = warmUpMap_.find(runtimeID);
    if (it == warmUpMap_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace functionsystem::runtime_manager
