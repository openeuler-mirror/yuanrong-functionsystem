/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 * See the LICENSE file in this repository for the complete license text.
 */

#include "function_agent/snapshot/local_snapshot_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/snapshot_storage/secure_directory.h"
#include "common/snapshot_storage/snapshot_storage.h"
#include "common/utils/ssl_config.h"

namespace functionsystem::function_agent {
namespace {

namespace fs = std::filesystem;
using snapshot_storage::detail::SecureDirectory;

constexpr uint32_t LOCAL_SNAPSHOT_SCHEMA_VERSION = 1;
constexpr std::size_t MAX_META_BYTES = 64 * 1024;
constexpr char CHECKPOINT_IMAGE_NAME[] = "checkpoint.img";
constexpr char SNAPSHOT_META_NAME[] = "snapshot.meta";
constexpr char CHECKPOINT_ARTIFACT_FORMAT[] = "sandboxd-checkpoint-v1";
constexpr uint32_t CHECKPOINT_ARTIFACT_FORMAT_VERSION = 1;

Status FileError(const std::string &operation, int error = errno)
{
    return Status(error == ENOENT ? StatusCode::FILE_NOT_FOUND : StatusCode::FAILED,
                  operation + ": " + std::strerror(error));
}

bool SameCommitIdentity(const LocalSnapshotDescriptor &descriptor,
                        const LocalSnapshotCommitRequest &request)
{
    return descriptor.snapshotID == request.snapshotID
        && descriptor.anonymous == request.anonymous
        && (!request.anonymous || descriptor.instanceID == request.instanceID)
        && descriptor.tenantHash == request.tenantHash
        && (request.sourceRuntimeID.empty() || descriptor.sourceRuntimeID == request.sourceRuntimeID)
        && (request.sourceSandboxID.empty() || descriptor.sourceSandboxID == request.sourceSandboxID)
        && (request.sourceInstanceVersion <= 0
            || descriptor.sourceInstanceVersion == request.sourceInstanceVersion)
        && descriptor.runtimeClass == request.runtimeClass
        && descriptor.architecture == request.architecture
        && descriptor.artifactFormat == request.artifactFormat
        && descriptor.artifactFormatVersion == request.artifactFormatVersion;
}

nlohmann::json EncodeDescriptor(const LocalSnapshotDescriptor &descriptor)
{
    return {
        {"schemaVersion", LOCAL_SNAPSHOT_SCHEMA_VERSION},
        {"snapshotID", descriptor.snapshotID},
        {"anonymous", descriptor.anonymous},
        {"instanceID", descriptor.instanceID},
        {"tenantHash", descriptor.tenantHash},
        {"sourceRuntimeID", descriptor.sourceRuntimeID},
        {"sourceSandboxID", descriptor.sourceSandboxID},
        {"sourceInstanceVersion", descriptor.sourceInstanceVersion},
        {"generation", descriptor.generation},
        {"runtimeClass", descriptor.runtimeClass},
        {"architecture", descriptor.architecture},
        {"artifactFormat", descriptor.artifactFormat},
        {"artifactFormatVersion", descriptor.artifactFormatVersion},
        {"size", descriptor.size},
        {"sha256", descriptor.sha256},
        {"createdAtUnixSeconds", descriptor.createdAtUnixSeconds},
    };
}

Status DecodeDescriptor(const std::string &encoded, LocalSnapshotDescriptor &descriptor)
{
    try {
        const auto json = nlohmann::json::parse(encoded);
        if (!json.is_object() || json.at("schemaVersion").get<uint32_t>() != LOCAL_SNAPSHOT_SCHEMA_VERSION) {
            return Status(StatusCode::ERR_PARAM_INVALID, "unsupported local snapshot metadata schema");
        }
        descriptor.snapshotID = json.at("snapshotID").get<std::string>();
        descriptor.anonymous = json.at("anonymous").get<bool>();
        descriptor.instanceID = json.at("instanceID").get<std::string>();
        descriptor.tenantHash = json.at("tenantHash").get<std::string>();
        descriptor.sourceRuntimeID = json.at("sourceRuntimeID").get<std::string>();
        descriptor.sourceSandboxID = json.at("sourceSandboxID").get<std::string>();
        descriptor.sourceInstanceVersion = json.at("sourceInstanceVersion").get<int64_t>();
        descriptor.generation = json.at("generation").get<uint64_t>();
        descriptor.runtimeClass = json.at("runtimeClass").get<std::string>();
        descriptor.architecture = json.at("architecture").get<std::string>();
        descriptor.artifactFormat = json.at("artifactFormat").get<std::string>();
        descriptor.artifactFormatVersion = json.at("artifactFormatVersion").get<uint32_t>();
        descriptor.size = json.at("size").get<uint64_t>();
        descriptor.sha256 = json.at("sha256").get<std::string>();
        descriptor.createdAtUnixSeconds = json.at("createdAtUnixSeconds").get<int64_t>();
    } catch (const std::exception &error) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "invalid local snapshot metadata: " + std::string(error.what()));
    }
    if (!snapshot_storage::detail::IsSafeLeafName(descriptor.snapshotID)
        || descriptor.instanceID.empty() || descriptor.runtimeClass.empty()
        || descriptor.architecture.empty() || descriptor.artifactFormat.empty()
        || descriptor.artifactFormatVersion == 0 || descriptor.size == 0
        || descriptor.sha256.size() != 64) {
        return Status(StatusCode::ERR_PARAM_INVALID, "incomplete local snapshot metadata");
    }
    return Status::OK();
}

