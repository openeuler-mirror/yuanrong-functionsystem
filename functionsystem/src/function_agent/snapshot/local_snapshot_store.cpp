/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include "function_agent/snapshot/local_snapshot_store.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/snapshot_storage/secure_directory.h"
#include "common/snapshot_storage/snapshot_storage.h"

namespace functionsystem::function_agent {
namespace fs = std::filesystem;
namespace {

constexpr char CHECKPOINT_IMAGE_NAME[] = "checkpoint.img";

Status FileError(const std::string &operation, int error = errno)
{
    return Status(error == ENOENT ? StatusCode::FILE_NOT_FOUND : StatusCode::FAILED,
                  operation + ": " + std::strerror(error));
}

}  // namespace

LocalSnapshotStore::LocalSnapshotStore(fs::path checkpointRoot, uint64_t maxCacheBytes)
    : checkpointRoot_(fs::absolute(std::move(checkpointRoot)).lexically_normal()),
      maxCacheBytes_(maxCacheBytes)
{
    std::error_code error;
    fs::create_directories(checkpointRoot_, error);
}

Status LocalSnapshotStore::ValidateCommitRequest(const LocalSnapshotCommitRequest &request) const
{
    if (!snapshot_storage::detail::IsSafeLeafName(request.snapshotID)
        || request.createdAtUnixSeconds <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid local artifact request");
    }
    if (request.recoveryCandidate && request.instanceID.empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "recovery candidate requires instance ID");
    }
    return Status::OK();
}

fs::path LocalSnapshotStore::SnapshotDirectory(const std::string &snapshotID) const
{
    return (checkpointRoot_ / snapshotID).lexically_normal();
}

Status LocalSnapshotStore::InspectArtifact(const fs::path &directory, uint64_t &size) const
{
    snapshot_storage::detail::SecureDirectory opened;
    auto status = snapshot_storage::detail::SecureDirectory::Open(directory, false, opened);
    if (status.IsError()) {
        return status;
    }
    const int fd = openat(opened.Fd(), CHECKPOINT_IMAGE_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return FileError("open local checkpoint artifact");
    }
    struct stat fileStatus {};
    const int statResult = fstat(fd, &fileStatus);
    close(fd);
    if (statResult != 0) {
        return FileError("inspect local checkpoint artifact");
    }
    if (!S_ISREG(fileStatus.st_mode)) {
        return Status(StatusCode::FAILED, "local checkpoint artifact is not a regular file");
    }
    size = static_cast<uint64_t>(fileStatus.st_size);
    return Status::OK();
}

LocalSnapshotPrepareResult LocalSnapshotStore::Prepare(const LocalSnapshotCommitRequest &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto status = ValidateCommitRequest(request);
    if (status.IsError()) {
        return {status, {}};
    }
    const auto directory = SnapshotDirectory(request.snapshotID);
    std::error_code error;
    const auto fileStatus = fs::symlink_status(directory, error);
    if (!error && fs::exists(fileStatus)) {
        if (!fs::is_directory(fileStatus)) {
            return {Status(StatusCode::FAILED, "local artifact path is not a directory"), {}};
        }
        uint64_t ignoredSize = 0;
        const auto artifact = InspectArtifact(directory, ignoredSize);
        if (artifact.IsOk()) {
            return {Status::OK(), directory, true};
        }
        if (artifact.StatusCode() != StatusCode::FILE_NOT_FOUND) {
            return {artifact, directory, false};
        }
        if (!fs::is_empty(directory, error) || error) {
            return {snapshot_storage::detail::Conflict("local artifact directory is not empty"), directory, false};
        }
        return {Status::OK(), directory, false};
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        return {Status(StatusCode::FAILED, "inspect local artifact directory: " + error.message()), {}};
    }
    if (!fs::create_directory(directory, error) || error) {
        return {Status(StatusCode::FAILED, "create local artifact directory: " + error.message()), {}};
    }
    return {Status::OK(), directory, false};
}

