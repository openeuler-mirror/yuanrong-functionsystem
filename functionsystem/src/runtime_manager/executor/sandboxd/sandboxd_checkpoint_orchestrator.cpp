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

#include "sandboxd_checkpoint_orchestrator.h"

#include "common/logs/logging.h"
#include "common/status/status.h"

#include <filesystem>

namespace functionsystem::runtime_manager {
namespace {

bool IsAuthoritativeArtifactPath(const std::string &artifactPath, const std::string &checkpointDir)
{
    if (artifactPath.empty() || checkpointDir.empty()) {
        return false;
    }
    const auto artifact = std::filesystem::path(artifactPath).lexically_normal();
    const auto directory = std::filesystem::path(checkpointDir).lexically_normal();
    if (!artifact.is_absolute() || !directory.is_absolute() || artifact.filename() != "checkpoint.img") {
        return false;
    }
    return artifact.parent_path() == directory;
}

}  // namespace

SandboxdCheckpointOrchestrator::SandboxdCheckpointOrchestrator(
    litebus::AID ownerAID, std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd,
    std::shared_ptr<CkptFileManager> ckptFileManager, RuntimeStateManager &stateManager)
    : ownerAID_(std::move(ownerAID)),
      sandboxd_(std::move(sandboxd)),
      ckptFileManager_(std::move(ckptFileManager)),
      stateManager_(stateManager)
{
}

// ── User-managed publication policy ──────────────────────────────────────────

litebus::Future<messages::SnapshotRuntimeResponse> SandboxdCheckpointOrchestrator::PublishUserManagedSnapshot(
    const std::shared_ptr<messages::SnapshotRuntimeRequest> &request,
    const CheckpointPlan &plan)
{
    const std::string &runtimeID    = request->runtimeid();
    const std::string &requestID    = request->requestid();

    messages::SnapshotRuntimeResponse response;
    response.set_requestid(requestID);
    if (plan.lifecycle != ArtifactLifecycle::USER_MANAGED) {
        response.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        response.set_message("user-managed snapshot requires a user-managed checkpoint plan");
        return response;
    }

    SnapshotContext context{requestID, runtimeID, plan.sandboxID, plan.checkpointID,
                            plan.checkpointDirectory, plan.ttlSeconds};
    return CheckpointLocal(plan).Then(
        litebus::Defer(ownerAID_,
            [self = shared_from_this(), context](const CheckpointResult &result) {
                return self->OnCheckpointDone(result, context);
            }));
}

litebus::Future<messages::SnapshotRuntimeResponse> SandboxdCheckpointOrchestrator::OnCheckpointDone(
    const CheckpointResult &result, const SnapshotContext &context)
{
    messages::SnapshotRuntimeResponse response;
    response.set_requestid(context.requestID);

    if (result.status.IsError()) {
        YRLOG_ERROR("{}|checkpoint failed for runtime({}): {}", context.requestID, context.runtimeID,
                    result.status.RawMessage());
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED));
        response.set_message(fmt::format("checkpoint gRPC failed for sandbox {}: {}",
                                         context.sandboxID,
                                         result.status.RawMessage()));
        return response;
    }
    response.mutable_snapshotinfo()->set_size(result.size);
    response.mutable_snapshotinfo()->set_sha256(result.sha256);

    YRLOG_INFO("{}|checkpoint succeeded, uploading checkpoint({}) for runtime({})", context.requestID,
               context.checkpointID, context.runtimeID);

    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_
        ->RegisterCheckpoint(context.checkpointID, context.checkpointPath, context.checkpointID, context.ttl)
        .Then(litebus::Defer(ownerAID_, [self = shared_from_this(), response, context](const std::string &storageUrl) {
                return self->OnRegisterDone(storageUrl, response, context);
            }));
}

litebus::Future<messages::SnapshotRuntimeResponse> SandboxdCheckpointOrchestrator::OnRegisterDone(
    const std::string &storageUrl, messages::SnapshotRuntimeResponse response, const SnapshotContext &context)
{
    auto *info = response.mutable_snapshotinfo();
    info->set_checkpointid(context.checkpointID);
    info->set_storage(storageUrl);
    info->set_ttlseconds(context.ttl);

    if (storageUrl.empty()) {
        YRLOG_ERROR("{}|RegisterCheckpoint returned empty storageUrl for runtime({})", context.requestID,
                    context.runtimeID);
        response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CHECKPOINT_FAILED));
        response.set_message("checkpoint registration failed: empty storage URL");
        return response;
    }

    // Register in state manager — must happen before returning success so that
    // subsequent StopInstance calls can release the reference.
    stateManager_.SetCheckpointID(context.runtimeID, context.checkpointID);

    YRLOG_INFO("{}|snapshot complete: runtime({}) checkpoint({}) storage({})", context.requestID, context.runtimeID,
               context.checkpointID, storageUrl);
    response.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    response.set_message("snapshot created successfully");
    return response;
}

