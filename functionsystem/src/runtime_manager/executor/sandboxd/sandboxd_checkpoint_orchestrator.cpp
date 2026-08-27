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
#include "common/snapshot_storage/snapshot_storage.h"
#include "common/status/status.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace functionsystem::runtime_manager {
namespace {

Status DeleteCallerOwnedCheckpoint(const std::string &checkpointDir, const std::string &checkpointID,
                                   int64_t expectedSize, const std::string &expectedSHA256)
{
    namespace fs = std::filesystem;
    const auto directoryPath = fs::path(checkpointDir).lexically_normal();
    if (!directoryPath.is_absolute() || directoryPath.filename() != checkpointID
        || !snapshot_storage::detail::IsSafeLeafName(checkpointID)
        || expectedSize <= 0 || expectedSHA256.empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid caller-owned checkpoint identity");
    }

    snapshot_storage::detail::SecureDirectory directory;
    auto status = snapshot_storage::detail::SecureDirectory::Open(directoryPath, false, directory);
    if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        return Status::OK();
    }
    if (status.IsError()) {
        return status;
    }

    snapshot_storage::SnapshotObjectMetadata expected;
    expected.snapshotID = checkpointID;
    expected.size = static_cast<uint64_t>(expectedSize);
    expected.sha256 = expectedSHA256;
    expected.complete = true;
    status = snapshot_storage::detail::ValidateFile(directory.ProcPath("checkpoint.img"), expected);
    if (status.IsError()) {
        return snapshot_storage::detail::Conflict(status.RawMessage());
    }
    if (auto identity = directory.VerifyPathIdentity(); identity.IsError()) {
        return identity;
    }
    if (unlinkat(directory.Fd(), "checkpoint.img", 0) != 0 && errno != ENOENT) {
        return Status(StatusCode::FAILED, "failed to delete checkpoint.img: " + std::string(std::strerror(errno)));
    }

    snapshot_storage::detail::SecureDirectory parent;
    status = snapshot_storage::detail::SecureDirectory::Open(directoryPath.parent_path(), false, parent);
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(parent.Fd(), checkpointID.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        return Status(StatusCode::FAILED, "failed to delete checkpoint directory: "
                                          + std::string(std::strerror(errno)));
    }
    return Status::OK();
}

}  // namespace

SandboxdCheckpointOrchestrator::SandboxdCheckpointOrchestrator(
    litebus::AID ownerAID, std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd,
    std::shared_ptr<CkptFileManager> ckptFileManager, RuntimeStateManager &stateManager)
    : ownerAID_(std::move(ownerAID)),
      sandboxd_(std::move(sandboxd)),
      ckptFileManager_(std::move(ckptFileManager)),
      snapshotWorker_(std::make_shared<ActorWorker>()),
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
                return CheckpointResult{status, 0, {}};
            }
            return {Status::OK(), 0, {}};
        });
}

litebus::Future<CheckpointResult> SandboxdCheckpointOrchestrator::CheckpointLocal(
    const CheckpointPlan &plan)
{
    auto request = std::make_shared<runtime::v1::CheckpointRequest>();
    request->set_id(plan.sandboxID);
    request->set_checkpoint_dir(plan.checkpointDirectory);
    request->set_timeout_seconds(plan.timeoutSeconds);
    request->set_compress(plan.compress);
    request->set_leave_running(plan.leaveRuntimeRunning);
    return DoCheckpoint(request)
        .Then([this, plan](const CheckpointResult &result) -> litebus::Future<CheckpointResult> {
            if (result.status.IsError()) {
                return result;
            }
            const auto image = (std::filesystem::path(plan.checkpointDirectory) / "checkpoint.img").string();
            return snapshot_storage::InspectLocalSnapshotFile(snapshotWorker_, image, plan.checkpointID, 0)
                .Then([](const snapshot_storage::SnapshotStat &inspection) -> CheckpointResult {
                    if (inspection.status.IsError()) {
                        return {inspection.status, 0, {}};
                    }
                    return {Status::OK(), static_cast<int64_t>(inspection.metadata.size),
                            inspection.metadata.sha256};
                });
        });
}

litebus::Future<Status> SandboxdCheckpointOrchestrator::DeleteCheckpoint(
    const std::string &checkpointDir, const std::string &checkpointID,
    const std::string &, int64_t expectedSize,
    const std::string &expectedSHA256)
{
    return snapshot_storage::detail::RunOnWorker<Status>(
        snapshotWorker_, [checkpointDir, checkpointID, expectedSize, expectedSHA256]() {
            return DeleteCallerOwnedCheckpoint(checkpointDir, checkpointID, expectedSize, expectedSHA256);
        });
}

}  // namespace functionsystem::runtime_manager
