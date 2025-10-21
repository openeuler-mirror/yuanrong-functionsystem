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

#ifndef COMMON_UTILS_HEX_H
#define COMMON_UTILS_HEX_H

#include <sstream>
#include <string>

namespace functionsystem {

const static unsigned int CHAR_TO_HEX = 2;

const static int32_t FIRST_FOUR_BIT_MOVE = 4;

const static std::string HEX_STRING_SET = "0123456789abcdef";  // NOLINT

const static std::string HEX_STRING_SET_CAP = "0123456789ABCDEF";  // NOLINT

inline void ToLower(std::string &source)
{
    (void)std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) { return std::tolower(c); });
}
}  // namespace functionsystem

#endif  // COMMON_UTILS_HEX_H
