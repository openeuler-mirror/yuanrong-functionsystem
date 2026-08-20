/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SECURE_DIRECTORY_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SECURE_DIRECTORY_H

#include <filesystem>
#include <string>

#include "common/status/status.h"

namespace functionsystem::snapshot_storage::detail {

class SecureDirectory {
public:
    SecureDirectory() = default;
    ~SecureDirectory();
    SecureDirectory(const SecureDirectory &) = delete;
    SecureDirectory &operator=(const SecureDirectory &) = delete;
    SecureDirectory(SecureDirectory &&other) noexcept;
    SecureDirectory &operator=(SecureDirectory &&other) noexcept;

    static Status Open(const std::filesystem::path &path, bool create, SecureDirectory &result);

    int Fd() const;
    std::string ProcPath(const std::string &leaf) const;
    Status VerifyPathIdentity() const;

private:
    SecureDirectory(int fd, std::filesystem::path path, uint64_t device, uint64_t inode);
    void Close();

    int fd_{ -1 };
    std::filesystem::path path_;
    uint64_t device_{ 0 };
    uint64_t inode_{ 0 };
};

bool IsSafeLeafName(const std::string &name);

}  // namespace functionsystem::snapshot_storage::detail

#endif  // FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SECURE_DIRECTORY_H
