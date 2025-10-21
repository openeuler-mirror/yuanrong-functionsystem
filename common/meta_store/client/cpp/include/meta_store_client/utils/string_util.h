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

#ifndef COMMON_METASTORE_STRING_UTIL_H
#define COMMON_METASTORE_STRING_UTIL_H

#include <string>

namespace functionsystem {

/**
 * @brief Adds the value at the end of the string by one.
 * @param[in] value String to be process.
 * @return String after processing.
 */
inline std::string StringPlusOne(const std::string &value)
{
    for (auto i = static_cast<int32_t>(value.size() - 1); i >= 0; --i) {
        auto index = static_cast<uint32_t>(i);
        if (static_cast<unsigned char>(value[index]) < 0xff) {
            std::string s = value.substr(0, index + 1);
            s[index] = static_cast<char>(s[index] + 1);
            return s;
        }
    }
    return {};
}
}

#endif // COMMON_METASTORE_STRING_UTIL_H
