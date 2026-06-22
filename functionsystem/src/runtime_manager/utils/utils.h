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

#ifndef RUNTIME_MANAGER_UTILS_UTILS_H
#define RUNTIME_MANAGER_UTILS_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

#include <async/uuid_generator.hpp>
#include <functional>

namespace functionsystem::runtime_manager {

// Port forward configuration parsed from network JSON.
struct PortForwardConfig {
    uint32_t containerPort;  // Container port to forward
    std::string protocol;    // "tcp" or "udp" (lowercase)
};

// Parse the list of port forward configs from a network JSON string.
// Expected format: {"portForwardings": [{"port": 8080, "protocol": "tcp"}, ...]}
std::vector<PortForwardConfig> ParseForwardPorts(const std::string &networkJson);

// Extract the image URL from a rootfs JSON of type "image".
// Expected format: {"type": "image", "imageurl": "repo/image:tag", ...}
// Returns empty string if the JSON is not type=image or has no imageurl.
std::string ParseRootfsImageUrl(const std::string &rootfsJson);

// Extract the working directory from a rootfs JSON.
// Expected format: {"type": "image", "imageurl": "...", "workdir": "/data", ...}
// Returns empty string if not set.
std::string ParseRootfsWorkdir(const std::string &rootfsJson);

// Host directory mount parsed from a rootfs JSON "mounts" entry.
struct RootfsMount {
    std::string source;    // host path
    std::string target;    // container path
    bool readonly = false; // mount read-only
};

// Parse the mounts list from a rootfs JSON.
// Expected format: {"mounts": [{"source": "/data/host", "target": "/data", "readonly": false}, ...]}
// Skips entries with empty source/target. Returns empty vector on parse failure.
std::vector<RootfsMount> ParseRootfsMounts(const std::string &rootfsJson);

class Utils {
public:
    static std::string JoinToString(std::vector<std::string> const &strings, std::string delim);

    static std::string TrimPrefix(const std::string &str, const std::string &prefix);

    static std::string GetJobIDFromTraceID(const std::string &traceID);

    static std::vector<std::string> SplitByFunc(std::string str, const std::function<bool(const char &)> &func);

    static std::string LinkCommandWithLdLibraryPath(const std::string& ldLibraryPath, const std::string& originCmd);
};
}  // namespace functionsystem::runtime_manager

#endif // RUNTIME_MANAGER_UTILS_UTILS_H