// ── DownloadForRestore ────────────────────────────────────────────────────────

litebus::Future<std::string> SandboxdCheckpointOrchestrator::DownloadForRestore(const std::string &checkpointID,
                                                                                const std::string &storageUrl,
                                                                                const std::string &requestID)
{
    YRLOG_INFO("{}|downloading checkpoint({}) from {}", requestID, checkpointID, storageUrl);
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->DownloadCheckpoint(checkpointID, storageUrl);
}

// ── AddRef ────────────────────────────────────────────────────────────────────

litebus::Future<Status> SandboxdCheckpointOrchestrator::AddRef(const std::string &checkpointID,
                                                               const std::string &runtimeID,
                                                               const std::string &requestID)
{
    ASSERT_IF_NULL(ckptFileManager_);
    return ckptFileManager_->AddReference(checkpointID)
        .Then(litebus::Defer(
            ownerAID_, [self = shared_from_this(), checkpointID, runtimeID, requestID](const Status &s) -> Status {
                if (s.IsError()) {
                    YRLOG_ERROR("{}|AddRef failed for checkpoint({}) runtime({}): {}", requestID, checkpointID,
                                runtimeID, s.RawMessage());
                    return s;
                }
                // Register mapping so StopInstance can release the ref
                self->stateManager_.SetCheckpointID(runtimeID, checkpointID);
                YRLOG_INFO("{}|AddRef succeeded for checkpoint({}) runtime({})", requestID, checkpointID, runtimeID);
                return Status::OK();
            }));
}

// ── ReleaseRef ────────────────────────────────────────────────────────────────

litebus::Future<Status> SandboxdCheckpointOrchestrator::ReleaseRef(const std::string &runtimeID,
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
                YRLOG_WARN("{}|RemoveReference failed for checkpoint({}) runtime({}): {}", requestID, checkpointID,
                           runtimeID, s.RawMessage());
            }
            return Status::OK();
        });
}

// ── gRPC wrapper ─────────────────────────────────────────────────────────────

litebus::Future<CheckpointResult> SandboxdCheckpointOrchestrator::DoCheckpoint(
    const std::shared_ptr<runtime::v1::CheckpointRequest> &req)
{
    ASSERT_IF_NULL(sandboxd_);
    auto resp = std::make_shared<runtime::v1::CheckpointResponse>();
    return sandboxd_->CallAsyncX("Checkpoint", *req, resp.get(), &runtime::v1::SandboxService::Stub::AsyncCheckpoint)
        .Then([req, resp](const Status &status) -> CheckpointResult {
            if (status.IsError()) {
                YRLOG_ERROR("checkpoint gRPC failed for sandbox({}): {}", req->id(), status.RawMessage());
                return {status, 0, {}};
            }
            if (resp->artifact_path().empty() || resp->artifact_size() <= 0 ||
                resp->artifact_sha256().empty()) {
                return {Status(StatusCode::FAILED,
                               "sandboxd returned incomplete checkpoint artifact facts"),
                        0, {}};
            }
            if (!IsAuthoritativeArtifactPath(resp->artifact_path(), req->checkpoint_dir())) {
                return {Status(StatusCode::FAILED,
                               "sandboxd returned checkpoint artifact outside requested directory"),
                        0, {}};
            }
            return {Status::OK(), resp->artifact_size(), resp->artifact_sha256()};
        });
}

litebus::Future<CheckpointResult> SandboxdCheckpointOrchestrator::CheckpointLocal(
    const CheckpointPlan &plan)
{
    auto request = std::make_shared<runtime::v1::CheckpointRequest>();
    request->set_id(plan.sandboxID);
    request->set_checkpoint_dir(plan.checkpointDirectory);
    request->set_checkpoint_id(plan.checkpointID);
    request->set_leave_running(plan.leaveRuntimeRunning);
    return DoCheckpoint(request);
}

litebus::Future<Status> SandboxdCheckpointOrchestrator::DeleteCheckpoint(
    const std::string &checkpointDir, const std::string &checkpointID,
    const std::string &sourceSandboxID, int64_t expectedSize,
    const std::string &expectedSHA256)
{
    ASSERT_IF_NULL(sandboxd_);
    auto request = std::make_shared<runtime::v1::DeleteCheckpointRequest>();
    request->set_checkpoint_dir(checkpointDir);
    request->set_checkpoint_id(checkpointID);
    request->set_source_sandbox_id(sourceSandboxID);
    request->set_expected_size(expectedSize);
    request->set_expected_sha256(expectedSHA256);
    auto response = std::make_shared<runtime::v1::DeleteCheckpointResponse>();
    return sandboxd_->CallAsyncX("DeleteCheckpoint", request, response,
                                 &runtime::v1::SandboxService::Stub::AsyncDeleteCheckpoint);
}

}  // namespace functionsystem::runtime_manager
