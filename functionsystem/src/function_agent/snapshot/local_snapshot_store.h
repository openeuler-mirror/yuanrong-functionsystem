/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 * See the LICENSE file in this repository for the complete license text.
 */

#ifndef FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_LOCAL_SNAPSHOT_STORE_H
#define FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_LOCAL_SNAPSHOT_STORE_H

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "common/status/status.h"

namespace functionsystem::function_agent {

struct LocalSnapshotDescriptor {
    std::string snapshotID;
    bool anonymous{ false };
    std::string instanceID;
    std::string tenantHash;
    std::string sourceRuntimeID;
    std::string sourceSandboxID;
    int64_t sourceInstanceVersion{ 0 };
    uint64_t generation{ 0 };
    std::string runtimeClass;
    std::string architecture;
    std::string artifactFormat{ "sandboxd-checkpoint-v1" };
    uint32_t artifactFormatVersion{ 1 };
    uint64_t size{ 0 };
    std::string sha256;
    int64_t createdAtUnixSeconds{ 0 };
};

struct LocalSnapshotCommitRequest {
    std::string snapshotID;
    bool anonymous{ false };
    std::string instanceID;
    std::string tenantHash;
    std::string sourceRuntimeID;
    std::string sourceSandboxID;
    int64_t sourceInstanceVersion{ 0 };
    std::string runtimeClass;
    std::string architecture;
    std::string artifactFormat{ "sandboxd-checkpoint-v1" };
    uint32_t artifactFormatVersion{ 1 };
    int64_t createdAtUnixSeconds{ 0 };
};

struct LocalSnapshotPrepareResult {
    Status status;
    std::filesystem::path directory;
    bool replayed{ false };
};

struct LocalSnapshotCommitResult {
    Status status;
    LocalSnapshotDescriptor descriptor;
};

struct LocalSnapshotDeleteIdentity {
    std::string snapshotID;
    uint64_t expectedGeneration{ 0 };
    uint64_t expectedSize{ 0 };
    std::string expectedSha256;
};

class LocalSnapshotStore {
public:
    explicit LocalSnapshotStore(std::filesystem::path checkpointRoot);

    LocalSnapshotPrepareResult Prepare(const LocalSnapshotCommitRequest &request);
    LocalSnapshotCommitResult Commit(const LocalSnapshotCommitRequest &request);
    std::vector<LocalSnapshotDescriptor> List() const;
    Status ValidateForRestore(const std::string &snapshotID, LocalSnapshotDescriptor &descriptor) const;
    Status Delete(const LocalSnapshotDeleteIdentity &identity);

private:
    Status ValidateCommitRequest(const LocalSnapshotCommitRequest &request) const;
    std::filesystem::path SnapshotDirectory(const std::string &snapshotID) const;
    Status ReadDescriptor(const std::filesystem::path &directory, LocalSnapshotDescriptor &descriptor) const;
    Status InspectImage(const std::filesystem::path &directory, uint64_t &size, std::string &sha256) const;
    Status ValidateCommittedImage(const std::filesystem::path &directory,
                                  const LocalSnapshotDescriptor &descriptor) const;
    std::vector<LocalSnapshotDescriptor> ListUnlocked() const;
    uint64_t NextGeneration(const LocalSnapshotCommitRequest &request) const;

    std::filesystem::path checkpointRoot_;
    mutable std::mutex mutex_;
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_LOCAL_SNAPSHOT_STORE_H
