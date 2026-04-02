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

#include "checkpoint_orchestrator.h"

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem::runtime_manager {

CheckpointOrchestrator::CheckpointOrchestrator(
    litebus::AID ownerAID,
    std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd,
    std::shared_ptr<CkptFileManager> ckptFileManager,
    RuntimeStateManager &stateManager)
    : ownerAID_(std::move(ownerAID)),
      containerd_(std::move(containerd)),
      ckptFileManager_(std::move(ckptFileManager)),
      stateManager_(stateManager)
{
}

// ── TakeSnapshot ──────────────────────────────────────────────────────────────

litebus::Future<messages::SnapshotRuntimeResponse> CheckpointOrchestrator::TakeSnapshot(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request)
{
    const std::string &runtimeID    = request->runtimeid();
    const std::string &requestID    = request->requestid();
    const std::string &instanceID   = request->instanceid();
    int32_t ttl                     = request->ttl();

    messages::SnapshotRuntimeResponse response;
    response.set_requestid(requestID);

    // Verify the sandbox exists
    if (!stateManager_.HasSandbox(runtimeID)) {
        YRLOG_ERROR("{}|TakeSnapshot: runtime({}) not found", requestID, runtimeID);
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_RUNTIME_NOT_FOUND));
        response.set_message(fmt::format("runtime {} not found", runtimeID));
        return response;
    }

    // Validate instanceID to prevent path traversal when building checkpoint path
    if (instanceID.find("..") != std::string::npos || instanceID.find('/') != std::string::npos ||
        instanceID.empty()) {
        YRLOG_ERROR("{}|TakeSnapshot: invalid instanceID({})", requestID, instanceID);
        response.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        response.set_message("invalid instanceID");
        return response;
    }

    // Generate unique checkpoint ID and local path
    const std::string sandboxID = stateManager_.GetSandboxID(runtimeID);
    const std::string checkpointID = fmt::format("ckpt-{}-{}",
        instanceID, std::chrono::system_clock::now().time_since_epoch().count());
    const std::string checkpointPath = fmt::format("/home/yuanrong/checkpoints/{}", checkpointID);

    auto ckptReq = std::make_shared<runtime::v1::CheckpointRequest>();
    ckptReq->set_id(sandboxID);
    ckptReq->set_ckpt_dir(checkpointPath);
    ckptReq->set_timeout(30);
    ckptReq->set_compress(false);
    ckptReq->set_trace_id(requestID);

    return DoCheckpoint(ckptReq).Then(
        litebus::Defer(ownerAID_, &CheckpointOrchestrator::OnCheckpointDone,
                       std::placeholders::_1, requestID, runtimeID, checkpointID, checkpointPath, ttl));
}

litebus::Future<messages::SnapshotRuntimeResponse> CheckpointOrchestrator::OnCheckpointDone(
    const runtime::v1::CheckpointResponse &ckptResponse,
    const std::string &requestID, const std::string &runtimeID,
    const std::string &checkpointID, const std::string &checkpointPath, int32_t ttl)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(requestID);

    if (!ckptResponse.success()) {
        YRLOG_ERROR("{}|checkpoint failed for runtime({}): {}", requestID, runtimeID, ckptResponse.message());
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED));
        response.set_message(ckptResponse.message());
        return response;
    }

    YRLOG_INFO("{}|checkpoint succeeded, uploading checkpoint({}) for runtime({})", requestID, checkpointID, runtimeID);

    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->RegisterCheckpoint(checkpointID, checkpointPath, checkpointID, ttl)
        .Then(litebus::Defer(ownerAID_, &CheckpointOrchestrator::OnRegisterDone,
                             std::placeholders::_1, response, requestID, runtimeID, checkpointID, ttl));
}

litebus::Future<messages::SnapshotRuntimeResponse> CheckpointOrchestrator::OnRegisterDone(
    const std::string &storageUrl,
    messages::SnapshotRuntimeResponse response,
    const std::string &requestID, const std::string &runtimeID,
    const std::string &checkpointID, int32_t ttl)
{
    auto *info = response.mutable_snapshotinfo();
    info->set_checkpointid(checkpointID);
    info->set_storage(storageUrl);
    info->set_ttlseconds(ttl);

    if (storageUrl.empty()) {
        YRLOG_ERROR("{}|RegisterCheckpoint returned empty storageUrl for runtime({})", requestID, runtimeID);
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED));
        response.set_message("checkpoint registration failed: empty storage URL");
        return response;
    }

    // Register in state manager — must happen before returning success so that
    // subsequent StopInstance calls can release the reference.
    stateManager_.SetCheckpointID(runtimeID, checkpointID);

    YRLOG_INFO("{}|snapshot complete: runtime({}) checkpoint({}) storage({})", requestID, runtimeID,
               checkpointID, storageUrl);
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("snapshot created successfully");
    return response;
}

