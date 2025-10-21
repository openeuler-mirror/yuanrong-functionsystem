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

#ifndef LITEBUS_HTTP_DECODER
#define LITEBUS_HTTP_DECODER

#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "httpd/http.hpp"
#include "httpd/http_parser.hpp"

namespace litebus {

namespace http_parsing {

constexpr int SUCCESS = 0;
constexpr int FAILURE = 1;

}    // namespace http_parsing

class ResponseDecoder : public http::HttpParser {
public:
    ResponseDecoder();
    ResponseDecoder(const ResponseDecoder &) = delete;
    ResponseDecoder &operator=(const ResponseDecoder &) = delete;

    ~ResponseDecoder() override;

    std::deque<http::Response *> Decode(const char *data, size_t length, bool flgEOF = false);

    void RegisterResponseCallBack(const http::ResponseCallback &f);

protected:
    void HandleMessageBegin() override;

    void HandleUrl(const char *, size_t) override;

    void HandleHeaderField(const char *data, size_t length) override;

    void HandleHeaderValue(const char *data, size_t length) override;

    int HandleHeadersComplete() override;

    void HandleBody(const char *data, size_t length) override;

    int HandleMessageComplete() override;

private:
    http::Response *response;

    std::deque<http::Response *> responses;
    http::ResponseCallback responseCallback;
};

class RequestDecoder : public http::HttpParser {
public:
    explicit RequestDecoder();
    RequestDecoder(const RequestDecoder &) = delete;
    RequestDecoder &operator=(const RequestDecoder &) = delete;
    ~RequestDecoder() override;

    std::deque<http::Request *> Decode(const char *data, size_t length);

protected:
    void HandleMessageBegin() override;

    void HandleUrl(const char *data, size_t length) override;

    void HandleHeaderField(const char *data, size_t length) override;

    void HandleHeaderValue(const char *data, size_t length) override;

    int HandleHeadersComplete() override;

    void HandleBody(const char *data, size_t length) override;

    int HandleMessageComplete() override;

private:
    std::string query;
    std::string url;

    http::Request *request;

    std::deque<http::Request *> requests;
};

}    // namespace litebus

#endif    // __LITEBUS_HTTP_DECODER__
