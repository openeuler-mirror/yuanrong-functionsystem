/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "runtime_manager/ckpt/pause_artifact_path_manager.h"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include "common/snapshot_storage/secure_directory.h"
#include "common/utils/resume_identity.h"

namespace functionsystem::runtime_manager {
namespace {

namespace fs = std::filesystem;
namespace storage_detail = snapshot_storage::detail;

bool IsSafeComponent(const std::string &component)
{
    return !component.empty() && component != "." && component != ".." &&
           component.find('/') == std::string::npos && component.find('\\') == std::string::npos &&
           component.find('\0') == std::string::npos;
}

Status InvalidPath(const std::string &message)
{
    return Status(StatusCode::ERR_PARAM_INVALID, message);
}

Status FileError(const std::string &operation)
{
    const auto code = errno == ENOENT ? StatusCode::FILE_NOT_FOUND : StatusCode::FAILED;
    return Status(code, operation + ": " + std::strerror(errno));
}

Status RemoveManagedTreeAt(int parentFd, const std::string &name)
{
    struct stat info {};
    if (fstatat(parentFd, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return errno == ENOENT ? Status::OK() : FileError("failed to inspect restore cache entry");
    }
    if (!S_ISDIR(info.st_mode)) {
        return unlinkat(parentFd, name.c_str(), 0) == 0 ? Status::OK()
                                                        : FileError("failed to remove restore cache file");
    }
    const int directoryFd = openat(parentFd, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directoryFd < 0) {
        return FileError("failed to open restore cache directory");
    }
    const int duplicate = dup(directoryFd);
    DIR *stream = duplicate < 0 ? nullptr : fdopendir(duplicate);
    if (stream == nullptr) {
        if (duplicate >= 0) {
            close(duplicate);
        }
        close(directoryFd);
        return FileError("failed to enumerate restore cache directory");
    }

    Status result = Status::OK();
    errno = 0;
    while (const auto *entry = readdir(stream)) {
        const std::string child(entry->d_name);
        if (child == "." || child == "..") {
            continue;
        }
        if (!storage_detail::IsSafeLeafName(child)) {
            result = Status(StatusCode::FAILED, "unsafe restore cache entry");
            break;
        }
        result = RemoveManagedTreeAt(directoryFd, child);
        if (result.IsError()) {
            break;
        }
        errno = 0;
    }
    const auto readError = errno;
    closedir(stream);
    close(directoryFd);
    if (result.IsError()) {
        return result;
    }
    if (readError != 0) {
        errno = readError;
        return FileError("failed to enumerate restore cache directory");
    }
    return unlinkat(parentFd, name.c_str(), AT_REMOVEDIR) == 0
               ? Status::OK()
               : FileError("failed to remove restore cache directory");
}

Status RemoveEmptyManagedDirectory(const fs::path &path)
{
    const auto name = path.filename().string();
    if (!storage_detail::IsSafeLeafName(name)) {
        return InvalidPath("invalid empty checkpoint directory component");
    }
    storage_detail::SecureDirectory parent;
    auto status = storage_detail::SecureDirectory::Open(path.parent_path(), false, parent);
    if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
        return Status::OK();
    }
    if (status.IsError()) {
        return status;
    }
    if (unlinkat(parent.Fd(), name.c_str(), AT_REMOVEDIR) == 0 || errno == ENOENT || errno == ENOTEMPTY ||
        errno == EEXIST) {
        return Status::OK();
    }
    return FileError("failed to prune empty checkpoint directory");
}

Status PruneEmptyManagedAncestors(fs::path path, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) {
        auto status = RemoveEmptyManagedDirectory(path);
        if (status.IsError()) {
            return status;
        }
        path = path.parent_path();
    }
    return Status::OK();
}

template <typename T, typename F>
litebus::Future<T> RunOnWorker(const std::shared_ptr<ActorWorker> &worker, F &&operation)
{
    auto promise = std::make_shared<litebus::Promise<T>>();
    auto future = promise->GetFuture();
    (void)worker->AsyncWork([promise, operation = std::forward<F>(operation)]() mutable {
        try {
            promise->SetValue(operation());
        } catch (const std::exception &error) {
            if constexpr (std::is_same_v<T, Status>) {
                promise->SetValue(Status(StatusCode::FAILED, error.what()));
            } else {
                promise->SetValue(T{ Status(StatusCode::FAILED, error.what()), {} });
            }
        }
    });
    return future;
}

}  // namespace