Status WriteAll(int fd, const std::string &contents)
{
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto written = write(fd, contents.data() + offset, contents.size() - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FileError("failed to write local snapshot metadata");
        }
        offset += static_cast<std::size_t>(written);
    }
    return Status::OK();
}

Status ReadAll(int fd, std::size_t size, std::string &contents)
{
    contents.assign(size, '\0');
    std::size_t offset = 0;
    while (offset < size) {
        const auto count = read(fd, contents.data() + offset, size - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return FileError("failed to read local snapshot metadata");
        }
        if (count == 0) {
            return Status(StatusCode::FAILED, "local snapshot metadata is truncated");
        }
        offset += static_cast<std::size_t>(count);
    }
    return Status::OK();
}

}  // namespace

LocalSnapshotStore::LocalSnapshotStore(fs::path checkpointRoot)
    : checkpointRoot_(fs::absolute(std::move(checkpointRoot)).lexically_normal())
{
}

Status LocalSnapshotStore::ValidateCommitRequest(const LocalSnapshotCommitRequest &request) const
{
    if (!snapshot_storage::detail::IsSafeLeafName(request.snapshotID)
        || request.instanceID.empty() || request.runtimeClass.empty()
        || request.architecture.empty() || request.artifactFormat.empty()
        || request.artifactFormatVersion == 0 || request.createdAtUnixSeconds <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "incomplete local snapshot commit request");
    }
    return Status::OK();
}

fs::path LocalSnapshotStore::SnapshotDirectory(const std::string &snapshotID) const
{
    return (checkpointRoot_ / snapshotID).lexically_normal();
}

LocalSnapshotPrepareResult LocalSnapshotStore::Prepare(const LocalSnapshotCommitRequest &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto status = ValidateCommitRequest(request);
    if (status.IsError()) {
        return {status, {}};
    }
    const auto directoryPath = SnapshotDirectory(request.snapshotID);
    SecureDirectory directory;
    status = SecureDirectory::Open(directoryPath, false, directory);
    if (status.IsOk()) {
        LocalSnapshotDescriptor descriptor;
        const auto existing = ReadDescriptor(directoryPath, descriptor);
        if (existing.IsOk() && SameCommitIdentity(descriptor, request)) {
            status = ValidateCommittedImage(directoryPath, descriptor);
            return status.IsError() ? LocalSnapshotPrepareResult{status, directoryPath}
                                    : LocalSnapshotPrepareResult{Status::OK(), directoryPath, true};
        }
        if (existing.StatusCode() == StatusCode::FILE_NOT_FOUND) {
            return {Status::OK(), directoryPath, false};
        }
        return {snapshot_storage::detail::Conflict("local snapshot directory is already owned"), directoryPath};
    }
    if (status.StatusCode() != StatusCode::FILE_NOT_FOUND) {
        return {status, {}};
    }

    SecureDirectory root;
    status = SecureDirectory::Open(checkpointRoot_, true, root);
    if (status.IsError()) {
        return {status, {}};
    }
    if (mkdirat(root.Fd(), request.snapshotID.c_str(), 0750) != 0 && errno != EEXIST) {
        return {FileError("failed to create local snapshot directory"), {}};
    }
    status = SecureDirectory::Open(directoryPath, false, directory);
    return status.IsError() ? LocalSnapshotPrepareResult{status, {}}
                            : LocalSnapshotPrepareResult{Status::OK(), directoryPath, false};
}

