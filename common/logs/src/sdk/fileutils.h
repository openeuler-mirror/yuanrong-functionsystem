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

#ifndef OBSERVABILITY_SDK_LOGS_FILEUTILS_H
#define OBSERVABILITY_SDK_LOGS_FILEUTILS_H

#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace observability::sdk::logs {

struct FileUnit {
    FileUnit(std::string name, size_t size) : name(std::move(name)), size(size)
    {
    }
    ~FileUnit() = default;

    // filename.
    std::string name;

    // file size.
    size_t size;
};

size_t FileSize(const std::string &filename);
bool FileExist(const std::string &filename, int mode = F_OK);
void Glob(const std::string &pathPattern, std::vector<std::string> &paths);
void Read(FILE *f, uint8_t *buf, size_t *pSize);
int CompressFile(const std::string &src, const std::string &dest);
void DeleteFile(const std::string &filename);
void GetFileModifiedTime(const std::string &filename, int64_t &timestamp);
bool RenameFile(const std::string &srcFile, const std::string &targetFile) noexcept;
std::optional<std::string> RealPath(const std::string &inputPath, const int reserveLen = 0);

}  // namespace observability::sdk::logs

#endif  // LOGS_FILEUTILS_H
