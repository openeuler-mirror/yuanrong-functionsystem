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

#ifndef COMMON_UTILS_PARAM_CHECK_H
#define COMMON_UTILS_PARAM_CHECK_H

#include <regex>
#include <string>
#include <set>

namespace functionsystem {

const std::string NODE_ID_CHECK_PATTERN = "^[^/\\s]{1,128}$";  // non-empty and does not contain slash or space

const std::string ALIAS_CHECK_PATTERN = "^[^/\\s]{0,128}$";  // does not contain slash or space

const std::string IP_CHECK_PATTERN =
    "((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)";

const std::string ADDRESSES_CHECK_PATTERN =
    "^(((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?):([0-9]{1,5})(,|$))+$";

const std::string INNER_SERVICE_ADDRESS_SUFFIX = "svc.cluster.local";

const int MIN_PORT = 0;

const int MAX_PORT = 65535;

[[maybe_unused]] static bool IsNodeIDValid(const std::string &nodeID)
{
    std::regex pattern(NODE_ID_CHECK_PATTERN);
    std::smatch res;
    return std::regex_match(nodeID, res, pattern);
}

[[maybe_unused]] static bool IsAliasValid(const std::string &alias)
{
    std::regex pattern(ALIAS_CHECK_PATTERN);
    std::smatch res;
    return std::regex_match(alias, res, pattern);
}

[[maybe_unused]] static bool IsIPValid(const std::string &ip)
{
    std::regex pattern(IP_CHECK_PATTERN);
    std::smatch res;
    return std::regex_match(ip, res, pattern);
}

inline bool IsInnerServiceAddress(const std::string &ip)
{
    if (ip.length() < INNER_SERVICE_ADDRESS_SUFFIX.length()) {
        return false;
    }
    return ip.substr(ip.length() - INNER_SERVICE_ADDRESS_SUFFIX.length()) == INNER_SERVICE_ADDRESS_SUFFIX;
}

[[maybe_unused]] static bool IsAddressesValid(const std::string &address)
{
    std::regex pattern(ADDRESSES_CHECK_PATTERN);
    std::smatch res;
    return std::regex_match(address, res, pattern);
}

[[maybe_unused]] static bool IsPortValid(const std::string &portStr)
{
    if (portStr.empty()) {
        return false;
    }
    int port = 0;
    try {
        port = std::stoi(portStr);
    } catch (std::exception &e) {
        return false;
    }

    return port >= MIN_PORT && port <= MAX_PORT;
}

[[maybe_unused]] static bool IsAddressValid(const std::string &address)
{
    if (address.find_last_of(':') == std::string::npos) {
        return false;
    }

    if (!IsIPValid(address.substr(0, address.find_last_of(':')))) {
        return false;
    }

    if (!IsPortValid(address.substr(address.find_last_of(':') + 1))) {
        return false;
    }
    return true;
}

[[maybe_unused]] static std::function<bool(const std::string &, std::string &)> FlagCheckWrraper(
    std::function<bool(const std::string &)> check)
{
    return [check](const std::string &flagName, std::string &input) {
        auto valid = check(input);
        if (!valid) {
            std::cerr << "flag: " << flagName << " value: " << input << " is invalid" << std::endl;
        }
        return valid;
    };
}

[[maybe_unused]] static std::function<bool(const std::string &, std::string &)> WhiteListCheck(
    const std::set<std::string> whiteList)
{
    auto check = [whiteList](const std::string &item) { return (whiteList.find(item) != whiteList.end()); };
    return FlagCheckWrraper(check);
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_PARAM_CHECK_H
