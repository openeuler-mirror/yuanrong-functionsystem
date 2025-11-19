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

#ifndef COMMON_UTILS_RANDOM_NUMBER_H
#define COMMON_UTILS_RANDOM_NUMBER_H

#include <random>
#include "utils/string_utils.hpp"

namespace functionsystem {

template <class T>
T GenerateRandomNumber(T lower, T upper)
{
    std::random_device rd;
    std::mt19937 mt(rd());
    std::uniform_int_distribution<T> dis(lower, upper);
    return static_cast<T>(dis(mt));
}

/**
 * @brief Shuffle string with delimiter.
 * @param[in] str The string to shuffle.
 * @param[in] pattern Regular expression of split.
 * @return Shuffled string.
 */
inline std::string ShuffleStringByDelimiter(const std::string &str, const std::string &pattern)
{
    auto vec = litebus::strings::Split(str, pattern);
    std::shuffle(vec.begin(), vec.end(), std::mt19937{ std::random_device{}() });
    std::string strShuffled;
    for (const auto &tmpStr : vec) {
        strShuffled += tmpStr + pattern;
    }
    if (!strShuffled.empty()) {
        // pop last delimiter
        strShuffled.pop_back();
    }
    return strShuffled;
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_RANDOM_NUMBER_H
