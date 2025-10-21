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

#include "utils/time_util.hpp"

#include <ctime>
#include <iomanip>

namespace litebus::time {
std::string GetCurrentUTCTime()
{
    std::stringstream ss;

    std::time_t now = std::time(nullptr);
    std::tm *utcTime = std::gmtime(&now);  // to utc

    ss << std::put_time(utcTime, "%Y%m%dT%H%M%SZ");  // format

    return ss.str();
}
}  // namespace litebus::time