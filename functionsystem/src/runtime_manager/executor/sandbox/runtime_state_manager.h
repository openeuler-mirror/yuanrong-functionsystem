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

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOX_RUNTIME_STATE_MANAGER_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOX_RUNTIME_STATE_MANAGER_H

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "async/future.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"

namespace functionsystem::runtime_manager {

/**
 * Aggregated state for a single sandbox instance.
 * All fields that were previously scattered across 6 independent maps
 * are grouped here for atomic register/unregister.
 */
struct SandboxInfo {
    std::string runtimeID;
    std::string sandboxID;         // formerly containerID
    std::string checkpointID;      // empty = no checkpoint
    std::string portMappingsJson;  // empty = no port mappings
    messages::RuntimeInstanceInfo instanceInfo;
};

/**
 * RuntimeStateManager owns and enforces the invariants of all sandbox state.
 *
 * Design rules:
 *  - Register() and Unregister() are atomic w.r.t. all internal maps.
 *  - Callers MUST NOT hold references into internal maps across actor turns.
 *  - WarmUp state and regular runtime state are orthogonal; a runtimeID can
 *    only be in one of them at a time.
 */
class RuntimeStateManager {
public:
    RuntimeStateManager() = default;
    ~RuntimeStateManager() = default;

    // Non-copyable, non-movable — owns mutable state
    RuntimeStateManager(const RuntimeStateManager &) = delete;
    RuntimeStateManager &operator=(const RuntimeStateManager &) = delete;

    // ── Registration ──────────────────────────────────────────────────────────

    /**
     * Register a new sandbox. Replaces any existing entry for the same runtimeID.
     * Clears all associated state fields atomically.
     */
    void Register(SandboxInfo info);

    /**
     * Unregister a sandbox, erasing all state associated with runtimeID.
     * No-op if runtimeID is not registered.
     */
    void Unregister(const std::string &runtimeID);

    // ── Queries ───────────────────────────────────────────────────────────────

    std::optional<SandboxInfo> Find(const std::string &runtimeID) const;
    bool IsActive(const std::string &runtimeID) const;
    bool HasSandbox(const std::string &runtimeID) const;
    std::string GetSandboxID(const std::string &runtimeID) const;
    std::string GetCheckpointID(const std::string &runtimeID) const;
    void SetCheckpointID(const std::string &runtimeID, const std::string &checkpointID);
    void ClearCheckpointID(const std::string &runtimeID);
    std::map<std::string, messages::RuntimeInstanceInfo> GetAllInstanceInfos() const;

    // Returns a snapshot of all active sandbox entries.
    std::unordered_map<std::string, SandboxInfo> GetAllSandboxes() const;

    // Returns the port-mappings JSON for runtimeID, or empty string if none.
    std::string GetPortMappingsJson(const std::string &runtimeID) const;

    // ── Partial updates (applied after sandbox is already registered) ─────────

    void UpdateSandboxID(const std::string &runtimeID, const std::string &sandboxID);
    void UpdateCheckpoint(const std::string &runtimeID, const std::string &checkpointID);
    void UpdatePortMappings(const std::string &runtimeID, const std::string &portMappingsJson);

    // ── In-progress start tracking ────────────────────────────────────────────

    void MarkStartInProgress(const std::string &runtimeID,
                             litebus::Future<messages::StartInstanceResponse> future);
    void MarkStartDone(const std::string &runtimeID);
    bool IsStartInProgress(const std::string &runtimeID) const;
    /**
     * Returns the existing in-progress Future for runtimeID, or an empty optional.
     * Used to deduplicate concurrent StartInstance requests.
     */
    std::optional<litebus::Future<messages::StartInstanceResponse>> GetInProgressFuture(
        const std::string &runtimeID) const;

    // ── Pending-delete tracking ───────────────────────────────────────────────

    void MarkPendingDelete(const std::string &runtimeID);
    void ClearPendingDelete(const std::string &runtimeID);
    bool IsPendingDelete(const std::string &runtimeID) const;

    // ── Warm-up state ─────────────────────────────────────────────────────────

    void RegisterWarmUp(const std::string &runtimeID, runtime::v1::FunctionRuntime proto);
    void UnregisterWarmUp(const std::string &runtimeID);
    bool IsWarmUp(const std::string &runtimeID) const;
    std::optional<runtime::v1::FunctionRuntime> GetWarmUp(const std::string &runtimeID) const;

private:
    // Core sandbox state — single source of truth
    std::unordered_map<std::string, SandboxInfo> sandboxes_;

    // Transient in-progress start futures (deduplicate concurrent StartInstance)
    std::unordered_map<std::string, litebus::Future<messages::StartInstanceResponse>> inProgressStarts_;

    // Runtimes that should be stopped as soon as their in-progress start completes
    std::unordered_set<std::string> pendingDeletes_;

    // Pre-warmed runtimes (registered but not yet materialized as a container)
    std::unordered_map<std::string, runtime::v1::FunctionRuntime> warmUpMap_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOX_RUNTIME_STATE_MANAGER_H
