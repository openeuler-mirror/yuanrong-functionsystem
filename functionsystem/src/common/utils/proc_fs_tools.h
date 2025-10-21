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

#ifndef COMMON_UTILS_OS_UTILS_H
#define COMMON_UTILS_OS_UTILS_H

#include <wait.h>
#include <fcntl.h>
#include "utils/os_utils.hpp"

namespace functionsystem {

const int DEFAULT_READ_LENGTH = 10240;

class ProcFSTools {
public:
    virtual litebus::Option<std::string> Read(const std::string &path)
    {
        return ReadWithMostLength(path, DEFAULT_READ_LENGTH);
    }

    litebus::Option <std::string> ReadWithMostLength(const std::string &path, const int length) const
    {
        int fd = open(path.c_str(), O_RDONLY);
        auto res = litebus::os::ReadPipe(fd, length);
        (void)close(fd);
        return res;
    }
    virtual ~ProcFSTools() = default;
};

}

#endif // COMMON_UTILS_OS_UTILS_H
