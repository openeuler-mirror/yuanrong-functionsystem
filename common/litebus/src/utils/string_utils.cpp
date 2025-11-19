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

#include "utils/string_utils.hpp"

#include <iomanip>

#include "openssl/hmac.h"
#include "openssl/sha.h"

namespace litebus {
namespace strings {

std::vector<std::string> Tokenize(const std::string &s, const std::string &delims, const size_t maxTokens)
{
    std::vector<std::string> strVec;
    size_t loopIndex = 0;

    for (;;) {
        size_t nonDelim = s.find_first_not_of(delims, loopIndex);
        if (nonDelim == std::string::npos) {
            break;  // No valid content left
        }

        size_t delim = s.find_first_of(delims, nonDelim);
        // This is the last token, or enough tokens found.
        if (delim == std::string::npos || (maxTokens > 0 && strVec.size() == maxTokens - 1)) {
            strVec.push_back(s.substr(nonDelim));
            break;
        }

        strVec.push_back(s.substr(nonDelim, delim - nonDelim));
        loopIndex = delim;
    }

    return strVec;
}

std::vector<std::string> Split(const std::string &str, const std::string &pattern, const size_t maxTokens)
{
    std::vector<std::string> result;
    if (pattern.empty()) {
        return result;
    }

    std::string::size_type pos;
    std::string tmpStr(str + pattern);
    std::string::size_type size = tmpStr.size();
    size_t vecSize = 0;
    // if max vector size ==1, then return source
    if (maxTokens == 1) {
        result.push_back(str);
        return result;
    }
    std::string::size_type index = 0;
    while (index < size) {
        pos = tmpStr.find(pattern, index);
        if (pos < size) {
            std::string s = tmpStr.substr(index, pos - index);
            result.push_back(s);
            index = pos + pattern.size() - 1;
            vecSize++;
            // if has maxsize and reach maxsize, then push the rest and break;
            if (maxTokens > 0 && vecSize >= maxTokens - 1 && pos < str.length()) {
                result.push_back(str.substr(index + 1));
                break;
            }
        }
        index++;
    }
    return result;
}

std::string &Trim(std::string &s, Mode mode, const std::string &chars)
{
    size_t start = 0;
    // set start pos
    if (mode == ANY || mode == PREFIX) {
        start = s.find_first_not_of(chars);
    }
    // if s contains only chars in chars
    if (start == std::string::npos) {
        s.clear();
        return s;
    }
    size_t length = std::string::npos;
    // set end pos
    if (mode == ANY || mode == SUFFIX) {
        length = s.find_last_not_of(chars) + 1 - start;
    }
    s = s.substr(start, length);
    return s;
}

bool StartsWithPrefix(const std::string &source, const std::string &prefix)
{
    if (source.length() < prefix.length()) {
        return false;
    }

    return (source.compare(0, prefix.length(), prefix) == 0);
}

std::string Remove(const std::string &from, const std::string &subStr, Mode mode)
{
    std::string result = from;

    if (mode == PREFIX) {
        if (from.find(subStr) == 0) {
            result = from.substr(subStr.size());
        }
    } else if (mode == SUFFIX) {
        if (from.rfind(subStr) == from.size() - subStr.size()) {
            result = from.substr(0, from.size() - subStr.size());
        }
    } else {
        size_t index;
        while ((index = result.find(subStr)) != std::string::npos) {
            result = result.erase(index, subStr.size());
        }
    }

    return result;
}

}  // namespace strings

namespace hmac {
const static unsigned int CHAR_TO_HEX = 2;

const static int32_t FIRST_FOUR_BIT_MOVE = 4;

const static std::string HEX_STRING_SET = "0123456789abcdef";  // NOLINT

void SHA256AndHex(const std::string &input, std::stringstream &output, bool appendLineFeed)
{
    unsigned char sha256Chars[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(input.c_str()), input.size(), sha256Chars);
    for (const auto &c : sha256Chars) {
        output << HEX_STRING_SET[c >> FIRST_FOUR_BIT_MOVE] << HEX_STRING_SET[c & 0xf];
    }
    if (appendLineFeed) {
        output << "\n";
    }
}

std::string HMACAndSHA256(const SensitiveValue &secretKey, const std::string &data, bool binaryDigest)
{
    HMAC_CTX *ctx = HMAC_CTX_new();

    int ret = HMAC_Init_ex(ctx, secretKey.GetData(), static_cast<int>(secretKey.GetSize()), EVP_sha256(), nullptr);
    if (ret != 1) {
        BUSLOG_ERROR("hmac init ex error: {}", ret);
        HMAC_CTX_free(ctx);
        return "";
    }
    ret = HMAC_Update(ctx, reinterpret_cast<const unsigned char *>(&data[0]), data.length());
    if (ret != 1) {
        BUSLOG_ERROR("hmac update error: {}", ret);
        HMAC_CTX_free(ctx);
        return "";
    }

    unsigned int mdLength = EVP_MAX_MD_SIZE;
    unsigned char md[EVP_MAX_MD_SIZE];
    ret = HMAC_Final(ctx, md, &mdLength);
    if (ret != 1) {
        BUSLOG_ERROR("hmac final error: {}", ret);
        HMAC_CTX_free(ctx);
        return "";
    }

    HMAC_CTX_free(ctx);

    std::stringstream ss;
    if (binaryDigest) {
        for (unsigned int i = 0; i < mdLength; i++) {
            ss << md[i];
        }
        return ss.str();
    }

    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < mdLength; i++) {
        ss << std::setw(CHAR_TO_HEX) << static_cast<unsigned int>(md[i]);
    }

    return ss.str();
}
}  // namespace hmac
}  // namespace litebus
