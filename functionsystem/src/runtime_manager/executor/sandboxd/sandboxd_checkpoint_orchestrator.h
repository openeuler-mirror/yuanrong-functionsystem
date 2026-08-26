/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H

#include <memory>
#include <cstdint>
#include <string>

#include "async/defer.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/sandbox_api.grpc.pb.h"
#include "common/rpc/client/grpc_client.h"
#include "common/status/status.h"
#include "runtime_manager/ckpt/checkpoint_plan.h"
#include "runtime_manager/ckpt/ckpt_file_manager.h"
#include "runtime_manager/executor/sandboxd/runtime_state_manager.h"

namespace functionsystem::runtime_manager {

/**
 * SandboxdCheckpointOrchestrator — checkpoint lifecycle against the sandboxd
 * SandboxService (Checkpoint + Restore RPCs).
 *
 * All callers use the same explicit CheckpointPlan and receive the same local
 * artifact facts. Artifact publication and lifecycle remain caller policy.
 */
class SandboxdCheckpointOrchestrator : public std::enable_shared_from_this<SandboxdCheckpointOrchestrator> {
public:
    SandboxdCheckpointOrchestrator(litebus::AID ownerAID,
                                   std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd,
                                   std::shared_ptr<CkptFileManager> ckptFileManager, RuntimeStateManager &stateManager);
    ~SandboxdCheckpointOrchestrator() = default;

    // ── Snapshot ──────────────────────────────────────────────────────────────

    /** Publish an already planned user-managed checkpoint through the legacy
     * register/upload/ref-count lifecycle. The physical checkpoint operation
     * itself is shared with instance-managed Pause.
     */
    litebus::Future<messages::SnapshotRuntimeResponse> PublishUserManagedSnapshot(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request,
        const CheckpointPlan &plan);

    // ── Restore ───────────────────────────────────────────────────────────────

    /**
     * Download a checkpoint and return its local path.
     * Legacy snapshot caller follows up with AddRef() and Restore.
     */
    litebus::Future<std::string> DownloadForRestore(const std::string &checkpointID, const std::string &storageUrl,
                                                    const std::string &requestID);

    /**
     * Add a reference for checkpointID. On success: records runtimeID->checkpointID
     * in the state manager so StopInstance can release it.
     */
    litebus::Future<Status> AddRef(const std::string &checkpointID, const std::string &runtimeID,
                                   const std::string &requestID);

    // ── Release ───────────────────────────────────────────────────────────────

    /**
     * Release the checkpoint reference held by runtimeID. Safe no-op if none.
     */
    litebus::Future<Status> ReleaseRef(const std::string &runtimeID, const std::string &requestID);

    // ── gRPC wrapper ──────────────────────────────────────────────────────────

    litebus::Future<CheckpointResult> DoCheckpoint(
        const std::shared_ptr<runtime::v1::CheckpointRequest> &req);

    litebus::Future<CheckpointResult> CheckpointLocal(const CheckpointPlan &plan);

    /**
     * Delete one sandboxd-owned checkpoint artifact after verifying its exact
     * durable identity. A mismatch is reported by sandboxd and must not delete
     * any other checkpoint.
     */
    litebus::Future<Status> DeleteCheckpoint(
        const std::string &checkpointDir, const std::string &checkpointID,
        const std::string &sourceSandboxID, int64_t expectedSize,
        const std::string &expectedSHA256);

private:
    struct SnapshotContext {
        std::string requestID;
        std::string runtimeID;
        std::string sandboxID;
        std::string checkpointID;
        std::string checkpointPath;
        int32_t ttl = 0;
    };

    litebus::Future<messages::SnapshotRuntimeResponse> OnCheckpointDone(
        const CheckpointResult &result, const SnapshotContext &context);

    litebus::Future<messages::SnapshotRuntimeResponse> OnRegisterDone(const std::string &storageUrl,
        messages::SnapshotRuntimeResponse response,
        const SnapshotContext &context);

    litebus::AID ownerAID_;
    std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd_;
    std::shared_ptr<CkptFileManager> ckptFileManager_;
    RuntimeStateManager &stateManager_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H
