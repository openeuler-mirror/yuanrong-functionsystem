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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CKPT_FILE_MANAGER_ACTOR_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CKPT_FILE_MANAGER_ACTOR_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/status/status.h"
#include "common/proto/pb/message_pb.h"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace functionsystem::runtime_manager {

/**
 * CheckpointFileInfo: Checkpoint file metadata with reference counting and TTL
 */
struct CheckpointFileInfo {
    std::string checkpointID;
    std::string localPath;
    std::string storageUrl;
    int32_t refCount;  // Reference count from containers
    std::chrono::steady_clock::time_point createdTime;
    std::chrono::steady_clock::time_point lastAccessTime;
    std::chrono::steady_clock::time_point ttlStartTime;  // When ref count reached 0
    int32_t ttlSeconds;  // TTL in seconds after ref count reaches 0
    bool ttlActive;      // Whether TTL counting is active

    CheckpointFileInfo()
        : refCount(0), ttlSeconds(3600), ttlActive(false)
    {
        auto now = std::chrono::steady_clock::now();
        createdTime = now;
        lastAccessTime = now;
        ttlStartTime = now;
    }
};

/**
 * CkptFileManagerActor: Manages checkpoint files with download, reference counting, and TTL cleanup
 */
class CkptFileManagerActor : public litebus::ActorBase {
public:
    explicit CkptFileManagerActor(const std::string &name, const litebus::AID &parentAID);

    ~CkptFileManagerActor() override = default;

    /**
     * Download checkpoint file from remote storage
     * @param checkpointID Unique checkpoint identifier
     * @param storageUrl Remote storage URL (used as storage key)
     * @return Future with local file path
     */
    litebus::Future<std::string> DownloadCheckpoint(const std::string &checkpointID,
                                                     const std::string &storageUrl);

    /**
     * Increment reference count for a checkpoint file (container using it)
     * @param checkpointID Checkpoint identifier
     * @return Future with status
     */
    litebus::Future<Status> AddReference(const std::string &checkpointID);

    /**
     * Decrement reference count for a checkpoint file (container stopped using it)
     * @param checkpointID Checkpoint identifier
     * @return Future with status
     */
    litebus::Future<Status> RemoveReference(const std::string &checkpointID);

    /**
     * Register a locally created checkpoint file (from snapshot operation)
     * @param checkpointID Unique checkpoint identifier
     * @param localPath Local checkpoint directory path
     * @param storageUrl Remote storage URL (storage key)
     * @return Future with status
     */
    litebus::Future<Status> RegisterCheckpoint(const std::string &checkpointID,
                                               const std::string &localPath,
                                               const std::string &storageUrl);

    /**
     * Set TTL for checkpoint files (in seconds)
     * @param ttlSeconds Time to live after reference count reaches 0
     */
    void SetDefaultTTL(int32_t ttlSeconds);

    /**
     * Start TTL cleanup timer
     */
    void StartCleanupTimer();

    /**
     * Stop TTL cleanup timer
     */
    void StopCleanupTimer();

    /**
     * Manually trigger cleanup of expired checkpoint files
     * @return Number of files deleted
     */
    litebus::Future<int32_t> CleanupExpiredFiles();

protected:
    void Init() override;
    void Finalize() override;

private:
    /**
     * Perform periodic cleanup of expired checkpoint files
     */
    void PeriodicCleanup();

    /**
     * Check if a checkpoint file has expired
     * @param info Checkpoint file info
     * @return True if expired
     */
    bool IsExpired(const CheckpointFileInfo &info) const;

    /**
     * Delete checkpoint file from local storage
     * @param checkpointID Checkpoint identifier
     * @return Status of deletion
     */
    Status DeleteCheckpointFile(const std::string &checkpointID);

    /**
     * Get local storage path for checkpoint
     * @param checkpointID Checkpoint identifier
     * @return Local file path
     */
    std::string GetLocalPath(const std::string &checkpointID) const;

    /**
     * Restore checkpoint files from local directory on startup
     */
    void RestoreCheckpointsFromLocal();

    /**
     * Handle successful download completion
     */
    void OnDownloadSuccess(const std::string &checkpointID, const CheckpointFileInfo &info);

    /**
     * Handle failed download
     */
    void OnDownloadFailed(const std::string &checkpointID, int32_t errorCode);

    /**
     * Handle successful upload completion
     */
    void OnUploadSuccess(const std::string &checkpointID,
                        const std::string &localPath,
                        const std::string &storageUrl,
                        litebus::Promise<Status> uploadPromise);

    litebus::AID parentAID_;
    std::unordered_map<std::string, CheckpointFileInfo> checkpointFiles_;
    std::unordered_map<std::string, litebus::Promise<std::string>> pendingDownloads_;  // Track ongoing downloads
    int32_t defaultTTLSeconds_;
    int32_t cleanupIntervalSeconds_;
    litebus::Timer cleanupTimer_;
    std::string checkpointBaseDir_;
    bool cleanupEnabled_;
};

}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CKPT_FILE_MANAGER_ACTOR_H
