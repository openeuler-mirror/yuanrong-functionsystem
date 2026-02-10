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

#ifndef FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_CACHE_H
#define FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_CACHE_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

#include "common/proto/pb/posix/message.pb.h"

namespace functionsystem::snap_manager {

using SnapshotMetadata = ::messages::SnapshotMetadata;

/**
 * SnapshotCache manages snapshot metadata in memory.
 * Provides fast lookup and indexing by function ID.
 */
class SnapshotCache {
public:
    SnapshotCache() = default;
    ~SnapshotCache() = default;

    // Disable copy
    SnapshotCache(const SnapshotCache &) = delete;
    SnapshotCache &operator=(const SnapshotCache &) = delete;

    /**
     * Add or update a snapshot in the cache
     * @param snapshotID Snapshot ID
     * @param meta Snapshot metadata
     */
    void Put(const std::string &snapshotID, const SnapshotMetadata &meta);

    /**
     * Get snapshot metadata by ID
     * @param snapshotID Snapshot ID
     * @return Optional snapshot metadata
     */
    std::optional<SnapshotMetadata> Get(const std::string &snapshotID) const;

    /**
     * Remove a snapshot from the cache
     * @param snapshotID Snapshot ID
     * @return true if snapshot was found and removed
     */
    bool Remove(const std::string &snapshotID);

    /**
     * Get all snapshots for a function
     * @param functionID Function ID
     * @return Vector of snapshot metadata
     */
    std::vector<SnapshotMetadata> GetByFunction(const std::string &functionID) const;

    /**
     * Get all snapshot IDs for a function
     * @param functionID Function ID
     * @return Set of snapshot IDs
     */
    std::unordered_set<std::string> GetSnapshotIDs(const std::string &functionID) const;

    /**
     * Clear all cached snapshots
     */
    void Clear();

    /**
     * Get total number of cached snapshots
     */
    size_t Size() const { return snapshotCache_.size(); }

    /**
     * Check if a snapshot exists
     */
    bool Contains(const std::string &snapshotID) const {
        return snapshotCache_.find(snapshotID) != snapshotCache_.end();
    }

    /**
     * Get all snapshots with their create times for a function (sorted)
     * @param functionID Function ID
     * @return Vector of (createTime, snapshotID, metadata) sorted by time
     */
    std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>>
        GetSnapshotsWithTime(const std::string &functionID) const;

    /**
     * Get all snapshots with their create times (for cleanup)
     * @return Vector of (createTime, snapshotID, metadata)
     */
    std::vector<std::tuple<int64_t, std::string, SnapshotMetadata>> GetAllSnapshotsWithTime() const;

private:
    // Snapshot cache: snapshotID -> metadata
    std::unordered_map<std::string, SnapshotMetadata> snapshotCache_;

    // Function to snapshots mapping: functionID -> set of snapshotIDs
    std::unordered_map<std::string, std::unordered_set<std::string>> functionSnapshots_;

    // Helper: Extract function ID from metadata
    static std::string GetFunctionID(const SnapshotMetadata &meta);

    // Helper: Extract create time from metadata
    static int64_t GetCreateTime(const SnapshotMetadata &meta);
};

}  // namespace functionsystem::snap_manager

#endif  // FUNCTION_MASTER_SNAP_MANAGER_SNAPSHOT_CACHE_H
