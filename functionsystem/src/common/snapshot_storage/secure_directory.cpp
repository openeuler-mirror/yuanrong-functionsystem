/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "common/snapshot_storage/secure_directory.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace functionsystem::snapshot_storage::detail {
namespace {

Status FsError(const std::string &operation)
{
    return Status(errno == ENOENT ? StatusCode::FILE_NOT_FOUND : StatusCode::FAILED,
                  operation + ": " + std::strerror(errno));
}

Status OpenDirectoryPath(const std::filesystem::path &input, bool create, int &result)
{
    auto path = input.empty() ? std::filesystem::path(".") : input.lexically_normal();
    int current = open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (current < 0) {
        return FsError("failed to open secure directory root");
    }
    for (const auto &componentPath : path.relative_path()) {
        auto component = componentPath.string();
        if (component.empty() || component == ".") {
            continue;
        }
        if (!IsSafeLeafName(component)) {
            close(current);
            return Status(StatusCode::ERR_PARAM_INVALID, "unsafe secure directory component");
        }
        int next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && create) {
            if (mkdirat(current, component.c_str(), 0750) != 0 && errno != EEXIST) {
                auto status = FsError("failed to create secure directory component");
                close(current);
                return status;
            }
            next = openat(current, component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (next < 0) {
            auto status = FsError("failed to open secure directory component");
            close(current);
            return status;
        }
        close(current);
        current = next;
    }
    result = current;
    return Status::OK();
}

}  // namespace

SecureDirectory::SecureDirectory(int fd, std::filesystem::path path, uint64_t device, uint64_t inode)
    : fd_(fd), path_(std::move(path)), device_(device), inode_(inode)
{
}

SecureDirectory::~SecureDirectory()
{
    Close();
}

SecureDirectory::SecureDirectory(SecureDirectory &&other) noexcept
    : fd_(other.fd_), path_(std::move(other.path_)), device_(other.device_), inode_(other.inode_)
{
    other.fd_ = -1;
}

SecureDirectory &SecureDirectory::operator=(SecureDirectory &&other) noexcept
{
    if (this != &other) {
        Close();
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        device_ = other.device_;
        inode_ = other.inode_;
        other.fd_ = -1;
    }
    return *this;
}

Status SecureDirectory::Open(const std::filesystem::path &path, bool create, SecureDirectory &result)
{
    int fd = -1;
    auto status = OpenDirectoryPath(path, create, fd);
    if (status.IsError()) {
        return status;
    }
    struct stat info {};
    if (fstat(fd, &info) != 0) {
        status = FsError("failed to stat secure directory");
        close(fd);
        return status;
    }
    result = SecureDirectory(fd, path.empty() ? std::filesystem::path(".") : path.lexically_normal(),
                             static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino));
    return Status::OK();
}

int SecureDirectory::Fd() const
{
    return fd_;
}

std::string SecureDirectory::ProcPath(const std::string &leaf) const
{
    return "/proc/self/fd/" + std::to_string(fd_) + "/" + leaf;
}

Status SecureDirectory::VerifyPathIdentity() const
{
    SecureDirectory current;
    auto status = Open(path_, false, current);
    if (status.IsError()) {
        return status;
    }
    struct stat info {};
    if (fstat(current.fd_, &info) != 0) {
        return FsError("failed to verify secure directory");
    }
    return static_cast<uint64_t>(info.st_dev) == device_ && static_cast<uint64_t>(info.st_ino) == inode_
               ? Status::OK()
               : Status(StatusCode::SCHEDULE_CONFLICTED, "secure directory path changed during operation");
}

void SecureDirectory::Close()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool IsSafeLeafName(const std::string &name)
{
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos && name.find('\0') == std::string::npos;
}

}  // namespace functionsystem::snapshot_storage::detail
