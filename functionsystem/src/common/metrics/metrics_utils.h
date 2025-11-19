/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_METRICS_UTILS_H
#define FUNCTIONSYSTEM_METRICS_UTILS_H

#include <mutex>
#include <string>
#include <unordered_map>

#include "metrics_constants.h"
#include "common/logs/logging.h"

namespace functionsystem {
namespace metrics {

inline YRInstrument GetInstrumentEnum(const std::string &desc)
{
    if (INSTRUMENT_DESC_2_ENUM.find(desc) != INSTRUMENT_DESC_2_ENUM.end()) {
        return INSTRUMENT_DESC_2_ENUM.find(desc)->second;
    }
    YRLOG_WARN("{}|Enabled instrument input is not valid", desc);
    return YRInstrument::UNKNOWN_INSTRUMENT;
}

inline std::string GetInstrumentDesc(const YRInstrument &instrument)
{
    if (ENUM_2_INSTRUMENT_DESC.find(instrument) != ENUM_2_INSTRUMENT_DESC.end()) {
        return ENUM_2_INSTRUMENT_DESC.find(instrument)->second;
    }
    YRLOG_WARN("{}|Instrument does not have a description", static_cast<int>(instrument));
    return UNKNOWN_INSTRUMENT_NAME;
}

inline std::string GetSystemTimeStampNowStr()
{
    return std::to_string(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

inline long GetSystemTimeStampNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

inline int64_t GetCurrentTimeInMilliSec()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}
}
#endif  // FUNCTIONSYSTEM_METRICS_UTILS_H
