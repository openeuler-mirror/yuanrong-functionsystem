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

#include "ckpt_file_manager_actor.h"

#include <filesystem>
#include "async/asyncafter.hpp"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "common/utils/actor_worker.h"
#include "common/file_storage/file_storage_client.h"

namespace functionsystem::runtime_manager {

static const int32_t DEFAULT_TTL_SECONDS = 1800;  // 30 minutes
static const int32_t DEFAULT_CLEANUP_INTERVAL_SECONDS = 300;  // 5 minutes
static const std::string DEFAULT_CHECKPOINT_DIR = "/home/yuanrong/checkpoints";

CkptFileManagerActor::CkptFileManagerActor(const std::string &name, const litebus::AID &parentAID)
    : ActorBase(name),
      parentAID_(parentAID),
      defaultTTLSeconds_(DEFAULT_TTL_SECONDS),
      cleanupIntervalSeconds_(DEFAULT_CLEANUP_INTERVAL_SECONDS),
      checkpointBaseDir_(DEFAULT_CHECKPOINT_DIR),
      cleanupEnabled_(true)
{
    // Create checkpoint base directory if not exists
    std::filesystem::create_directories(checkpointBaseDir_);
}

void CkptFileManagerActor::Init()
{
    YRLOG_INFO("init CkptFileManagerActor {}", GetAID().Name());

    // Restore checkpoint files from local directory
    RestoreCheckpointsFromLocal();

    if (cleanupEnabled_) {
        StartCleanupTimer();
    }
}

void CkptFileManagerActor::Finalize()
{
    YRLOG_INFO("finalize CkptFileManagerActor {}", GetAID().Name());
    StopCleanupTimer();
}

litebus::Future<std::string> CkptFileManagerActor::DownloadCheckpoint(
    const std::string &checkpointID,
    const std::string &storageUrl)
{
    YRLOG_INFO("downloading checkpoint: {}, from: {}", checkpointID, storageUrl);

    // Check if checkpoint already exists locally
    auto iter = checkpointFiles_.find(checkpointID);
    if (iter != checkpointFiles_.end()) {
        YRLOG_INFO("checkpoint {} already exists locally at {}", checkpointID, iter->second.localPath);
        iter->second.lastAccessTime = std::chrono::steady_clock::now();
        return iter->second.localPath;
    }

    // Check if download is already in progress
    auto pendingIter = pendingDownloads_.find(checkpointID);
    if (pendingIter != pendingDownloads_.end()) {
        YRLOG_INFO("checkpoint {} download already in progress, waiting...", checkpointID);
        return pendingIter->second.GetFuture();
    }

    // Create promise for this download to coordinate concurrent requests
    litebus::Promise<std::string> downloadPromise;
    pendingDownloads_[checkpointID] = std::move(downloadPromise);
    auto downloadFuture = pendingDownloads_[checkpointID].GetFuture();

    // Create checkpoint info
    CheckpointFileInfo info;
    info.checkpointID = checkpointID;
    info.storageUrl = storageUrl;
    info.localPath = GetLocalPath(checkpointID);
    info.ttlSeconds = defaultTTLSeconds_;

    // Use local AsyncWorker for concurrent downloads
    auto worker = std::make_shared<ActorWorker>();
    worker->AsyncWork([storageUrl, localPath = info.localPath, checkpointID]() {
        YRLOG_DEBUG("worker thread downloading checkpoint: {}", checkpointID);
        file_storage::FileStorageClient client;
        Status status = client.DownloadFile(storageUrl, localPath);
        return status;
    }).OnComplete([worker, checkpointID, info, aid(GetAID())](
        const litebus::Future<Status> &result) mutable {
        worker->Terminate();
        if (result.IsError() || result.Get().IsError()) {
            auto code = result.IsError() ? result.GetErrorCode() : result.Get().StatusCode();
            YRLOG_ERROR("async download checkpoint {} failed with error code: {}",
                checkpointID, code);
            litebus::Async(aid, &CkptFileManagerActor::OnDownloadFailed,
                checkpointID, code);
            return;
        }
        YRLOG_INFO("checkpoint {} downloaded successfully to {}", checkpointID, info.localPath);
        litebus::Async(aid, &CkptFileManagerActor::OnDownloadSuccess, checkpointID, info);
    });

    return downloadFuture;
}

litebus::Future<Status> CkptFileManagerActor::AddReference(const std::string &checkpointID)
{
    auto iter = checkpointFiles_.find(checkpointID);
    if (iter == checkpointFiles_.end()) {
        YRLOG_ERROR("checkpoint {} not found when adding reference", checkpointID);
        return Status(StatusCode::ERR_CHECKPOINT_NOT_FOUND, "checkpoint not found");
    }

    iter->second.refCount++;
    iter->second.lastAccessTime = std::chrono::steady_clock::now();

    // Stop TTL counting when reference count becomes non-zero
    if (iter->second.refCount == 1 && iter->second.ttlActive) {
        iter->second.ttlActive = false;
        YRLOG_INFO("checkpoint {} reference count increased to {}, TTL stopped",
                   checkpointID, iter->second.refCount);
    } else {
        YRLOG_DEBUG("checkpoint {} reference count increased to {}",
                    checkpointID, iter->second.refCount);
    }

    return Status::OK();
}

litebus::Future<Status> CkptFileManagerActor::RemoveReference(const std::string &checkpointID)
{
    auto iter = checkpointFiles_.find(checkpointID);
    if (iter == checkpointFiles_.end()) {
        YRLOG_WARN("checkpoint {} not found when removing reference", checkpointID);
        return Status::OK();  // Already deleted, not an error
    }

    if (iter->second.refCount > 0) {
        iter->second.refCount--;
        iter->second.lastAccessTime = std::chrono::steady_clock::now();

        // Start TTL counting when reference count reaches 0
        if (iter->second.refCount == 0) {
            iter->second.ttlActive = true;
            iter->second.ttlStartTime = std::chrono::steady_clock::now();
            YRLOG_INFO("checkpoint {} reference count reached 0, starting TTL timer ({} seconds)",
                       checkpointID, iter->second.ttlSeconds);
        } else {
            YRLOG_DEBUG("checkpoint {} reference count decreased to {}",
                        checkpointID, iter->second.refCount);
        }
    } else {
        YRLOG_WARN("checkpoint {} reference count already 0", checkpointID);
    }

    return Status::OK();
}

litebus::Future<Status> CkptFileManagerActor::RegisterCheckpoint(
    const std::string &checkpointID,
    const std::string &localPath,
    const std::string &storageUrl)
{
    YRLOG_INFO("registering checkpoint: {}, local: {}, storage: {}", checkpointID, localPath, storageUrl);

    // Check if checkpoint already exists
    auto iter = checkpointFiles_.find(checkpointID);
    if (iter != checkpointFiles_.end()) {
        YRLOG_WARN("checkpoint {} already registered", checkpointID);
        return Status(StatusCode::ERR_CHECKPOINT_ALREADY_EXISTS, "checkpoint already exists");
    }

    // Upload checkpoint to remote storage using AsyncWorker
    auto worker = std::make_shared<ActorWorker>();
    litebus::Promise<Status> uploadPromise;
    auto uploadFuture = uploadPromise.GetFuture();

    worker->AsyncWork([storageUrl, localPath, checkpointID]() {
        YRLOG_DEBUG("worker thread uploading checkpoint: {}", checkpointID);
        file_storage::FileStorageClient client;
        Status status = client.UploadFile(storageUrl, localPath);
        return status;
    }).OnComplete([worker, checkpointID, localPath, storageUrl, uploadPromise, aid(GetAID())](
        const litebus::Future<Status> &result) mutable {
        worker->Terminate();
        if (result.IsError() || result.Get().IsError()) {
            auto code = result.IsError() ? result.GetErrorCode() : result.Get().StatusCode();
            YRLOG_ERROR("async upload checkpoint {} failed with error code: {}",
                checkpointID, code);
            uploadPromise.SetValue(Status(static_cast<StatusCode>(code), "upload failed"));
            return;
        }
        YRLOG_INFO("checkpoint {} uploaded successfully to {}", checkpointID, storageUrl);
        
        // Register checkpoint info after successful upload
        litebus::Async(aid, &CkptFileManagerActor::OnUploadSuccess,
                      checkpointID, localPath, storageUrl, uploadPromise);
    });

    return uploadFuture;
}

void CkptFileManagerActor::OnUploadSuccess(
    const std::string &checkpointID,
    const std::string &localPath,
    const std::string &storageUrl,
    litebus::Promise<Status> uploadPromise)
{
    // Create checkpoint info with initial reference count = 0
    CheckpointFileInfo info;
    info.checkpointID = checkpointID;
    info.localPath = localPath;
    info.storageUrl = storageUrl;
    info.refCount = 0;  // Start with 0, will be incremented when used for restore
    info.ttlSeconds = defaultTTLSeconds_;
    info.ttlActive = true;  // Start TTL immediately since refCount is 0
    info.creationTime = std::chrono::steady_clock::now();
    info.lastAccessTime = info.creationTime;
    info.ttlStartTime = info.creationTime;  // Start TTL timer now

    checkpointFiles_[checkpointID] = info;

    YRLOG_INFO("checkpoint {} registered successfully with initial refCount=0, TTL active", checkpointID);
    uploadPromise.SetValue(Status::OK());
}

void CkptFileManagerActor::SetDefaultTTL(int32_t ttlSeconds)
{
    defaultTTLSeconds_ = ttlSeconds;
    YRLOG_INFO("set default checkpoint TTL to {} seconds", ttlSeconds);
}

void CkptFileManagerActor::StartCleanupTimer()
{
    if (!cleanupEnabled_) {
        return;
    }

    YRLOG_INFO("starting checkpoint cleanup timer (interval: {} seconds)", cleanupIntervalSeconds_);
    cleanupTimer_ = litebus::AsyncAfter(cleanupIntervalSeconds_ * 1000, GetAID(),
                                        &CkptFileManagerActor::PeriodicCleanup);
}

void CkptFileManagerActor::StopCleanupTimer()
{
    if (cleanupTimer_.IsValid()) {
        litebus::TimerTools::Cancel(cleanupTimer_);
        YRLOG_INFO("checkpoint cleanup timer stopped");
    }
}

void CkptFileManagerActor::PeriodicCleanup()
{
    YRLOG_DEBUG("performing periodic checkpoint cleanup");

    CleanupExpiredFiles().Then([aid(GetAID())](const litebus::Future<int32_t> &result) {
        if (result.IsError()) {
            YRLOG_ERROR("cleanup failed with error code: {}", result.GetErrorCode());
        } else {
            int32_t deletedCount = result.Get();
            if (deletedCount > 0) {
                YRLOG_INFO("cleaned up {} expired checkpoint files", deletedCount);
            }
        }
    });

    // Schedule next cleanup
    if (cleanupEnabled_) {
        cleanupTimer_ = litebus::AsyncAfter(cleanupIntervalSeconds_ * 1000, GetAID(),
                                            &CkptFileManagerActor::PeriodicCleanup);
    }
}

litebus::Future<int32_t> CkptFileManagerActor::CleanupExpiredFiles()
{
    int32_t deletedCount = 0;
    std::vector<std::string> toDelete;

    // Find expired checkpoints
    for (const auto &entry : checkpointFiles_) {
        const auto &info = entry.second;
        if (IsExpired(info)) {
            toDelete.push_back(entry.first);
        }
    }

    // Delete expired checkpoints
    for (const auto &checkpointID : toDelete) {
        Status status = DeleteCheckpointFile(checkpointID);
        if (status.IsOK()) {
            deletedCount++;
        }
    }

    return deletedCount;
}

bool CkptFileManagerActor::IsExpired(const CheckpointFileInfo &info) const
{
    // Only check expiration if:
    // 1. Reference count is 0
    // 2. TTL is active
    if (!info.ttlActive || info.refCount > 0) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - info.ttlStartTime).count();