Status LocalSnapshotStore::InspectImage(const fs::path &directoryPath, uint64_t &size, std::string &sha256) const
{
    SecureDirectory directory;
    auto status = SecureDirectory::Open(directoryPath, false, directory);
    if (status.IsError()) {
        return status;
    }
    const int rawFd = openat(directory.Fd(), CHECKPOINT_IMAGE_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (rawFd < 0) {
        return FileError("failed to open local checkpoint image");
    }
    struct ScopedFd {
        int fd;
        ~ScopedFd() { close(fd); }
    } image{rawFd};
    struct ::stat initial {};
    if (fstat(image.fd, &initial) != 0 || !S_ISREG(initial.st_mode) || initial.st_size <= 0) {
        return Status(StatusCode::ERR_PARAM_INVALID, "local checkpoint image is not a non-empty regular file");
    }
    std::array<unsigned char, 32> digest{};
    const auto pinned = "/proc/self/fd/" + std::to_string(image.fd);
    if (Sha256CalculateFile(pinned.c_str(), digest.data(), digest.size()) != 0) {
        return Status(StatusCode::FAILED, "failed to hash local checkpoint image");
    }
    if (fsync(image.fd) != 0) {
        return FileError("failed to sync local checkpoint image");
    }
    struct ::stat inspected {};
    if (fstat(image.fd, &inspected) != 0 || inspected.st_dev != initial.st_dev || inspected.st_ino != initial.st_ino
        || inspected.st_size != initial.st_size || inspected.st_mtim.tv_sec != initial.st_mtim.tv_sec
        || inspected.st_mtim.tv_nsec != initial.st_mtim.tv_nsec) {
        return Status(StatusCode::SCHEDULE_CONFLICTED, "local checkpoint image changed during inspection");
    }
    if (auto identity = directory.VerifyPathIdentity(); identity.IsError()) {
        return identity;
    }
    struct ::stat named {};
    if (fstatat(directory.Fd(), CHECKPOINT_IMAGE_NAME, &named, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(named.st_mode) || named.st_dev != inspected.st_dev || named.st_ino != inspected.st_ino) {
        return Status(StatusCode::SCHEDULE_CONFLICTED, "local checkpoint image path changed during inspection");
    }
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        encoded << std::setw(2) << static_cast<unsigned int>(byte);
    }
    size = static_cast<uint64_t>(inspected.st_size);
    sha256 = encoded.str();
    return Status::OK();
}

Status LocalSnapshotStore::ValidateCommittedImage(const fs::path &directoryPath,
                                                   const LocalSnapshotDescriptor &descriptor) const
{
    uint64_t actualSize = 0;
    std::string actualSha256;
    const auto status = InspectImage(directoryPath, actualSize, actualSha256);
    if (status.IsError()) {
        return status;
    }
    return actualSize != descriptor.size || actualSha256 != descriptor.sha256
               ? snapshot_storage::detail::Conflict("local snapshot image does not match committed digest")
               : Status::OK();
}

Status LocalSnapshotStore::ReadDescriptor(const fs::path &directoryPath,
                                          LocalSnapshotDescriptor &descriptor) const
{
    SecureDirectory directory;
    auto status = SecureDirectory::Open(directoryPath, false, directory);
    if (status.IsError()) {
        return status;
    }
    const int rawFd = openat(directory.Fd(), SNAPSHOT_META_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (rawFd < 0) {
        return FileError("failed to open local snapshot metadata");
    }
    struct ScopedFd {
        int fd;
        ~ScopedFd() { close(fd); }
    } meta{rawFd};
    struct ::stat info {};
    if (fstat(meta.fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0
        || static_cast<uint64_t>(info.st_size) > MAX_META_BYTES) {
        return Status(StatusCode::ERR_PARAM_INVALID, "local snapshot metadata is not a bounded regular file");
    }
    std::string encoded;
    status = ReadAll(meta.fd, static_cast<std::size_t>(info.st_size), encoded);
    if (status.IsError()) {
        return status;
    }
    status = DecodeDescriptor(encoded, descriptor);
    if (status.IsError()) {
        return status;
    }
    if (descriptor.snapshotID != directoryPath.filename().string()) {
        return Status(StatusCode::SCHEDULE_CONFLICTED, "local snapshot metadata does not match its directory");
    }
    const auto imagePath = directory.ProcPath(CHECKPOINT_IMAGE_NAME);
    std::error_code error;
    const auto imageStatus = fs::symlink_status(imagePath, error);
    if (error || !fs::is_regular_file(imageStatus) || fs::file_size(imagePath, error) != descriptor.size || error) {
        return Status(StatusCode::FAILED, "local snapshot image does not match committed metadata");
    }
    return directory.VerifyPathIdentity();
}

uint64_t LocalSnapshotStore::NextGeneration(const LocalSnapshotCommitRequest &request) const
{
    if (!request.anonymous) {
        return 0;
    }
    uint64_t generation = 0;
    for (const auto &descriptor : ListUnlocked()) {
        if (descriptor.anonymous && descriptor.instanceID == request.instanceID) {
            generation = std::max(generation, descriptor.generation);
        }
    }
    return generation + 1;
}

LocalSnapshotCommitResult LocalSnapshotStore::Commit(const LocalSnapshotCommitRequest &request)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto status = ValidateCommitRequest(request);
    if (status.IsError()) {
        return {status, {}};
    }
    const auto directoryPath = SnapshotDirectory(request.snapshotID);
    LocalSnapshotDescriptor existing;
    status = ReadDescriptor(directoryPath, existing);
    if (status.IsOk()) {
        if (!SameCommitIdentity(existing, request)) {
            return {snapshot_storage::detail::Conflict(
                        "committed local snapshot identity conflicts with request"), {}};
        }
        status = ValidateCommittedImage(directoryPath, existing);
        return status.IsError() ? LocalSnapshotCommitResult{status, {}}
                                : LocalSnapshotCommitResult{Status::OK(), existing};
    }
    if (status.StatusCode() != StatusCode::FILE_NOT_FOUND) {
        return {status, {}};
    }

    LocalSnapshotDescriptor descriptor;
    descriptor.snapshotID = request.snapshotID;
    descriptor.anonymous = request.anonymous;
    descriptor.instanceID = request.instanceID;
    descriptor.tenantHash = request.tenantHash;
    descriptor.sourceRuntimeID = request.sourceRuntimeID;
    descriptor.sourceSandboxID = request.sourceSandboxID;
    descriptor.sourceInstanceVersion = request.sourceInstanceVersion;
    descriptor.generation = NextGeneration(request);
    descriptor.runtimeClass = request.runtimeClass;
    descriptor.architecture = request.architecture;
    descriptor.artifactFormat = request.artifactFormat;
    descriptor.artifactFormatVersion = request.artifactFormatVersion;
    descriptor.createdAtUnixSeconds = request.createdAtUnixSeconds;
    status = InspectImage(directoryPath, descriptor.size, descriptor.sha256);
    if (status.IsError()) {
        return {status, {}};
    }

    SecureDirectory directory;
    status = SecureDirectory::Open(directoryPath, false, directory);
    if (status.IsError()) {
        return {status, {}};
    }
    const auto temporaryName = std::string(SNAPSHOT_META_NAME) + ".tmp."
        + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    const int rawFd = openat(directory.Fd(), temporaryName.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (rawFd < 0) {
        return {FileError("failed to create local snapshot metadata"), {}};
    }
    struct ScopedMeta {
        int directoryFd;
        int fd;
        std::string name;
        bool committed{ false };
        ~ScopedMeta()
        {
            close(fd);
            if (!committed) {
                unlinkat(directoryFd, name.c_str(), 0);
            }
        }
    } meta{directory.Fd(), rawFd, temporaryName};
    const auto encoded = EncodeDescriptor(descriptor).dump();
    status = WriteAll(meta.fd, encoded);
    if (status.IsError() || fsync(meta.fd) != 0) {
        return {status.IsError() ? status : FileError("failed to sync local snapshot metadata"), {}};
    }
    if (renameat(directory.Fd(), temporaryName.c_str(), directory.Fd(), SNAPSHOT_META_NAME) != 0) {
        return {FileError("failed to commit local snapshot metadata"), {}};
    }
    meta.committed = true;
    if (fsync(directory.Fd()) != 0) {
        return {FileError("failed to sync local snapshot directory"), {}};
    }
    return {Status::OK(), descriptor};
}

std::vector<LocalSnapshotDescriptor> LocalSnapshotStore::ListUnlocked() const
{
    std::vector<LocalSnapshotDescriptor> snapshots;
    std::error_code error;
    if (!fs::exists(checkpointRoot_, error) || error) {
        return snapshots;
    }
    for (fs::directory_iterator iter(checkpointRoot_, error), end; !error && iter != end; iter.increment(error)) {
        const auto entryStatus = iter->symlink_status(error);
        if (error || !fs::is_directory(entryStatus)) {
            error.clear();
            continue;
        }
        LocalSnapshotDescriptor descriptor;
        const auto status = ReadDescriptor(iter->path(), descriptor);
        if (status.IsOk()) {
            snapshots.emplace_back(std::move(descriptor));
        } else {
            YRLOG_WARN("local snapshot scan ignored invalid directory({}): {}",
                       iter->path().filename().string(), status.RawMessage());
        }
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto &left, const auto &right) {
        return left.snapshotID < right.snapshotID;
    });
    return snapshots;
}

std::vector<LocalSnapshotDescriptor> LocalSnapshotStore::List() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ListUnlocked();
}

Status LocalSnapshotStore::ValidateForRestore(const std::string &snapshotID,
                                              LocalSnapshotDescriptor &descriptor) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_storage::detail::IsSafeLeafName(snapshotID)) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid local snapshot ID");
    }
    const auto directoryPath = SnapshotDirectory(snapshotID);
    auto status = ReadDescriptor(directoryPath, descriptor);
    if (status.IsError()) {
        return status;
    }
    if (descriptor.artifactFormat != CHECKPOINT_ARTIFACT_FORMAT
        || descriptor.artifactFormatVersion != CHECKPOINT_ARTIFACT_FORMAT_VERSION) {
        return snapshot_storage::detail::Conflict("local snapshot artifact format is not supported");
    }
    return ValidateCommittedImage(directoryPath, descriptor);
}

