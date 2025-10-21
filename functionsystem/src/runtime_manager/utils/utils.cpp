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

#include <numeric>
#include <sstream>
#include <utils/string_utils.hpp>

namespace functionsystem::runtime_manager {
const std::string RUNTIME_UUID_PREFIX = "runtime-";
const std::string JOB_ID_STR = "job";
const int32_t JOB_INDEX = 1;

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
}  // namespace functionsystem::runtime_manager