    return elapsed >= info.ttlSeconds;
}

Status CkptFileManagerActor::DeleteCheckpointFile(const std::string &checkpointID)
{
    auto iter = checkpointFiles_.find(checkpointID);
    if (iter == checkpointFiles_.end()) {
        return Status(StatusCode::ERR_CHECKPOINT_NOT_FOUND, "checkpoint not found");
    }

    const std::string &localPath = iter->second.localPath;

    try {
        // Delete file or directory
        if (std::filesystem::exists(localPath)) {
            if (std::filesystem::is_directory(localPath)) {
                std::filesystem::remove_all(localPath);
            } else {
                std::filesystem::remove(localPath);
            }
            YRLOG_INFO("deleted checkpoint file: {}", localPath);
        } else {
            YRLOG_WARN("checkpoint file not found: {}", localPath);
        }

        // Remove from map
        checkpointFiles_.erase(iter);
        return Status::OK();

    } catch (const std::filesystem::filesystem_error &e) {
        YRLOG_ERROR("failed to delete checkpoint {}: {}", checkpointID, e.what());
        return Status(StatusCode::ERR_FILE_OPERATION_FAILED, e.what());
    }
}

std::string CkptFileManagerActor::GetLocalPath(const std::string &checkpointID) const
{
    return checkpointBaseDir_ + "/" + checkpointID;
}

