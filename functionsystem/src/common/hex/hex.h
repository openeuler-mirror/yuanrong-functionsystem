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

#include "common/status/status.h"
#include "securec.h"

namespace functionsystem {

const static unsigned int CHAR_TO_HEX = 2;

const static int32_t FIRST_FOUR_BIT_MOVE = 4;

const static std::string HEX_STRING_SET = "0123456789abcdef";  // NOLINT

const static std::string HEX_STRING_SET_CAP = "0123456789ABCDEF";  // NOLINT

std::string FromHexString(const std::string &hex);

std::string HexStringToCharString(const std::string &hexString);

Status HexStringToCharString(const char *hexString, char *charString, unsigned int charStringLen);

Status HexStringToCharStringCap(const unsigned char *hexString, uint32_t hexStringLen, char *charString,
                                uint32_t charStringLen);

Status HexStringToCharStringCap(const char *hexStr, uint32_t hexStrLen, char *charStr, uint32_t charStrLen);

inline std::string CharStringToHexString(const std::string &str, const std::string &separator = "")
{
    std::stringstream ss;

    for (std::string::size_type i = 0; i < str.size(); ++i) {
        ss << HEX_STRING_SET[static_cast<unsigned char>(str[i]) >> FIRST_FOUR_BIT_MOVE]
           << HEX_STRING_SET[static_cast<unsigned char>(str[i]) & 0xf] << separator;
    }

    return ss.str();
}

inline std::string CharToHexString(const unsigned char *md, const int size, const std::string &separator = "")
{
    std::stringstream ss;

    for (int i = 0; i < size; ++i) {
        ss << HEX_STRING_SET[md[i] >> FIRST_FOUR_BIT_MOVE]
           << HEX_STRING_SET[md[i] & 0xf] << separator;
    }
    return ss.str();
}

inline std::string CharStringToHexStringCap(const std::string &str, const std::string &separator = "")
{
    std::stringstream ss;

    for (std::string::size_type i = 0; i < str.size(); ++i) {
        ss << HEX_STRING_SET_CAP[static_cast<unsigned char>(str[i]) >> FIRST_FOUR_BIT_MOVE]
           << HEX_STRING_SET_CAP[static_cast<unsigned char>(str[i]) & 0xf] << separator;
    }

    return ss.str();
}

inline Status CharStringToHexStringCap(unsigned char *hexString, const unsigned char *charString,
                                       unsigned int hexStringLen, unsigned int charStringLen,
                                       const std::string &separator = "")
{
    if (hexString == nullptr || charString == nullptr) {
        return Status(StatusCode::FAILED, "pointer is nullptr");
    }

    std::stringstream ss;

    for (uint32_t i = 0; i < charStringLen; ++i) {
        ss << HEX_STRING_SET_CAP[charString[i] >> FIRST_FOUR_BIT_MOVE] << HEX_STRING_SET_CAP[charString[i] & 0xf]
           << separator;
    }

    int ret = memcpy_s(hexString, hexStringLen, ss.str().c_str(), hexStringLen);
    if (ret != 0) {
        return Status(StatusCode::FAILED, "failed to copy char" + std::to_string(ret));
    }

    return Status::OK();
}

inline bool IsHexString(const char *str, unsigned int charStringLength)
{
    for (uint32_t i = 0; i < charStringLength; ++i) {
        if (str[i] < '0' || (str[i] > '9' && str[i] < 'a') || str[i] > 'f') {
            return false;
        }
    }
    return true;
}

inline bool IsHexStringCaseInsensitive(const char *str, unsigned int charStringLength)
{
    for (uint32_t i = 0; i < charStringLength; ++i) {
        if (str[i] < '0' || (str[i] > '9' && str[i] < 'A') || (str[i] > 'Z' && str[i] < 'a') || str[i] > 'f') {
            return false;
        }
    }
    return true;
}

inline void ToLower(std::string &source)
{
    (void)std::transform(source.begin(), source.end(), source.begin(), [](unsigned char c) { return std::tolower(c); });
}

std::vector<unsigned char> HexToBytes(const std::string &hex);
std::string BytesToBase64(const std::vector<unsigned char> &data);
std::string Base64UrlEncodeByte(const std::vector<unsigned char> &data);
std::string Base64UrlEncodeString(const std::string &data);
std::string Base64Decode(const std::string &input);
std::string Base64Encode(const std::string &input);

}  // namespace functionsystem

#endif  // COMMON_UTILS_HEX_H
