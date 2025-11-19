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

#ifndef COMMON_UTILS_PROC_UTIL_H
#define COMMON_UTILS_PROC_UTIL_H

#include <string>
#include <unordered_set>
#include <unordered_map>

#include "common/utils/exec_utils.h"
#include "common/logs/logging.h"

namespace functionsystem {
[[maybe_unused]] static std::unordered_map<pid_t, std::string> QueryProcStatus(std::unordered_set<pid_t> pids)
{
    std::unordered_map<pid_t, std::string> statusMap;
    if (pids.empty()) {
        return statusMap;
    }
    // Construct command
    std::ostringstream pidStream;
    bool isFirst = true;
    for (const auto &pid : pids) {
        if (!isFirst) {
            pidStream << ",";
        }
        pidStream << pid;
        isFirst = false;
    }
    const std::string pidList = pidStream.str();
    const std::string cmd = "ps -p " + pidList + " -o pid,stat ";
    YRLOG_DEBUG("cmd: {}", cmd);
    const std::string output = ExecuteCommandByPopen(cmd, CMD_OUTPUT_MAX_LEN, false);
    // Parse
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        line.erase(line.find_last_not_of(" \t\n\r") + 1);  // Trim trailing whitespace
        if (line.empty() || line.find("PID") != std::string::npos) {
            continue;
        }
        pid_t pid;
        std::string stat;
        std::istringstream lineStream(line);
        if (lineStream >> pid >> stat) {
            statusMap[pid] = stat;
        }
    }
    // Fill missing PIDs
    for (const auto &pid : pids) {
        if (statusMap.find(pid) == statusMap.end()) {
            statusMap[pid] = "";  // Mark as non-existent
        }
    }
    return statusMap;
}
}  // namespace functionsystem
#endif // COMMON_UTILS_PROC_UTIL_H
