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

#include "http_util.h"

#include <map>
#include <sstream>

#include "common/hex/hex.h"
#include "utils/string_utils.hpp"

namespace functionsystem {
bool ShouldQueryEscape(char c)
{
    if (std::isalnum(c)) {
        // A~Z, a~z, 0~9 not escape
        return false;
    }

    if (c == '-' || c == '_' || c == '.' || c == '~') {
        // -, _, ., ~ not escape
        return false;
    }

    // encode according to RFC 3986.
    return true;
}

std::string EscapeQuery(const std::string &s)
{
    std::stringstream ss;

    for (const auto &c : s) {
        if (!ShouldQueryEscape(c)) {
            ss << c;
        } else if (' ' == c) {
            ss << '+';
        } else {
            // encode according to RFC 3986.
            ss << '%' << HEX_STRING_SET_CAP[c >> FIRST_FOUR_BIT_MOVE] << HEX_STRING_SET_CAP[c & 0xf];
        }
    }

    return ss.str();
}

void DoReplace(std::string *source, const std::string &replace, const std::string &replacement)
{
    size_t pos = 0;
    while ((pos = source->find(replace, pos)) != std::string::npos) {
        source->replace(pos, replace.length(), replacement);
        pos += replacement.length();
    }
}


std::string EscapeURL(const std::string &url, bool replacePath)
{
    if (url.empty()) {
        return "";
    }

    std::string encodeurl = EscapeQuery(url);

    DoReplace(&encodeurl, "+", "%20");
    DoReplace(&encodeurl, "*", "%2A");

    DoReplace(&encodeurl, "%7E", "~");
    if (replacePath) {
        DoReplace(&encodeurl, "%2F", "/");
    }

    return encodeurl;
}

std::string GetCanonicalHeaders(const std::map<std::string, std::string> &headers)
{
    std::stringstream ss;
    for (const auto &header : headers) {
        std::string lowerKey = header.first;
        ToLower(lowerKey);

        if (lowerKey == HEADER_CONNECTION || lowerKey == HEADER_AUTHORIZATION) {
            continue;
        }

        std::string value = header.second;
        ss << lowerKey << ':' << litebus::strings::Trim(value) << "\n";
    }
    return ss.str();
}

std::string GetSignedHeaders(const std::map<std::string, std::string> &headers)
{
    bool first = true;
    std::stringstream ss;
    for (const auto &header : headers) {
        std::string lowerKey = header.first;
        ToLower(lowerKey);

        if (lowerKey == HEADER_CONNECTION || lowerKey == HEADER_AUTHORIZATION) {
            continue;
        }

        if (first) {
            first = false;
            ss << lowerKey;
        } else {
            ss << ";" << lowerKey;
        }
    }
    return ss.str();
}

std::string GetCanonicalQueries(const std::shared_ptr<std::map<std::string, std::string>> &queries)
{
    if (queries == nullptr) {
        return "";
    }

    bool first = true;
    std::stringstream ss;
    for (const auto &query : *queries) {
        if (first) {
            first = false;
            ss << EscapeURL(query.first, false) << '=' << EscapeURL(query.second, false);
        } else {
            ss << "&" << EscapeURL(query.first, false) << '=' << EscapeURL(query.second, false);
        }
    }
    return ss.str();
}

std::string GetCanonicalRequest(const std::string &method, const std::string &path,
                                const std::shared_ptr<std::map<std::string, std::string>> &queries,
                                const std::map<std::string, std::string> &headers, const std::string &sha256)
{
    std::string canonicalPath = path.empty() ? "/" : EscapeURL(path, true);
    std::string canonicalQueries = GetCanonicalQueries(queries);
    std::string canonicalHeaders = GetCanonicalHeaders(headers);
    std::string signedHeaders = GetSignedHeaders(headers);

    std::stringstream ss;
    ss << method << "\n"
       << canonicalPath << "\n"
       << canonicalQueries << "\n"
       << canonicalHeaders << "\n"
       << signedHeaders << "\n";
    if (sha256.empty()) {
        // default empty canonical sha256
        ss << EMPTY_CONTENT_SHA256;
    } else {
        ss << sha256;
    }
    return ss.str();
}

std::string GetCanonicalHeadersX(const std::map<std::string, std::string> &headers)
{
    std::stringstream ss;
    bool first = true;
    for (const auto &header : headers) {
        std::string lowerKey = header.first;
        ToLower(lowerKey);
        if (lowerKey == HEADER_CONNECTION || lowerKey == HEADER_AUTHORIZATION) {
            continue;
        }
        std::string value = header.second;
        if (first) {
            first = false;
            ss << lowerKey << "=" << litebus::strings::Trim(value);
        } else {
            ss << "&" << lowerKey << "=" << litebus::strings::Trim(value);
        }
    }
    return ss.str();
}
}  // namespace functionsystem