LocalSnapshotCommitResult LocalSnapshotStore::Commit(const LocalSnapshotCommitRequest &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto status = ValidateCommitRequest(request);
    if (status.IsError()) {
        return {status, {}};
    }
    uint64_t size = 0;
    status = InspectArtifact(SnapshotDirectory(request.snapshotID), size);
    if (status.IsError()) {
        return {status, {}};
    }
    LocalSnapshotDescriptor descriptor;
    descriptor.snapshotID = request.snapshotID;
    descriptor.recoveryCandidate = request.recoveryCandidate;
    descriptor.instanceID = request.instanceID;
    descriptor.tenantHash = request.tenantHash;
    descriptor.sourceRuntimeID = request.sourceRuntimeID;
    descriptor.sourceSandboxID = request.sourceSandboxID;
    descriptor.sourceInstanceVersion = request.sourceInstanceVersion;
    descriptor.size = size;
    descriptor.createdAtUnixSeconds = request.createdAtUnixSeconds;
    if (const auto existing = records_.find(request.snapshotID); existing != records_.end()) {
        cachedBytes_ -= std::min(cachedBytes_, existing->second.size);
    }
    records_[request.snapshotID] = descriptor;
    cachedBytes_ += size;
    Touch(request.snapshotID);
    EvictIfNeeded(request.snapshotID);
    return {Status::OK(), descriptor};
}

std::vector<LocalSnapshotDescriptor> LocalSnapshotStore::List() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LocalSnapshotDescriptor> result;
    result.reserve(records_.size());
    for (const auto &[_, descriptor] : records_) {
        result.push_back(descriptor);
    }
    return result;
}

Status LocalSnapshotStore::ValidateForRestore(const std::string &snapshotID,
                                              LocalSnapshotDescriptor &descriptor)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_storage::detail::IsSafeLeafName(snapshotID)) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid local artifact ID");
    }
    uint64_t size = 0;
    auto status = InspectArtifact(SnapshotDirectory(snapshotID), size);
    if (status.IsError()) {
        return status;
    }
    const auto record = records_.find(snapshotID);
    descriptor = record == records_.end() ? LocalSnapshotDescriptor{} : record->second;
    descriptor.snapshotID = snapshotID;
    descriptor.size = size;
    Touch(snapshotID);
    return Status::OK();
}

Status LocalSnapshotStore::SetStorageLocation(
    const std::string &snapshotID, const std::string &storageBackend,
    const std::string &objectKey)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto record = records_.find(snapshotID);
    if (record == records_.end()) {
        return Status(StatusCode::FILE_NOT_FOUND, "local snapshot record is unavailable");
    }
    record->second.storageBackend = storageBackend;
    record->second.objectKey = objectKey;
    return Status::OK();
}

Status LocalSnapshotStore::PinForRestore(const std::string &snapshotID)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_storage::detail::IsSafeLeafName(snapshotID)) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid restore pin identity");
    }
    uint64_t size = 0;
    auto status = InspectArtifact(SnapshotDirectory(snapshotID), size);
    if (status.IsError()) {
        return status;
    }
    if (auto record = records_.find(snapshotID); record == records_.end()) {
        LocalSnapshotDescriptor descriptor;
        descriptor.snapshotID = snapshotID;
        descriptor.size = size;
        records_[snapshotID] = descriptor;
        cachedBytes_ += size;
    }
    ++restorePins_[snapshotID];
    Touch(snapshotID);
    return Status::OK();
}

Status LocalSnapshotStore::UnpinAfterRestore(
    const std::string &snapshotID, bool evictAfterRelease)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto pin = restorePins_.find(snapshotID);
    if (pin == restorePins_.end() || pin->second == 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "restore artifact is not pinned");
    }
    if (evictAfterRelease) {
        evictAfterUnpin_.insert(snapshotID);
    }
    if (--pin->second != 0) {
        return Status::OK();
    }
    restorePins_.erase(pin);
    if (evictAfterUnpin_.erase(snapshotID) != 0) {
        return DeleteUnlocked(snapshotID);
    }
    EvictIfNeeded({});
    return Status::OK();
}

