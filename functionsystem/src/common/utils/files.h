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

#ifndef COMMON_UTILS_FILES_H
#define COMMON_UTILS_FILES_H
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <utils/os_utils.hpp>

#include "common/logs/logging.h"
#include "common/status/status.h"


namespace functionsystem {

const std::set<uint32_t> WRITEABLE_PERMISSIONS = { 2, 3, 6, 7 };  // 2: -w-, 3: -wx, 6: rw-, 7: rwx

struct Permissions {
    uint32_t owner;
    uint32_t group;
    uint32_t others;
};

static bool FileExists(const std::string &path)
{
    struct stat s;

    if (lstat(path.c_str(), &s) < 0) {
        return false;
    }
    return true;
}

static int Close(int fd)
{
    if (fd >= 0) {
        return close(fd);
    }
    return 0;
}

[[maybe_unused]] static int TouchFile(const std::string &path)
{
    if (!FileExists(path)) {
        int fd = open(path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd >= 0) {
            return Close(fd);
        } else {
            return fd;
        }
    }
    return 0;
}

[[maybe_unused]] static std::string Read(const std::string &path)
{
    char realPath[PATH_MAX] = { 0 };
    if (realpath(path.c_str(), realPath) == nullptr) {
        YRLOG_WARN("failed to read, {} isn't a real path, errno: {}, {}", path, errno, litebus::os::Strerror(errno));
        return "";
    }

    YRLOG_DEBUG("read file, path: {}", realPath);
    std::ifstream in(realPath);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string contents(buffer.str());
    return contents;
}

[[maybe_unused]] static bool Write(const std::string &path, const std::string &content)
{
    YRLOG_DEBUG("write file, path: {}", path);
    std::ofstream out(path);
    if (out.is_open()) {
        out << content;
        out.close();
        return true;
    }
    return false;
}

[[maybe_unused]] static Status UnzipFile(const std::string &destDir, const std::string &destFile)
{
    std::string cmd = litebus::os::Join("unzip -d", destDir, ' ');
    cmd = litebus::os::Join(cmd, destFile, ' ');
    if (system(cmd.c_str()) > 0) {
        YRLOG_ERROR("failed to execute unzip cmd({}).", cmd);
        return Status(StatusCode::FUNC_AGENT_OBS_OPEN_FILE_ERROR, "failed to unzip file");
    }

    return Status::OK();
}

[[maybe_unused]] static bool CheckPathType(const std::string &path, uint32_t type)
{
    struct stat s;
    if (lstat(path.c_str(), &s) == 0) {
        return static_cast<bool>(s.st_mode & type);
    }

    return false;
}

[[maybe_unused]] static bool IsFile(const std::string &path)
{
    return CheckPathType(path, S_IFREG);
}

[[maybe_unused]] static bool IsDir(const std::string &path)
{
    return CheckPathType(path, S_IFDIR);
}

[[maybe_unused]] static litebus::Option<struct stat> GetFileInfo(const std::string &path)
{
    YRLOG_DEBUG("read file info, path: {}", path);
    struct stat fileStat {};
    if (stat(path.c_str(), &fileStat) != 0) {
        return {};
    };
    return fileStat;
}

[[maybe_unused]] static bool IsDirEmpty(const char *path)
{
    if (path == nullptr) {
        YRLOG_ERROR("path is nullptr");
        return true;
    }
    DIR *dir = opendir(path);
    if (dir == nullptr) {
        return true;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] != '.') {
            (void)closedir(dir);
            return false;
        }
    }
    (void)closedir(dir);
    return true;
}

static litebus::Option<Permissions> GetPermission(const std::string &path)
{
    auto stat = GetFileInfo(path);
    if (stat.IsNone()) {
        YRLOG_ERROR("failed to get file({}) permissions, unable to stat", path);
        return litebus::None();
    }

    // get file permission
    mode_t permissions = stat.Get().st_mode & 0777;
    Permissions result{ .owner = (permissions >> 6) & 7, .group = (permissions >> 3) & 7, .others = permissions & 7 };
    return result;
}

static litebus::Option<std::pair<uint32_t, uint32_t>> GetOwner(const std::string &path)
{
    auto stat = GetFileInfo(path);
    if (stat.IsNone()) {
        YRLOG_ERROR("failed to get file({}) owner, unable to stat", path);
        return litebus::None();
    }
    std::pair<uint32_t, uint32_t> owner = { stat.Get().st_uid, stat.Get().st_gid };
    return owner;
}

static bool IsWriteable(const Permissions &permissions, const std::pair<uint32_t, uint32_t> &owner, uint32_t uid,
                        uint32_t gid)
{
    // is owner
    if (owner.first == uid) {
        return WRITEABLE_PERMISSIONS.find(permissions.owner) != WRITEABLE_PERMISSIONS.end();
    }

    // is in group
    if (owner.second == gid) {
        return WRITEABLE_PERMISSIONS.find(permissions.group) != WRITEABLE_PERMISSIONS.end();
    }

    return WRITEABLE_PERMISSIONS.find(permissions.others) != WRITEABLE_PERMISSIONS.end();
}

[[maybe_unused]] static bool IsPathWriteable(const std::string &path, uint32_t uid, uint32_t gid)
{
    if (!litebus::os::ExistPath(path)) {
        YRLOG_WARN("path({}) doesn't exist, is not writeable", path);
        return false;
    }

    auto owner = GetOwner(path);
    if (owner.IsNone()) {
        YRLOG_ERROR("failed to get ({}) owner, is not writeable", path);
        return false;
    }

    auto permission = GetPermission(path);
    if (permission.IsNone()) {
        YRLOG_ERROR("failed to get ({}) permission, is not writeable", path);
        return false;
    }
    return IsWriteable(permission.Get(), owner.Get(), uid, gid);
}
}  // namespace functionsystem
#endif  // COMMON_UTILS_FILES_H
