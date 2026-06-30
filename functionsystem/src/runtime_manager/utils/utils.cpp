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

#include "utils.h"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <sstream>

#include <nlohmann/json.hpp>
#include <utils/string_utils.hpp>

#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {
const std::string JOB_ID_STR = "job";
const int32_t JOB_INDEX = 1;
const int MAX_PORT_NUMBER = 65535;
const size_t MAX_NETWORK_JSON_LEN = 65536;
const size_t MAX_PORT_FORWARDINGS = 256;

// Normalize protocol to lowercase "tcp"/"udp"; returns empty string when invalid (caller skips).
std::string NormalizeProtocol(const nlohmann::json &item)
{
    if (!item.contains("protocol")) {
        return "tcp";
    }
    if (!item["protocol"].is_string()) {
        YRLOG_WARN("ParseForwardPorts: protocol is not string, skip");
        return "";
    }
    std::string proto = item["protocol"].get<std::string>();
    std::transform(proto.begin(), proto.end(), proto.begin(), [](unsigned char c) { return std::tolower(c); });
    if (proto != "tcp" && proto != "udp") {
        YRLOG_WARN("ParseForwardPorts: invalid protocol {}, skip", proto);
        return "";
    }
    return proto;
}

std::vector<PortForwardConfig> ParseForwardPorts(const std::string &networkJson)
{
    std::vector<PortForwardConfig> configs;
    if (networkJson.empty()) {
        return configs;
    }
    if (networkJson.length() > MAX_NETWORK_JSON_LEN) {
        YRLOG_WARN("ParseForwardPorts: network json too long ({}), skip", networkJson.length());
        return configs;
    }
    try {
        auto j = nlohmann::json::parse(networkJson);
        if (!j.contains("portForwardings") || !j["portForwardings"].is_array()) {
            return configs;
        }
        for (const auto &item : j["portForwardings"]) {
            if (configs.size() >= MAX_PORT_FORWARDINGS) {
                YRLOG_WARN("ParseForwardPorts: port forwardings exceed limit {}, rest ignored", MAX_PORT_FORWARDINGS);
                break;
            }
            if (!item.is_object() || !item.contains("port") || !item["port"].is_number_unsigned()) {
                YRLOG_WARN("ParseForwardPorts: invalid port forwarding entry, skip");
                continue;
            }
            int64_t p = item["port"].get<int64_t>();
            if (p <= 0 || p > static_cast<int64_t>(MAX_PORT_NUMBER)) {
                YRLOG_WARN("ParseForwardPorts: port {} out of range (1-{}), skip", p, MAX_PORT_NUMBER);
                continue;
            }
            std::string proto = NormalizeProtocol(item);
            if (proto.empty()) {
                continue;
            }
            configs.push_back(PortForwardConfig{ static_cast<uint32_t>(p), proto });
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseForwardPorts: failed to parse network json (len {}), error: {}", networkJson.length(),
                   e.what());
    }
    return configs;
}

std::string Utils::JoinToString(const std::vector<std::string> &strings, std::string delim)
{
    if (strings.empty()) {
        return "";
    }

    return std::accumulate(strings.begin() + 1, strings.end(), strings[0],
                           [&delim](const std::string &x, const std::string &y) { return x + delim + y; });
}

std::string Utils::TrimPrefix(const std::string &str, const std::string &prefix)
{
    if (str.empty() || prefix.empty() || prefix.length() > str.length()) {
        return "";
    }
    return str.substr(prefix.length());
}

std::string Utils::GetJobIDFromTraceID(const std::string &traceID)
{
    auto splits = litebus::strings::Split(traceID, "-");
    if (splits.size() <= 1 || splits[0] != JOB_ID_STR) {
        return JOB_ID_STR;
    }
    return splits[JOB_INDEX];
}

std::vector<std::string> Utils::SplitByFunc(std::string str, const std::function<bool(const char &)> &func)
{
    std::vector<std::string> res;
    uint32_t start = 0;
    uint32_t end = 0;
    while (end != str.length()) {
        if (func(str.at(end))) {
            if (start != end) {
                res.push_back(str.substr(start, end - start));
            }
            end++;
            start = end;
            continue;
        }
        end++;
    }

    if (start != end) {
        res.push_back(str.substr(start, end - start));
    }
    return res;
}

std::string Utils::LinkCommandWithLdLibraryPath(const std::string& ldLibraryPath, const std::string& originCmd)
{
    std::string resultCmd = originCmd;
    if (!ldLibraryPath.empty()) {
        resultCmd = "export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:" + ldLibraryPath + "; " + originCmd;
    }
    return resultCmd;
}

std::string ParseRootfsImageUrl(const std::string &rootfsJson)
{
    if (rootfsJson.empty()) {
        return "";
    }
    try {
        auto j = nlohmann::json::parse(rootfsJson);
        if (j.value("type", "") != "image") {
            return "";
        }
        return j.value("imageurl", "");
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseRootfsImageUrl: failed to parse rootfs json: {}", e.what());
        return "";
    }
}

std::string ParseRootfsWorkdir(const std::string &rootfsJson)
{
    if (rootfsJson.empty()) {
        return "";
    }
    try {
        auto j = nlohmann::json::parse(rootfsJson);
        return j.value("workdir", "");
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseRootfsWorkdir: failed to parse rootfs json: {}", e.what());
        return "";
    }
}

std::vector<RootfsMount> ParseRootfsMounts(const std::string &rootfsJson)
{
    std::vector<RootfsMount> mounts;
    if (rootfsJson.empty()) {
        return mounts;
    }
    try {
        auto j = nlohmann::json::parse(rootfsJson);
        if (!j.contains("mounts") || !j["mounts"].is_array()) {
            return mounts;
        }
        for (const auto &m : j["mounts"]) {
            RootfsMount rm;
            rm.source = m.value("source", "");
            rm.target = m.value("target", "");
            rm.readonly = m.value("readonly", false);
            if (rm.source.empty() || rm.target.empty()) {
                YRLOG_WARN("ParseRootfsMounts: skip mount with empty source/target");
                continue;
            }
            mounts.push_back(rm);
        }
    } catch (const std::exception &e) {
        YRLOG_WARN("ParseRootfsMounts: failed to parse rootfs json: {}", e.what());
    }
    return mounts;
}
}  // namespace functionsystem::runtime_manager
