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
#include <vector>
#include <zlib.h>

#include "common/snapshot_storage/data_system_snapshot_storage.h"
#include "common/snapshot_storage/obs_snapshot_storage.h"
#include "common/utils/resume_identity.h"
#include "common/utils/ssl_config.h"
#include "openssl/sha.h"

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

SnapshotPublicationFile PrepareSnapshotPublicationFileSync(
    const std::string &sourceFile, bool compress)
{
    if (!compress) {
        return { Status::OK(), sourceFile, false, 0, {}, false };
    }
    int input = open(sourceFile.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) {
        return { Status(StatusCode::FAILED, "failed to open snapshot for compression"), {}, false, 0, {}, false };
    }
    ScopedFileDescriptor source(input);
    struct stat sourceInfo {};
    if (fstat(source.Get(), &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode)) {
        return { Status(StatusCode::ERR_PARAM_INVALID,
                        "snapshot compression source is not a regular file"), {}, false, 0, {}, false };
    }

    std::string pattern = sourceFile + ".publish-XXXXXX";
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    int output = mkstemp(writable.data());
    if (output < 0) {
        return { Status(StatusCode::FAILED,
                        "failed to create compressed snapshot staging file"), {}, false, 0, {}, false };
    }
    const std::string outputPath(writable.data());
    ScopedFileDescriptor destination(output);
    z_stream stream {};
    if (deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        (void)unlink(outputPath.c_str());
        return { Status(StatusCode::FAILED,
                        "failed to initialize snapshot gzip stream"), {}, false, 0, {}, false };
    }

    SHA256_CTX digestContext {};
    if (SHA256_Init(&digestContext) != 1) {
        (void)deflateEnd(&stream);
        (void)unlink(outputPath.c_str());
        return { Status(StatusCode::FAILED,
                        "failed to initialize compressed snapshot digest"), {}, false, 0, {}, false };
    }

    std::array<unsigned char, 1024 * 1024> inputBuffer {};
    std::array<unsigned char, 1024 * 1024> outputBuffer {};
    Status status = Status::OK();
    uint64_t outputSize = 0;
    auto writeCompressed = [&](size_t size) {
        size_t offset = 0;
        while (offset < size) {
            const auto written = write(destination.Get(), outputBuffer.data() + offset, size - offset);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return Status(StatusCode::FAILED, "failed to write compressed snapshot artifact");
            }
            if (written == 0) {
                return Status(StatusCode::FAILED, "short write of compressed snapshot artifact");
            }
            offset += static_cast<size_t>(written);
        }
        if (SHA256_Update(&digestContext, outputBuffer.data(), size) != 1) {
            return Status(StatusCode::FAILED, "failed to update compressed snapshot digest");
        }
        outputSize += size;
        return Status::OK();
    };
    while (status.IsOk()) {
        const auto count = read(source.Get(), inputBuffer.data(), inputBuffer.size());
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            status = Status(StatusCode::FAILED,
                            "failed to read snapshot during compression");
            break;
        }
        stream.next_in = inputBuffer.data();
        stream.avail_in = static_cast<uInt>(count);
        while (status.IsOk() && stream.avail_in > 0) {
            stream.next_out = outputBuffer.data();
            stream.avail_out = static_cast<uInt>(outputBuffer.size());
            if (deflate(&stream, Z_NO_FLUSH) != Z_OK) {
                status = Status(StatusCode::FAILED, "failed to compress snapshot artifact");
                break;
            }
            status = writeCompressed(outputBuffer.size() - stream.avail_out);
        }
    }

    struct stat finalSourceInfo {};
    if (status.IsOk() &&
        (fstat(source.Get(), &finalSourceInfo) != 0 ||
         finalSourceInfo.st_dev != sourceInfo.st_dev || finalSourceInfo.st_ino != sourceInfo.st_ino ||
         finalSourceInfo.st_size != sourceInfo.st_size ||
         finalSourceInfo.st_mtim.tv_sec != sourceInfo.st_mtim.tv_sec ||
         finalSourceInfo.st_mtim.tv_nsec != sourceInfo.st_mtim.tv_nsec ||
         finalSourceInfo.st_ctim.tv_sec != sourceInfo.st_ctim.tv_sec ||
         finalSourceInfo.st_ctim.tv_nsec != sourceInfo.st_ctim.tv_nsec)) {
        status = Status(StatusCode::SCHEDULE_CONFLICTED,
                        "snapshot source changed during compression");
    }

    int deflateResult = Z_OK;
    while (status.IsOk() && deflateResult != Z_STREAM_END) {
        stream.next_out = outputBuffer.data();
        stream.avail_out = static_cast<uInt>(outputBuffer.size());
        deflateResult = deflate(&stream, Z_FINISH);
        if (deflateResult != Z_OK && deflateResult != Z_STREAM_END) {
            status = Status(StatusCode::FAILED, "failed to finalize snapshot gzip stream");
            break;
        }
        status = writeCompressed(outputBuffer.size() - stream.avail_out);
    }
    if (deflateEnd(&stream) != Z_OK && status.IsOk()) {
        status = Status(StatusCode::FAILED, "failed to release snapshot gzip stream");
    }
    if (status.IsOk() && fsync(destination.Get()) != 0) {
        status = Status(StatusCode::FAILED, "failed to sync compressed snapshot artifact");
    }
    if (status.IsError()) {
        (void)unlink(outputPath.c_str());
        return { status, {}, false, 0, {}, false };
    }

    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
    if (SHA256_Final(digest.data(), &digestContext) != 1) {
        (void)unlink(outputPath.c_str());
        return { Status(StatusCode::FAILED,
                        "failed to finalize compressed snapshot digest"), {}, false, 0, {}, false };
    }
    std::ostringstream sha256;
    sha256 << std::hex << std::setfill('0');
    for (auto byte : digest) {
        sha256 << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return { Status::OK(), outputPath, true, outputSize, sha256.str(), true };
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

std::string StableTenantHash(const std::string &tenantID)
{
    return resume_identity::Sha256Hex(tenantID);
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

litebus::Future<SnapshotPublicationFile> PrepareSnapshotPublicationFile(
    const std::shared_ptr<ActorWorker> &worker, const std::string &sourceFile,
    bool compress)
{
    if (worker == nullptr || sourceFile.empty()) {
        litebus::Promise<SnapshotPublicationFile> promise;
        promise.SetValue({ Status(StatusCode::ERR_PARAM_INVALID,
                                  "snapshot publication preparation input is invalid"), {}, false, 0, {}, false });
        return promise.GetFuture();
    }
    return detail::RunOnWorker<SnapshotPublicationFile>(
        worker, [sourceFile, compress]() {
            return PrepareSnapshotPublicationFileSync(sourceFile, compress);
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
