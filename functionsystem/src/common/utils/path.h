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

#ifndef COMMON_UTILS_PATH_H
#define COMMON_UTILS_PATH_H

#include <string>
#include <utils/string_utils.hpp>
#include <sys/stat.h>
#include <utils/os_utils.hpp>

namespace functionsystem {

// LookPath searches for an executable named file in the
// directories named by the PATH environment variable.
[[maybe_unused]] static litebus::Option<std::string> LookPath(const std::string &file)
{
    if (file.find('/') != std::string::npos) {
        struct stat attr{};
        if (stat(file.c_str(), &attr) == 0 && !(attr.st_mode & S_IFDIR) &&
            (attr.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            return {file};
        }
        return {};
    }

    litebus::Option<std::string> path = litebus::os::GetEnv("PATH");
    if (path.IsNone()) {
        return {};
    }
    auto paths = litebus::strings::Split(path.Get(), ":");
    struct stat attr{};
    for (std::string &dir: paths) {
        if (dir.empty()) {
            dir = ".";
        }
        dir = litebus::os::Join(dir, file, '/');
        if (stat(dir.c_str(), &attr) == 0 && !(attr.st_mode & S_IFDIR) &&
            (attr.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            return {dir};
        }
    }

    return {};
}

[[maybe_unused]] static bool EndsWithSuffix(const std::string &path, const std::string &suffix)
{
    if (path.length() < suffix.length()) {
        return false;
    }

    for (unsigned long i = 1; i <= suffix.length(); i++) {
        if (path[path.length() - i] != suffix[suffix.length() - i]) {
            return false;
        }
    }

    return true;
}

}


#endif // COMMON_UTILS_PATH_H