PauseArtifactPathManager::PauseArtifactPathManager(fs::path checkpointRoot, std::string tenantHash,
                                                   std::string instanceID,
                                                   std::shared_ptr<ActorWorker> worker)
    : checkpointRoot_(fs::absolute(std::move(checkpointRoot)).lexically_normal()),
      tenantHash_(std::move(tenantHash)), instanceID_(std::move(instanceID)),
      worker_(worker == nullptr ? std::make_shared<ActorWorker>() : std::move(worker))
{
}

std::string PauseArtifactPathManager::StableTenantHash(const std::string &tenantID)
{
    return resume_identity::Sha256Hex(tenantID);
}

PauseArtifactPath PauseArtifactPathManager::PlanSourceArtifact(const std::string &snapshotID) const
{
    if (!IsSafeComponent(tenantHash_) || !IsSafeComponent(instanceID_) || !IsSafeComponent(snapshotID)) {
        return { InvalidPath("invalid pause artifact path component"), {} };
    }
    const auto path = checkpointRoot_ / "pause" / tenantHash_ / instanceID_ / snapshotID / "checkpoint.img";
    return { Status::OK(), path };
}

PauseArtifactPath PauseArtifactPathManager::PlanRestoreAttempt(
    const std::string &snapshotID, const std::string &targetAttemptID) const
{
    if (!IsSafeComponent(tenantHash_) || !IsSafeComponent(instanceID_) || !IsSafeComponent(snapshotID) ||
        !IsSafeComponent(targetAttemptID)) {
        return { InvalidPath("invalid restore attempt path component"), {} };
    }
    const auto path = checkpointRoot_ / "restore" / tenantHash_ / instanceID_ / snapshotID / "attempts" /
                      targetAttemptID / "checkpoint.img";
    return { Status::OK(), path };
}

litebus::Future<Status> PauseArtifactPathManager::DeleteRestoreAttempt(
    const std::string &snapshotID, const std::string &targetAttemptID)
{
    return RunOnWorker<Status>(worker_, [this, snapshotID, targetAttemptID]() {
        const auto attempt = PlanRestoreAttempt(snapshotID, targetAttemptID);
        if (attempt.status.IsError()) {
            return attempt.status;
        }
        storage_detail::SecureDirectory attemptsDirectory;
        auto status = storage_detail::SecureDirectory::Open(attempt.path.parent_path().parent_path(), false,
                                                            attemptsDirectory);
        if (status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
            return Status::OK();
        }
        if (status.IsError()) {
            return status;
        }
        status = attemptsDirectory.VerifyPathIdentity();
        if (status.IsError()) {
            return status;
        }
        status = RemoveManagedTreeAt(attemptsDirectory.Fd(), targetAttemptID);
        if (status.IsError()) {
            return status;
        }
        const auto attempts = attempt.path.parent_path().parent_path();
        // Prune attempts/snapshot/instance/tenant/restore while leaving the
        // configured checkpoint root intact. Non-empty ancestors are kept.
        return PruneEmptyManagedAncestors(attempts, 5);
    });
}

litebus::Future<Status> PauseArtifactPathManager::PruneSourceArtifactParents(const std::string &snapshotID)
{
    const auto source = PlanSourceArtifact(snapshotID);
    if (source.status.IsError()) {
        return source.status;
    }
    return RunOnWorker<Status>(worker_, [path = source.path]() {
        // Prune snapshot/instance/tenant/pause after checkpoint.img has gone.
        return PruneEmptyManagedAncestors(path.parent_path(), 4);
    });
}

}  // namespace functionsystem::runtime_manager