// ── DownloadForRestore ────────────────────────────────────────────────────────

litebus::Future<std::string> CheckpointOrchestrator::DownloadForRestore(const std::string &checkpointID,
                                                                          const std::string &storageUrl,
                                                                          const std::string &requestID)
{
    YRLOG_INFO("{}|downloading checkpoint({}) from {}", requestID, checkpointID, storageUrl);
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->DownloadCheckpoint(checkpointID, storageUrl);
}

// ── AddRef ────────────────────────────────────────────────────────────────────

litebus::Future<Status> CheckpointOrchestrator::AddRef(const std::string &checkpointID,
                                                        const std::string &runtimeID,
                                                        const std::string &requestID)
{
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->AddReference(checkpointID)
        .Then(litebus::Defer(ownerAID_,
            [this, checkpointID, runtimeID, requestID](const Status &s) -> Status {
                if (s.IsError()) {
                    YRLOG_ERROR("{}|AddRef failed for checkpoint({}) runtime({}): {}", requestID,
                                checkpointID, runtimeID, s.RawMessage());
                    return s;
                }
                // Register mapping so StopInstance can release the ref
                stateManager_.SetCheckpointID(runtimeID, checkpointID);
                YRLOG_INFO("{}|AddRef succeeded for checkpoint({}) runtime({})", requestID,
                           checkpointID, runtimeID);
                return Status::OK();
            }));
}

// ── ReleaseRef ────────────────────────────────────────────────────────────────

litebus::Future<Status> CheckpointOrchestrator::ReleaseRef(const std::string &runtimeID,
                                                            const std::string &requestID)
{
    std::string checkpointID = stateManager_.GetCheckpointID(runtimeID);
    if (checkpointID.empty()) {
        YRLOG_DEBUG("{}|ReleaseRef: no checkpoint for runtime({}), skipping", requestID, runtimeID);
        return Status::OK();
    }

    YRLOG_INFO("{}|releasing checkpoint({}) ref for runtime({})", requestID, checkpointID, runtimeID);
    // Clear from state first — even if RemoveReference fails we won't double-release
    stateManager_.ClearCheckpointID(runtimeID);

    if (!ckptFileManager_) {
        return Status::OK();
    }
    return ckptFileManager_->RemoveReference(checkpointID)
        .Then([checkpointID, requestID, runtimeID](const Status &s) -> Status {
            if (s.IsError()) {
                YRLOG_WARN("{}|RemoveReference failed for checkpoint({}) runtime({}): {}", requestID,
                           checkpointID, runtimeID, s.RawMessage());
            }
            return Status::OK();
        });
}

// ── gRPC wrappers ─────────────────────────────────────────────────────────────

litebus::Future<runtime::v1::CheckpointResponse> CheckpointOrchestrator::DoCheckpoint(
    const std::shared_ptr<runtime::v1::CheckpointRequest> &req)
{
    ASSERT_IF_NULL(containerd_);
    auto resp = std::make_shared<runtime::v1::CheckpointResponse>();
    return containerd_
        ->CallAsyncX("Checkpoint", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncCheckpoint)
        .Then([req, resp](const Status &status) -> litebus::Future<runtime::v1::CheckpointResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            runtime::v1::CheckpointResponse err;
            err.set_success(false);
            err.set_message(fmt::format("checkpoint gRPC failed for container {}: {}",
                                        req->id(), status.RawMessage()));
            YRLOG_ERROR("{}|{}", req->trace_id(), err.message());
            return err;
        });
}

litebus::Future<runtime::v1::RestoreResponse> CheckpointOrchestrator::DoRestore(
    const std::shared_ptr<runtime::v1::RestoreRequest> &req)
{
    ASSERT_IF_NULL(containerd_);
    auto resp = std::make_shared<runtime::v1::RestoreResponse>();
    return containerd_
        ->CallAsyncX("Restore", *req, resp.get(), &runtime::v1::RuntimeLauncher::Stub::AsyncRestore)
        .Then([req, resp](const Status &status) -> litebus::Future<runtime::v1::RestoreResponse> {
            if (status.IsOk()) {
                return *resp;
            }
            runtime::v1::RestoreResponse err;
            err.set_code(static_cast<int32_t>(status.StatusCode()));
            err.set_message(fmt::format("restore gRPC failed for checkpoint {}: {}",
                                        req->ckpt_dir(), status.RawMessage()));
            YRLOG_ERROR("{}|{}", req->trace_id(), err.message());
            return err;
        });
}

}  // namespace functionsystem::runtime_manager
