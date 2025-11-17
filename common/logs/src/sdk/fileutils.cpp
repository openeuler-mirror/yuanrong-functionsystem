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

#include "fileutils.h"

#include <glob.h>
#include <sys/stat.h>
#include <zlib.h>

#include <climits>
#include <cstring>
#include <optional>
#include <sstream>

#include "logs/api/provider.h"
#include "logs/sdk/log_manager.h"

#define STREAM_RETRY_ON_EINTR(nread, stream, expr)                                                              \
    do {                                                                                                        \
        static_assert(std::is_unsigned<decltype(nread)>::value == true, #nread " must be an unsigned integer"); \
        (nread) = (expr);                                                                                       \
    } while ((nread) == 0 && ferror(stream) == EINTR)

namespace observability::sdk::logs {

const int TIME_SINCE_YEAR = 1900;
const int THOUSANDS_OF_MAGNITUDE = 1000;
const int MILLION_OF_MAGNITUDE = 1000000;
const mode_t LOG_FILE_PERMISSION = 0440;
const size_t BUFFER_SIZE = 32 * 1024uL;
constexpr int DIR_PATH_MAX = PATH_MAX;

inline std::string StrErr(int errNum)
{
    char errBuf[256];
    errBuf[0] = '\0';
    return strerror_r(errNum, errBuf, sizeof errBuf);
}

size_t FileSize(const std::string &filename)
{
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) {
        LOGS_CORE_ERROR("failed to stat file, {}", filename);
        return 0;
    }
    size_t size = static_cast<size_t>(st.st_size);
    return size;
}

bool FileExist(const std::string &filename, int mode)
{
    return access(filename.c_str(), mode) == 0;
}

void Glob(const std::string &pathPattern, std::vector<std::string> &paths)
{
    glob_t result;

    int ret = glob(pathPattern.c_str(), GLOB_TILDE | GLOB_ERR, nullptr, &result);
    switch (ret) {
        case 0:
            break;
        case GLOB_NOMATCH:
            globfree(&result);
            return;
        case GLOB_NOSPACE:
            globfree(&result);
            LOGS_CORE_WARN("failed to glob files, reason: out of memory.");
            return;
        default:
            globfree(&result);
            LOGS_CORE_WARN("failed to glob files, pattern: {}, errno: {}, errmsg: {}", pathPattern, ret, StrErr(ret));
            return;
    }

    for (size_t i = 0; i < result.gl_pathc; ++i) {
        (void)paths.emplace_back(result.gl_pathv[i]);
    }

    globfree(&result);
    return;
}

void Read(FILE *f, uint8_t *buf, size_t *pSize)
{
    size_t numReads = 0;
    size_t size = *pSize;
    // If fread_unlocked() return value is EINTR, the system call is interrupted.
    // Need to retry to read.
    STREAM_RETRY_ON_EINTR(numReads, f, fread_unlocked(buf, 1, size, f));
    if (numReads < size) {
        if (feof(f)) {
            *pSize = numReads;
        } else {
            LOGS_CORE_WARN("failed to reads, IOError occurred, errno: {}", errno);
        }
    }
    return;
}

int CompressFile(const std::string &src, const std::string &dest)
{
    FILE *file = fopen(src.c_str(), "r");
    if (file == nullptr) {
        LOGS_CORE_ERROR("failed to open file: {}", src);
        return -1;
    }
    gzFile gzf = gzopen(dest.c_str(), "w");
    if (gzf == nullptr) {
        LOGS_CORE_ERROR("failed to open gz file: {}", dest);
        (void)fclose(file);
        return -1;
    }

    size_t size = BUFFER_SIZE;
    uint8_t buf[size] = "\0";
    for (;;) {
        try {
            Read(file, buf, &size);
        } catch (const std::exception &readException) {
            (void)gzclose(gzf);
            (void)fclose(file);
            LOGS_CORE_ERROR("failed to compress file, err: {}", readException.what());
            return -1;
        }
        if (size == 0) {
            break;
        }
        int n = gzwrite(gzf, buf, static_cast<unsigned int>(size));
        if (n <= 0) {
            int err;
            const char *errStr = gzerror(gzf, &err);
            LOGS_CORE_ERROR("failed to write gz file, err: {}, errmsg:{}", err, errStr);
            (void)gzclose(gzf);
            (void)fclose(file);
            return err;
        }
    }
    (void)gzclose(gzf);
    (void)fclose(file);

    // Change mode to 0400, we only allow the read permission. And we
    // will never check the return even the chmod operation is failed.
    int rc = chmod(dest.c_str(), LOG_FILE_PERMISSION);
    if (rc != 0) {
        LOGS_CORE_WARN("failed to chmod file {}, err:{}", dest, StrErr(errno));
    }

    return 0;
}

void DeleteFile(const std::string &filename)
{
    if (unlink(filename.c_str()) != 0) {
        LOGS_CORE_WARN("failed to delete file {}", filename);
    }
    LOGS_CORE_DEBUG("delete file: {}", filename);
}

void GetFileModifiedTime(const std::string &filename, int64_t &timestamp)
{
    struct stat statBuf;
    if (stat(filename.c_str(), &statBuf) != 0) {
        LOGS_CORE_WARN("failed to access modify time from {}", filename);
        return;
    }

    auto secondBit = statBuf.st_mtim.tv_sec * MILLION_OF_MAGNITUDE;
    if (secondBit / MILLION_OF_MAGNITUDE != statBuf.st_mtim.tv_sec) {
        LOGS_CORE_WARN("invalid value tv_sec:{}, tv_nsec:{}", statBuf.st_mtim.tv_sec, statBuf.st_mtim.tv_nsec);
        return;
    }
    auto nsecBit = statBuf.st_mtim.tv_nsec / THOUSANDS_OF_MAGNITUDE;
    timestamp = INT64_MAX - secondBit < nsecBit ? 0 : secondBit + nsecBit;
}

bool RenameFile(const std::string &srcFile, const std::string &targetFile) noexcept
{
    (void)std::remove(targetFile.c_str());
    return std::rename(srcFile.c_str(), targetFile.c_str()) == 0;
}

std::optional<std::string> RealPath(const std::string &inputPath, const int reserveLen)
{
    char path[DIR_PATH_MAX] = { 0x00 };
    if (reserveLen < 0) {
        return std::nullopt;
    }
    if (inputPath.length() > DIR_PATH_MAX ||
        inputPath.length() + static_cast<size_t>(reserveLen) > DIR_PATH_MAX ||
        realpath(inputPath.c_str(), path) == nullptr) {
        return std::nullopt;
    }
    return std::string(path);
}

}  // namespace observability::sdk::logs