Status LocalSnapshotStore::Delete(const LocalSnapshotDeleteIdentity &identity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_storage::detail::IsSafeLeafName(identity.snapshotID)
        || identity.expectedSize == 0 || identity.expectedSha256.empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid local snapshot delete identity");
    }
    const auto directoryPath = SnapshotDirectory(identity.snapshotID);
    LocalSnapshotDescriptor descriptor;
    auto status = ReadDescriptor(directoryPath, descriptor);
    if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        return Status::OK();
    }
    if (status.IsError()) {
        return status;
    }
    if (descriptor.generation != identity.expectedGeneration || descriptor.size != identity.expectedSize
        || descriptor.sha256 != identity.expectedSha256) {
        return snapshot_storage::detail::Conflict("local snapshot delete identity does not match metadata");
    }
    status = ValidateCommittedImage(directoryPath, descriptor);
    if (status.IsError()) {
        return status.StatusCode() == StatusCode::SCHEDULE_CONFLICTED
                   ? snapshot_storage::detail::Conflict("local snapshot delete digest does not match artifact")
                   : status;
    }

    SecureDirectory directory;
    status = SecureDirectory::Open(directoryPath, false, directory);
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(directory.Fd(), SNAPSHOT_META_NAME, 0) != 0 && errno != ENOENT) {
        return FileError("failed to delete local snapshot metadata");
    }
    if (unlinkat(directory.Fd(), CHECKPOINT_IMAGE_NAME, 0) != 0 && errno != ENOENT) {
        return FileError("failed to delete local checkpoint image");
    }
    SecureDirectory root;
    status = SecureDirectory::Open(checkpointRoot_, false, root);
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(root.Fd(), identity.snapshotID.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        return FileError("failed to delete local snapshot directory");
    }
    return Status::OK();
}

}  // namespace functionsystem::function_agent