Status LocalSnapshotStore::EvictLocalArtifact(const std::string &snapshotID)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto pin = restorePins_.find(snapshotID);
        pin != restorePins_.end() && pin->second > 0) {
        evictAfterUnpin_.insert(snapshotID);
        return Status::OK();
    }
    return DeleteUnlocked(snapshotID);
}

Status LocalSnapshotStore::DeleteRecoveryCandidatesForInstance(const std::string &instanceID)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (instanceID.empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "instance ID is required for local Snapshot cleanup");
    }
    std::vector<std::string> candidates;
    for (const auto &[snapshotID, descriptor] : records_) {
        if (descriptor.recoveryCandidate && descriptor.instanceID == instanceID) {
            candidates.emplace_back(snapshotID);
        }
    }
    for (const auto &snapshotID : candidates) {
        if (const auto pin = restorePins_.find(snapshotID);
            pin != restorePins_.end() && pin->second > 0) {
            evictAfterUnpin_.insert(snapshotID);
            continue;
        }
        const auto status = DeleteUnlocked(snapshotID);
        if (status.IsError()) {
            return status;
        }
    }
    return Status::OK();
}

void LocalSnapshotStore::Touch(const std::string &snapshotID)
{
    if (const auto existing = lruIndex_.find(snapshotID); existing != lruIndex_.end()) {
        lru_.erase(existing->second);
    }
    lru_.push_front(snapshotID);
    lruIndex_[snapshotID] = lru_.begin();
}

void LocalSnapshotStore::EvictIfNeeded(const std::string &protectedSnapshotID)
{
    size_t remainingCandidates = lru_.size();
    while (maxCacheBytes_ > 0 && cachedBytes_ > maxCacheBytes_
           && !lru_.empty() && remainingCandidates-- > 0) {
        const auto victim = lru_.back();
        const auto pin = restorePins_.find(victim);
        if (victim == protectedSnapshotID
            || (pin != restorePins_.end() && pin->second > 0)) {
            lru_.pop_back();
            lru_.push_front(victim);
            lruIndex_[victim] = lru_.begin();
            continue;
        }
        (void)DeleteUnlocked(victim);
    }
}

Status LocalSnapshotStore::DeleteUnlocked(const std::string &snapshotID)
{
    const auto forget = [this, &snapshotID]() {
        if (const auto record = records_.find(snapshotID); record != records_.end()) {
            cachedBytes_ -= std::min(cachedBytes_, record->second.size);
            records_.erase(record);
        }
        if (const auto entry = lruIndex_.find(snapshotID); entry != lruIndex_.end()) {
            lru_.erase(entry->second);
            lruIndex_.erase(entry);
        }
        restorePins_.erase(snapshotID);
        evictAfterUnpin_.erase(snapshotID);
    };
    const auto directoryPath = SnapshotDirectory(snapshotID);
    snapshot_storage::detail::SecureDirectory directory;
    auto status = snapshot_storage::detail::SecureDirectory::Open(directoryPath, false, directory);
    if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        forget();
        return Status::OK();
    }
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(directory.Fd(), CHECKPOINT_IMAGE_NAME, 0) != 0 && errno != ENOENT) {
        return FileError("delete local checkpoint artifact");
    }
    snapshot_storage::detail::SecureDirectory root;
    status = snapshot_storage::detail::SecureDirectory::Open(checkpointRoot_, false, root);
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(root.Fd(), snapshotID.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        return FileError("delete local artifact directory");
    }
    forget();
    return Status::OK();
}

Status LocalSnapshotStore::Delete(const LocalSnapshotDeleteIdentity &identity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_storage::detail::IsSafeLeafName(identity.snapshotID)) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid local artifact delete identity");
    }
    if (const auto pin = restorePins_.find(identity.snapshotID);
        pin != restorePins_.end() && pin->second > 0) {
        return Status(StatusCode::ERR_INSTANCE_BUSY, "restore artifact is pinned");
    }
    return DeleteUnlocked(identity.snapshotID);
}

}  // namespace functionsystem::function_agent
