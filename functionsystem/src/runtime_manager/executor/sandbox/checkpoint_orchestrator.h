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

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOX_CHECKPOINT_ORCHESTRATOR_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOX_CHECKPOINT_ORCHESTRATOR_H

#include <memory>
#include <string>

#include "async/defer.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"
#include "common/rpc/client/grpc_client.h"
#include "common/status/status.h"
#include "runtime_manager/ckpt/ckpt_file_manager.h"
#include "runtime_state_manager.h"

namespace functionsystem::runtime_manager {

/**
 * CheckpointOrchestrator — manages the complete checkpoint/restore lifecycle.
 *
 * Responsibilities:
 *   - TakeSnapshot:         DoCheckpoint → upload/register → update state.
 *   - RestoreFromSnapshot:  download → add-ref → DoRestore;
 *                           if restore fails, compensate by calling RemoveReference.
 *   - ReleaseCheckpointRef: remove-ref from storage, clear from state.
 *
 * Invariants:
 *   - Every AddReference() has a matching RemoveReference() (on stop OR on
 *     restore failure), preventing checkpoint ref-count leaks.
 *   - Checkpoint entries in RuntimeStateManager are set before DoRestore and
 *     cleared inside ReleaseCheckpointRef — never left dangling.
 */
class CheckpointOrchestrator {
public:
    CheckpointOrchestrator(litebus::AID ownerAID,
                            std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd,
                            std::shared_ptr<CkptFileManager> ckptFileManager,
                            RuntimeStateManager &stateManager);

    // ── Snapshot ──────────────────────────────────────────────────────────────

    /**
     * Take a checkpoint of a running sandbox.
     * Chain: DoCheckpoint → RegisterCheckpoint → UpdateStateManager
     */
    litebus::Future<messages::SnapshotRuntimeResponse> TakeSnapshot(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request);

    // ── Restore ───────────────────────────────────────────────────────────────

    /**
     * Download a checkpoint and return its local path.
     * Caller is expected to follow up with RestoreWithRef().
     */
    litebus::Future<std::string> DownloadForRestore(const std::string &checkpointID,
                                                     const std::string &storageUrl,
                                                     const std::string &requestID);

    /**
     * Add a reference for checkpointID (call after DownloadForRestore).
     * On success: updates stateManager_ with runtimeID→checkpointID mapping.
     * On failure: returns error Status — caller must NOT proceed with DoRestore.
     */
    litebus::Future<Status> AddRef(const std::string &checkpointID, const std::string &runtimeID,
                                    const std::string &requestID);

    // ── Release ───────────────────────────────────────────────────────────────

    /**
     * Release the checkpoint reference held by runtimeID.
     * Looks up checkpointID from stateManager_, calls RemoveReference,
     * and clears the mapping. Safe to call even if no ref exists (no-op).
     */
    litebus::Future<Status> ReleaseRef(const std::string &runtimeID, const std::string &requestID);

    // ── gRPC wrappers ─────────────────────────────────────────────────────────

    litebus::Future<runtime::v1::CheckpointResponse> DoCheckpoint(
        const std::shared_ptr<runtime::v1::CheckpointRequest> &req);

    litebus::Future<runtime::v1::RestoreResponse> DoRestore(
        const std::shared_ptr<runtime::v1::RestoreRequest> &req);

private:
    litebus::Future<messages::SnapshotRuntimeResponse> OnCheckpointDone(
        const runtime::v1::CheckpointResponse &response,
        const std::string &requestID, const std::string &runtimeID,
        const std::string &checkpointID, const std::string &checkpointPath, int32_t ttl);

    litebus::Future<messages::SnapshotRuntimeResponse> OnRegisterDone(
        const std::string &storageUrl,
        messages::SnapshotRuntimeResponse response,
        const std::string &requestID, const std::string &runtimeID,
        const std::string &checkpointID, int32_t ttl);

    litebus::AID ownerAID_;
    std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd_;
    std::shared_ptr<CkptFileManager> ckptFileManager_;
    RuntimeStateManager &stateManager_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOX_CHECKPOINT_ORCHESTRATOR_H
