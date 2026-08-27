/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "common/snapshot_storage/snapshot_storage.h"

#include <array>
#include <cerrno>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "common/snapshot_storage/data_system_snapshot_storage.h"
#include "common/snapshot_storage/obs_snapshot_storage.h"
#include "common/utils/ssl_config.h"

namespace functionsystem::snapshot_storage {
namespace fs = std::filesystem;
namespace {

class ScopedFileDescriptor {
public:
    explicit ScopedFileDescriptor(int fd) : fd_(fd)
    {
    }

    ~ScopedFileDescriptor()
    {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    int Get() const
    {
        return fd_;
    }

private:
    int fd_;
};

SnapshotStat InspectionFailure(StatusCode code, const std::string &message)
{
    return { Status(code, message), {} };
}

SnapshotStat InspectLocalSnapshotFileSync(const std::string &sourceFile, const std::string &snapshotID,
                                          int64_t sourceInstanceVersion,
                                          const std::function<void()> &afterInitialStat)
{
    fs::path path(sourceFile);
    auto leaf = path.filename().string();
    if (snapshotID.empty() || !detail::IsSafeLeafName(leaf)) {
        return InspectionFailure(StatusCode::ERR_PARAM_INVALID, "invalid local snapshot inspection input");
    }
    detail::SecureDirectory directory;
    auto status = detail::SecureDirectory::Open(path.parent_path(), false, directory);
    if (status.IsError()) {
        return { status, {} };
    }
    int rawFd = openat(directory.Fd(), leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (rawFd < 0) {
        auto error = errno;
        if (error == ENOENT) {
            return InspectionFailure(StatusCode::FILE_NOT_FOUND, "local snapshot file not found");
        }
        if (error == ELOOP) {
            return InspectionFailure(StatusCode::ERR_PARAM_INVALID, "local snapshot source must not be a symlink");
        }
        return InspectionFailure(StatusCode::FAILED,
                                 "failed to open local snapshot source: " + std::string(std::strerror(error)));
    }
    ScopedFileDescriptor fd(rawFd);
    struct stat fileInfo {};
    if (fstat(fd.Get(), &fileInfo) != 0) {
        return InspectionFailure(StatusCode::FAILED, "failed to stat local snapshot source");
    }
    if (!S_ISREG(fileInfo.st_mode) || fileInfo.st_size < 0) {
        return InspectionFailure(StatusCode::ERR_PARAM_INVALID, "local snapshot source must be a regular file");
    }
    if (afterInitialStat) {
        afterInitialStat();
    }
    std::array<unsigned char, 32> digest{};
    auto pinnedFile = "/proc/self/fd/" + std::to_string(fd.Get());
    if (Sha256CalculateFile(pinnedFile.c_str(), digest.data(), digest.size()) != 0) {
        return InspectionFailure(StatusCode::FAILED, "failed to hash local snapshot source");
    }
    struct stat finalFileInfo {};
    if (fstat(fd.Get(), &finalFileInfo) != 0) {
        return InspectionFailure(StatusCode::FAILED, "failed to restat local snapshot source");
    }
    const bool pinnedFileChanged = finalFileInfo.st_dev != fileInfo.st_dev || finalFileInfo.st_ino != fileInfo.st_ino ||
                                   finalFileInfo.st_size != fileInfo.st_size ||
                                   finalFileInfo.st_mtim.tv_sec != fileInfo.st_mtim.tv_sec ||
                                   finalFileInfo.st_mtim.tv_nsec != fileInfo.st_mtim.tv_nsec ||
                                   finalFileInfo.st_ctim.tv_sec != fileInfo.st_ctim.tv_sec ||
                                   finalFileInfo.st_ctim.tv_nsec != fileInfo.st_ctim.tv_nsec;
    if (pinnedFileChanged) {
        return InspectionFailure(StatusCode::SCHEDULE_CONFLICTED,
                                 "local snapshot source changed during inspection");
    }
    status = directory.VerifyPathIdentity();
    if (status.IsError()) {
        return { status, {} };
    }
    struct stat namedFileInfo {};
    if (fstatat(directory.Fd(), leaf.c_str(), &namedFileInfo, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(namedFileInfo.st_mode) || namedFileInfo.st_dev != finalFileInfo.st_dev ||
        namedFileInfo.st_ino != finalFileInfo.st_ino || namedFileInfo.st_size != finalFileInfo.st_size ||
        namedFileInfo.st_mtim.tv_sec != finalFileInfo.st_mtim.tv_sec ||
        namedFileInfo.st_mtim.tv_nsec != finalFileInfo.st_mtim.tv_nsec ||
        namedFileInfo.st_ctim.tv_sec != finalFileInfo.st_ctim.tv_sec ||
        namedFileInfo.st_ctim.tv_nsec != finalFileInfo.st_ctim.tv_nsec) {
        return InspectionFailure(StatusCode::SCHEDULE_CONFLICTED,
                                 "local snapshot source changed during inspection");
    }
    std::ostringstream sha256;
    sha256 << std::hex << std::setfill('0');
    for (auto byte : digest) {
        sha256 << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return { Status::OK(),
             { snapshotID, sourceInstanceVersion, static_cast<uint64_t>(finalFileInfo.st_size), sha256.str(), true } };
}

}  // namespace

Status ResolveStorageBackend(const std::shared_ptr<SnapshotStorage> &storage,
                             const std::string &configuredBackend,
                             std::string &resolvedBackend)
{
    if (std::dynamic_pointer_cast<ObsSnapshotStorage>(storage) != nullptr) {
        resolvedBackend = "obs";
        return Status::OK();
    }
    if (std::dynamic_pointer_cast<DataSystemSnapshotStorage>(storage) != nullptr) {
        resolvedBackend = "datasystem";
        return Status::OK();
    }
    if (configuredBackend == "obs" || configuredBackend == "datasystem") {
        resolvedBackend = configuredBackend;
        return Status::OK();
    }
    return Status(StatusCode::ERR_PARAM_INVALID,
                  "configured snapshot storage backend is missing or unsupported");
}

std::string BuildPauseSnapshotFinalKey(const std::string &tenantID, const std::string &instanceID)
{
    return "snapshots/pause/" + tenantID + "/" + instanceID + "/snapshot.img";
}

std::string BuildPauseSnapshotTemporaryKey(const std::string &tenantID, const std::string &instanceID,
                                           const std::string &snapshotID)
{
    return "snapshots/pause/" + tenantID + "/" + instanceID + "/attempts/" + snapshotID + ".tmp";
}

std::string BuildPauseSnapshotKey(const std::string &tenantHash, const std::string &instanceID,
                                  const std::string &snapshotID)
{
    return "pause/v2/" + tenantHash + "/" + instanceID + "/" + snapshotID + "/checkpoint.img";
}

std::string BuildPauseSnapshotTemporaryKey(const std::string &tenantHash, const std::string &instanceID,
                                           const std::string &snapshotID, const std::string &attemptID)
{
    return "pause/v2/" + tenantHash + "/" + instanceID + "/" + snapshotID + "/attempts/" + attemptID + ".tmp";
}

std::string BuildReusableSnapshotKey(const std::string &tenantHash, const std::string &snapshotID)
{
    return "reusable/v1/" + tenantHash + "/" + snapshotID + "/checkpoint.img";
}

std::string BuildReusableSnapshotTemporaryKey(const std::string &tenantHash, const std::string &snapshotID,
                                              const std::string &attemptID)
{
    return "reusable/v1/" + tenantHash + "/" + snapshotID + "/attempts/" + attemptID + ".tmp";
}

litebus::Future<SnapshotStat> InspectLocalSnapshotFile(const std::shared_ptr<ActorWorker> &worker,
                                                       const std::string &sourceFile, const std::string &snapshotID,
                                                       int64_t sourceInstanceVersion)
{
    if (worker == nullptr) {
        litebus::Promise<SnapshotStat> promise;
        promise.SetValue(InspectionFailure(StatusCode::ERR_PARAM_INVALID, "snapshot inspection worker is null"));
        return promise.GetFuture();
    }
    return detail::RunOnWorker<SnapshotStat>(worker, [sourceFile, snapshotID, sourceInstanceVersion]() {
        return InspectLocalSnapshotFileSync(sourceFile, snapshotID, sourceInstanceVersion, {});
    });
}

litebus::Future<Status> DeleteLocalSnapshotFile(const std::shared_ptr<ActorWorker> &worker,
                                                const std::string &sourceFile,
                                                const SnapshotObjectMetadata &expected)
{
    if (worker == nullptr) {
        return Status(StatusCode::ERR_PARAM_INVALID, "snapshot deletion worker is null");
    }
    return detail::RunOnWorker<Status>(worker, [sourceFile, expected]() {
        fs::path path(sourceFile);
        const auto leaf = path.filename().string();
        if (!detail::IsSafeLeafName(leaf)) {
            return Status(StatusCode::ERR_PARAM_INVALID, "invalid local snapshot deletion input");
        }
        detail::SecureDirectory directory;
        auto status = detail::SecureDirectory::Open(path.parent_path(), false, directory);
        if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
            return Status::OK();
        }
        if (status.IsError()) {
            return status;
        }
        int rawFd = openat(directory.Fd(), leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (rawFd < 0) {
            return errno == ENOENT
                ? Status::OK()
                : Status(StatusCode::FAILED,
                         "failed to open local snapshot for deletion: " + std::string(std::strerror(errno)));
        }
        ScopedFileDescriptor fd(rawFd);
        struct stat pinnedInfo {};
        if (fstat(fd.Get(), &pinnedInfo) != 0 || !S_ISREG(pinnedInfo.st_mode)) {
            return Status(StatusCode::ERR_PARAM_INVALID,
                          "local snapshot deletion target must be a regular file");
        }
        if (pinnedInfo.st_size < 0 || static_cast<uint64_t>(pinnedInfo.st_size) != expected.size) {
            return Status(StatusCode::FAILED, "local snapshot deletion size mismatch");
        }
        std::array<unsigned char, 32> digest{};
        const auto pinnedPath = "/proc/self/fd/" + std::to_string(fd.Get());
        if (Sha256CalculateFile(pinnedPath.c_str(), digest.data(), digest.size()) != 0) {
            return Status(StatusCode::FAILED, "failed to hash local snapshot deletion target");
        }
        std::ostringstream actual;
        actual << std::hex << std::setfill('0');
        for (auto byte : digest) {
            actual << std::setw(2) << static_cast<unsigned int>(byte);
        }
        if (actual.str() != expected.sha256) {
            return Status(StatusCode::FAILED, "local snapshot deletion sha256 mismatch");
        }
        status = directory.VerifyPathIdentity();
        if (status.IsError()) {
            return status;
        }
        struct stat namedInfo {};
        if (fstatat(directory.Fd(), leaf.c_str(), &namedInfo, AT_SYMLINK_NOFOLLOW) != 0) {
            return errno == ENOENT ? Status::OK()
                                   : Status(StatusCode::FAILED, "failed to verify local snapshot deletion target");
        }
        if (!S_ISREG(namedInfo.st_mode) || namedInfo.st_dev != pinnedInfo.st_dev
            || namedInfo.st_ino != pinnedInfo.st_ino) {
            return Status(StatusCode::SCHEDULE_CONFLICTED,
                          "local snapshot deletion target changed during verification");
        }
        if (unlinkat(directory.Fd(), leaf.c_str(), 0) != 0 && errno != ENOENT) {
            return Status(StatusCode::FAILED,
                          "failed to delete local snapshot: " + std::string(std::strerror(errno)));
        }
        if (fsync(directory.Fd()) != 0) {
            return Status(StatusCode::FAILED,
                          "failed to persist local snapshot deletion: " + std::string(std::strerror(errno)));
        }
        return Status::OK();
    });
}

namespace detail {

litebus::Future<SnapshotStat> InspectLocalSnapshotFileWithHookForTest(
    const std::shared_ptr<ActorWorker> &worker, const std::string &sourceFile, const std::string &snapshotID,
    int64_t sourceInstanceVersion, std::function<void()> afterInitialStat)
{
    if (worker == nullptr) {
        litebus::Promise<SnapshotStat> promise;
        promise.SetValue(InspectionFailure(StatusCode::ERR_PARAM_INVALID, "snapshot inspection worker is null"));
        return promise.GetFuture();
    }
    return RunOnWorker<SnapshotStat>(
        worker, [sourceFile, snapshotID, sourceInstanceVersion, afterInitialStat = std::move(afterInitialStat)]() {
            return InspectLocalSnapshotFileSync(sourceFile, snapshotID, sourceInstanceVersion, afterInitialStat);
        });
}

SecureDownloadTarget::~SecureDownloadTarget()
{
    Cleanup();
}

Status SecureDownloadTarget::Prepare(const std::string &destination)
{
    constexpr std::string_view pinnedPrefix = "/proc/self/fd/";
    if (destination.rfind(pinnedPrefix, 0) == 0) {
        const auto descriptorText = std::string_view(destination).substr(pinnedPrefix.size());
        int descriptor = 0;
        if (descriptorText.empty()) {
            return Status(StatusCode::ERR_PARAM_INVALID, "invalid pinned snapshot destination");
        }
        for (const auto character : descriptorText) {
            if (character < '0' || character > '9' ||
                descriptor > (std::numeric_limits<int>::max() - (character - '0')) / 10) {
                return Status(StatusCode::ERR_PARAM_INVALID, "invalid pinned snapshot destination");
            }
            descriptor = descriptor * 10 + (character - '0');
        }
        struct stat info {};
        if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
            return Status(StatusCode::ERR_PARAM_INVALID, "pinned snapshot destination is not a regular file");
        }
        stagingFd_ = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
        if (stagingFd_ < 0) {
            return Status(StatusCode::FAILED, "failed to duplicate pinned snapshot destination");
        }
        pinnedDestination_ = true;
        stagingPath_ = "/proc/self/fd/" + std::to_string(stagingFd_);
        return Status::OK();
    }
    fs::path path(destination);
    destinationName_ = path.filename().string();
    if (!IsSafeLeafName(destinationName_)) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid snapshot destination filename");
    }
    auto status = SecureDirectory::Open(path.parent_path(), true, directory_);
    if (status.IsError()) {
        return status;
    }
    stagingName_ = destinationName_ + ".staging." + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    stagingFd_ = openat(directory_.Fd(), stagingName_.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                        0600);
    if (stagingFd_ < 0) {
        stagingName_.clear();
        return Status(StatusCode::FAILED, "failed to create pinned snapshot staging file");
    }
    stagingPath_ = "/proc/self/fd/" + std::to_string(stagingFd_);
    return Status::OK();
}

const std::string &SecureDownloadTarget::StagingPath() const
{
    return stagingPath_;
}

Status SecureDownloadTarget::Commit(const SnapshotObjectMetadata &metadata)
{
    if (stagingFd_ < 0) {
        Cleanup();
        return Status(StatusCode::FAILED, "pinned snapshot staging file is unavailable");
    }
    struct stat info {};
    if (fstat(stagingFd_, &info) != 0 || !S_ISREG(info.st_mode) ||
        static_cast<uint64_t>(info.st_size) != metadata.size) {
        Cleanup();
        return Status(StatusCode::FAILED, "staged snapshot size or type mismatch");
    }
    std::array<unsigned char, 32> digest{};
    auto pinnedFile = "/proc/self/fd/" + std::to_string(stagingFd_);
    if (Sha256CalculateFile(pinnedFile.c_str(), digest.data(), digest.size()) != 0) {
        Cleanup();
        return Status(StatusCode::FAILED, "failed to hash staged snapshot");
    }
    std::ostringstream actual;
    actual << std::hex << std::setfill('0');
    for (auto byte : digest) {
        actual << std::setw(2) << static_cast<unsigned int>(byte);
    }
    if (actual.str() != metadata.sha256 || fsync(stagingFd_) != 0) {
        Cleanup();
        return Status(StatusCode::FAILED, "staged snapshot integrity mismatch");
    }
    if (pinnedDestination_) {
        close(stagingFd_);
        stagingFd_ = -1;
        stagingPath_.clear();
        pinnedDestination_ = false;
        return Status::OK();
    }
    auto identity = directory_.VerifyPathIdentity();
    if (identity.IsError()) {
        Cleanup();
        return identity;
    }
    struct stat destinationInfo {};
    errno = 0;
    auto destinationResult = fstatat(directory_.Fd(), destinationName_.c_str(), &destinationInfo, AT_SYMLINK_NOFOLLOW);
    if (destinationResult == 0 && !S_ISREG(destinationInfo.st_mode)) {
        Cleanup();
        return Status(StatusCode::FAILED, "snapshot destination is not a regular file");
    }
    if (destinationResult != 0 && errno != ENOENT) {
        Cleanup();
        return Status(StatusCode::FAILED, "failed to inspect snapshot destination");
    }
    struct stat namedStagingInfo {};
    if (fstatat(directory_.Fd(), stagingName_.c_str(), &namedStagingInfo, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(namedStagingInfo.st_mode) || namedStagingInfo.st_dev != info.st_dev ||
        namedStagingInfo.st_ino != info.st_ino) {
        Cleanup();
        return Status(StatusCode::SCHEDULE_CONFLICTED, "snapshot staging leaf changed before publish");
    }
    if (renameat(directory_.Fd(), stagingName_.c_str(), directory_.Fd(), destinationName_.c_str()) != 0) {
        Cleanup();
        return Status(StatusCode::FAILED, "failed to atomically publish downloaded snapshot");
    }
    if (fsync(directory_.Fd()) != 0) {
        (void)unlinkat(directory_.Fd(), destinationName_.c_str(), 0);
        (void)fsync(directory_.Fd());
        Cleanup();
        return Status(StatusCode::FAILED, "failed to persist downloaded snapshot publication");
    }
    identity = directory_.VerifyPathIdentity();
    struct stat publishedInfo {};
    if (identity.IsError() ||
        fstatat(directory_.Fd(), destinationName_.c_str(), &publishedInfo, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(publishedInfo.st_mode) || publishedInfo.st_dev != info.st_dev || publishedInfo.st_ino != info.st_ino) {
        (void)unlinkat(directory_.Fd(), destinationName_.c_str(), 0);
        (void)fsync(directory_.Fd());
        Cleanup();
        return identity.IsError() ? identity
                                  : Status(StatusCode::SCHEDULE_CONFLICTED,
                                           "published snapshot identity changed during commit");
    }
    stagingName_.clear();
    close(stagingFd_);
    stagingFd_ = -1;
    stagingPath_.clear();
    return Status::OK();
}

void SecureDownloadTarget::Cleanup()
{
    if (!stagingName_.empty() && directory_.Fd() >= 0) {
        (void)unlinkat(directory_.Fd(), stagingName_.c_str(), 0);
        stagingName_.clear();
    }
    if (stagingFd_ >= 0) {
        close(stagingFd_);
        stagingFd_ = -1;
    }
    stagingPath_.clear();
    pinnedDestination_ = false;
}

bool MetadataEqual(const SnapshotObjectMetadata &left, const SnapshotObjectMetadata &right)
{
    return MetadataIdentityEqual(left, right) && left.complete == right.complete;
}

bool MetadataIdentityEqual(const SnapshotObjectMetadata &left, const SnapshotObjectMetadata &right)
{
    return left.snapshotID == right.snapshotID && left.sourceInstanceVersion == right.sourceInstanceVersion &&
           left.size == right.size && left.sha256 == right.sha256 &&
           left.expiresAtUnixSeconds == right.expiresAtUnixSeconds;
}

bool IsExpired(const SnapshotObjectMetadata &metadata)
{
    return metadata.expiresAtUnixSeconds > 0
           && metadata.expiresAtUnixSeconds <= static_cast<int64_t>(std::time(nullptr));
}

Status ValidateFile(const std::string &path, const SnapshotObjectMetadata &metadata)
{
    std::error_code error;
    auto fileStatus = fs::symlink_status(path, error);
    if (error || !fs::is_regular_file(fileStatus)) {
        return Status(StatusCode::FAILED, "snapshot is not a regular file: " + path);
    }
    auto size = fs::file_size(path, error);
    if (error || size != metadata.size) {
        return Status(StatusCode::FAILED, "snapshot size mismatch: " + path);
    }
    std::array<unsigned char, 32> digest{};
    if (Sha256CalculateFile(path.c_str(), digest.data(), digest.size()) != 0) {
        return Status(StatusCode::FAILED, "failed to hash snapshot: " + path);
    }
    std::ostringstream actual;
    actual << std::hex << std::setfill('0');
    for (auto byte : digest) {
        actual << std::setw(2) << static_cast<unsigned int>(byte);
    }
    if (actual.str() != metadata.sha256) {
        return Status(StatusCode::FAILED, "snapshot sha256 mismatch: " + path);
    }
    return Status::OK();
}

Status Conflict(const std::string &message)
{
    return Status(StatusCode::SCHEDULE_CONFLICTED, message);
}

}  // namespace detail
}  // namespace functionsystem::snapshot_storage
