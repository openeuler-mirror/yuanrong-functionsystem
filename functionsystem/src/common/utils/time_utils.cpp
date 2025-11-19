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

#include "time_utils.h"

namespace functionsystem {
std::tm ParseTimestamp(const std::string &timestamp)
{
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y%m%dT%H%M%SZ");
    if (ss.fail()) {
        return {};
    }
    return tm;
}

bool IsLaterThan(const std::string &timestamp1, const std::string &timestamp2, double seconds)
{
    std::tm tm1 = ParseTimestamp(timestamp1);
    std::tm tm2 = ParseTimestamp(timestamp2);
    std::time_t time1 = std::mktime(&tm1);
    std::time_t time2 = std::mktime(&tm2);
    return std::difftime(time1, time2) > seconds;
}
}  // namespace functionsystem