void CkptFileManagerActor::RestoreCheckpointsFromLocal()
{
    // todo(lwy): 考虑filesystem的兼容性问题
    try {
        if (!std::filesystem::exists(checkpointBaseDir_)) {
            YRLOG_INFO("checkpoint base directory does not exist, skipping restore");
            return;
        }

        int32_t restoredCount = 0;
        for (const auto &entry : std::filesystem::directory_iterator(checkpointBaseDir_)) {
            if (!entry.is_directory() && !entry.is_regular_file()) {
                continue;
            }

            // Extract checkpoint ID from filename/dirname
            std::string checkpointID = entry.path().filename().string();
            std::string localPath = entry.path().string();

            // Skip hidden files and system files
            if (checkpointID.empty() || checkpointID[0] == '.') {
                continue;
            }

            // Create checkpoint info with default settings
            CheckpointFileInfo info;
            info.checkpointID = checkpointID;
            info.localPath = localPath;
            info.storageUrl = "";  // Unknown after restart
            info.refCount = 0;  // Start with 0 references
            info.ttlActive = true;  // Start TTL immediately
            info.ttlStartTime = std::chrono::steady_clock::now();
            info.ttlSeconds = defaultTTLSeconds_;
            info.creationTime = std::chrono::steady_clock::now();
            info.lastAccessTime = std::chrono::steady_clock::now();

            checkpointFiles_[checkpointID] = info;
            restoredCount++;
            YRLOG_DEBUG("restored checkpoint: {}, path: {}", checkpointID, localPath);
        }

        if (restoredCount > 0) {
            YRLOG_INFO("restored {} checkpoint files from local directory: {}",
                       restoredCount, checkpointBaseDir_);
        } else {
            YRLOG_INFO("no checkpoint files found in local directory: {}", checkpointBaseDir_);
        }

    } catch (const std::filesystem::filesystem_error &e) {
        YRLOG_ERROR("failed to restore checkpoints from local directory: {}", e.what());
    }
}

void CkptFileManagerActor::OnDownloadSuccess(const std::string &checkpointID,
                                             const CheckpointFileInfo &info)
{
    // Store checkpoint info
    checkpointFiles_[checkpointID] = info;

    // Notify all waiting requests about success
    auto promiseIter = pendingDownloads_.find(checkpointID);
    if (promiseIter != pendingDownloads_.end()) {
        promiseIter->second.SetValue(info.localPath);
        pendingDownloads_.erase(promiseIter);
    }
}

void CkptFileManagerActor::OnDownloadFailed(const std::string &checkpointID, int32_t errorCode)
{
    // Notify all waiting requests about failure
    auto promiseIter = pendingDownloads_.find(checkpointID);
    if (promiseIter != pendingDownloads_.end()) {
        promiseIter->second.SetFailed(errorCode);
        pendingDownloads_.erase(promiseIter);
    }
}

}  // namespace functionsystem::runtime_manager
