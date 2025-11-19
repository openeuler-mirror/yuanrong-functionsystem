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

#ifndef COMMON_UTILS_OPERATION_H
#define COMMON_UTILS_OPERATION_H

#include <string>
#include <vector>

#include "common/logs/logging.h"

namespace functionsystem {
const int LINE_LENGTH = 10240;

class CmdTool {
public:
    virtual ~CmdTool() = default;
    virtual std::vector<std::string> GetCmdResult(const std::string &cmd)
    {
        auto buffer = std::make_unique<char[]>(LINE_LENGTH);
        std::vector<std::string> cmdResult;
        FILE *pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            return cmdResult;
        }
        while (fgets(buffer.get(), LINE_LENGTH, pipe)) {
            (void)cmdResult.emplace_back(buffer.get());
        }
        (void)pclose(pipe);
        return cmdResult;
    }

    virtual std::vector<std::string> GetCmdResultWithError(const std::string &cmd)
    {
        std::string escapedCmd;
        for (char c : cmd) {
            if (c == '\'') {
                escapedCmd += "'\\''";
            } else {
                escapedCmd += c;
            }
        }

        std::string full_cmd = R"(sh -c ')" + escapedCmd + R"( 2>&1')";
        return GetCmdResult(full_cmd);
    }
};

}

#endif  // COMMON_UTILS_OPERATION_H
