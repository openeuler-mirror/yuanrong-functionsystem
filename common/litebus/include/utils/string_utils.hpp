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

#ifndef HASEN_STRING_UTILS_H
#define HASEN_STRING_UTILS_H

#include <string>
#include <vector>

#include "async/option.hpp"
#include "ssl/sensitive_value.hpp"

namespace litebus {
namespace strings {

const std::string STR_WHITESPACE = " \t\n\r";
const std::string STR_TRUE = std::string("true");
const std::string STR_FALSE = std::string("false");

// Flags indicating how 'remove' or 'trim' should operate.
enum Mode { PREFIX, SUFFIX, ANY };

std::vector<std::string> Tokenize(const std::string &s, const std::string &delims, const size_t maxTokens = 0);

std::vector<std::string> Split(const std::string &str, const std::string &pattern, size_t maxTokens = 0);

std::string &Trim(std::string &from, Mode mode = ANY, const std::string &chars = STR_WHITESPACE);

template <typename T>
Option<std::string> ToString(T t)
{
    std::ostringstream out;
    out << t;
    if (!out.good()) {
        return None();
    }

    return out.str();
}

inline Option<std::string> ToString(bool value)
{
    return value ? STR_TRUE : STR_FALSE;
}

// to judge whether a string is starting with  prefix
// for example: "hello world" is starting with "hello"
bool StartsWithPrefix(const std::string &source, const std::string &prefix);

std::string Remove(const std::string &from, const std::string &subStr, Mode mode = ANY);

}  // namespace strings

namespace hmac {
void SHA256AndHex(const std::string &input, std::stringstream &output, bool appendLineFeed = true);

std::string HMACAndSHA256(const SensitiveValue &secretKey, const std::string &data, bool binaryDigest = false);
}  // namespace hmac
}  // namespace litebus
#endif
