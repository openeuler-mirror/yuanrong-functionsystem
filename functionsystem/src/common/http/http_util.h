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
#ifndef COMMON_HTTP_HTTP_UTIL_H
#define COMMON_HTTP_HTTP_UTIL_H

#include <map>
#include <memory>
#include <string>

namespace functionsystem {
const std::string METHOD_GET = "GET";

const std::string HEADER_CONNECTION = "connection";
const std::string HEADER_AUTHORIZATION = "authorization";

const std::string EMPTY_CONTENT_SHA256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

std::string EscapeQuery(const std::string &value);

std::string EscapeURL(const std::string &url, bool replacePath);

std::string GetCanonicalRequest(const std::string &method, const std::string &path,
                                const std::shared_ptr<std::map<std::string, std::string>> &queries,
                                const std::map<std::string, std::string> &headers, const std::string &sha256);

std::string GetSignedHeaders(const std::map<std::string, std::string> &headers);

std::string GetCanonicalHeaders(const std::map<std::string, std::string> &headers);

std::string GetCanonicalQueries(const std::shared_ptr<std::map<std::string, std::string>> &queries);

std::string GetCanonicalHeadersX(const std::map<std::string, std::string> &headers);
}  // namespace functionsystem
#endif  // COMMON_HTTP_HTTP_UTIL_H
