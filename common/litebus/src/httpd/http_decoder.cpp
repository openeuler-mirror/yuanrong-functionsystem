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

#include "http_decoder.hpp"
#include "actor/buslog.hpp"
#include "httpd/http_parser.hpp"

using litebus::http::HTTP_INVALID_BODY_LENGTH;
using litebus::http::HTTP_INVALID_FIELD_LENGTH;
using litebus::http::HTTP_INVALID_URL_LENGTH;
using litebus::http::HTTP_INVALID_VALUE_LENGTH;

namespace litebus {

const unsigned int MAX_HTTP_URL_LENGTH = 2048;
const unsigned int MAX_HTTP_FIELD_LENGTH = 1024;
const unsigned int MAX_HTTP_VALUE_LENGTH = 65536;    // 64k

// by default, body length must be less than 20M (20*1024*1024)
const unsigned int MAX_HTTP_BODY_LENGTH = 20971520;

static const std::map<uint16_t, litebus::http::ResponseCode>::value_type g_initValue[] = {
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(100, litebus::http::CONTINUE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(101, litebus::http::SWITCHING_PROTOCOLS),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(200, litebus::http::OK),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(201, litebus::http::CREATED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(202, litebus::http::ACCEPTED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(203, litebus::http::NON_AUTHORITATIVE_INFORMATION),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(204, litebus::http::NO_CONTENT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(205, litebus::http::RESET_CONTENT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(206, litebus::http::PARTIAL_CONTENT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(300, litebus::http::MULTIPLE_CHOICES),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(301, litebus::http::MOVED_PERMANENTLY),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(302, litebus::http::FOUND),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(303, litebus::http::SEE_OTHER),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(304, litebus::http::NOT_MODIFIED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(305, litebus::http::USE_PROXY),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(307, litebus::http::TEMPORARY_REDIRECT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(400, litebus::http::BAD_REQUEST),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(401, litebus::http::UNAUTHORIZED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(402, litebus::http::PAYMENT_REQUIRED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(403, litebus::http::FORBIDDEN),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(404, litebus::http::NOT_FOUND),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(405, litebus::http::METHOD_NOT_ALLOWED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(406, litebus::http::NOT_ACCEPTABLE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(407, litebus::http::PROXY_AUTHENTICATION_REQUIRED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(408, litebus::http::REQUEST_TIMEOUT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(409, litebus::http::CONFLICT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(410, litebus::http::GONE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(411, litebus::http::LENGTH_REQUIRED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(412, litebus::http::PRECONDITION_FAILED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(413, litebus::http::REQUEST_ENTITY_TOO_LARGE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(414, litebus::http::REQUEST_URI_TOO_LARGE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(415, litebus::http::UNSUPPORTED_MEDIA_TYPE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(416, litebus::http::REQUESTED_RANGE_NOT_SATISFIABLE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(417, litebus::http::EXPECTATION_FAILED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(422, litebus::http::UNPROCESSABLE_ENTITY),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(428, litebus::http::PRECONDITION_REQUIRED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(429, litebus::http::TOO_MANY_REQUESTS),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(431, litebus::http::REQUEST_HEADER_FIELDS_TOO_LARGE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(500, litebus::http::INTERNAL_SERVER_ERROR),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(501, litebus::http::NOT_IMPLEMENTED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(502, litebus::http::BAD_GATEWAY),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(503, litebus::http::SERVICE_UNAVAILABLE),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(504, litebus::http::GATEWAY_TIMEOUT),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(505, litebus::http::HTTP_VERSION_NOT_SUPPORTED),
    std::map<uint16_t, litebus::http::ResponseCode>::value_type(511, litebus::http::NETWORK_AUTHENTICATION_REQUIRED)
};

std::map<uint16_t, litebus::http::ResponseCode> g_toResponse(g_initValue,
                                                             g_initValue
                                                                 + sizeof(g_initValue) / sizeof(g_initValue[0]));
namespace http_parsing {

bool OverWaitSize(http::HttpParserStatus parserStatus, unsigned int strSize)
{
    bool reachSize = false;
    switch (parserStatus) {
        case http::HttpParserStatus::S_REQUEST_URL_START:
        case http::HttpParserStatus::S_REQUEST_URL:
            reachSize = strSize > MAX_HTTP_URL_LENGTH;
            break;
        case http::HttpParserStatus::S_BODY_START:
        case http::HttpParserStatus::S_BODY:
        case http::HttpParserStatus::S_BODY_STRING_START:
        case http::HttpParserStatus::S_BODY_STRING:
            reachSize = strSize > MAX_HTTP_BODY_LENGTH;
            break;
        case http::HttpParserStatus::S_HEADERS_FIELD_START:
        case http::HttpParserStatus::S_HEADERS_FIELD:
            reachSize = strSize > MAX_HTTP_FIELD_LENGTH;
            break;
        case http::HttpParserStatus::S_HEADERS_VALUE_START:
        case http::HttpParserStatus::S_HEADERS_VALUE:
            reachSize = strSize > MAX_HTTP_VALUE_LENGTH;
            break;
        default:
            break;
    }
    return reachSize;
}
}    // namespace http_parsing

/* ResponseDecoder */
ResponseDecoder::ResponseDecoder() : response(nullptr), responses(std::deque<http::Response *>())
{
    Initialize();
}

ResponseDecoder::~ResponseDecoder()
{
    if (response != nullptr) {
        delete response;
        response = nullptr;
    }

    for (unsigned i = 0; i < responses.size(); i++) {
        if (responses[i] != nullptr) {
            delete responses[i];
            responses[i] = nullptr;
        }
    }
    responses.clear();
}

std::deque<http::Response *> ResponseDecoder::Decode(const char *data, size_t length, bool flgEOF)
{
    size_t responseParsed = Parse(data, length);
    bool bigSize = http_parsing::OverWaitSize(GetParserStatus(), GetWaitStrSize());
    if (responseParsed != length || bigSize) {
        http::HttpParserError parseResponseError = GetErrorCode();
        BUSLOG_INFO("parse data fail, parsedSize={}, length={}, parseError={}, bigSize={}", responseParsed, length,
                    static_cast<std::underlying_type_t<http::HttpParserError>>(parseResponseError), bigSize);

        failure = true;
    }

    if (flgEOF) {
        responseParsed = Parse("", 0);
    }

    if (!responses.empty()) {
        std::deque<http::Response *> result = responses;
        responses.clear();
        return result;
    }

    return std::deque<http::Response *>();
}

void ResponseDecoder::RegisterResponseCallBack(const http::ResponseCallback &f)
{
    responseCallback = std::move(f);
    isLongChunked = true;
}

void ResponseDecoder::HandleMessageBegin()
{
    // http attack
    if (failure) {
        UpdateError(HTTP_INVALID_BODY_LENGTH);
        return;
    }

    header = ParserHeaderType::HEADER_FIELD;
    field.clear();
    value.clear();
    response = new (std::nothrow) http::Response(litebus::http::CONTINUE);
    BUS_OOM_EXIT(response);
    response->headers.clear();
    response->body.clear();
}

void ResponseDecoder::HandleUrl(const char *, size_t)
{
    // null
}

void ResponseDecoder::HandleHeaderField(const char *data, size_t length)
{
    BUS_ASSERT(response != nullptr);

    if (header != ParserHeaderType::HEADER_FIELD) {
        if (!field.empty()) {
            response->headers[field] = value;
        }
        field.clear();
        value.clear();
    }

    size_t responseEndPoint = length >= 1 ? length - 1 : 0;
    field = GetWaitString();
    // http attack
    if ((field.size() + responseEndPoint) > MAX_HTTP_FIELD_LENGTH) {
        UpdateError(HTTP_INVALID_FIELD_LENGTH);
        return;
    }

    (void)field.append(data, responseEndPoint);
    header = ParserHeaderType::HEADER_FIELD;
}

void ResponseDecoder::HandleHeaderValue(const char *data, size_t length)
{
    BUS_ASSERT(response != nullptr);

    std::string responseHeaderWaitString = GetWaitString();
    // http attack
    if ((value.size() + responseHeaderWaitString.size() + length) > MAX_HTTP_VALUE_LENGTH) {
        UpdateError(HTTP_INVALID_VALUE_LENGTH);
        return;
    }

    (void)value.append(responseHeaderWaitString);
    (void)value.append(data, length);
    header = ParserHeaderType::HEADER_VALUE;
}

int ResponseDecoder::HandleHeadersComplete()
{
    BUS_ASSERT(response != nullptr);
    if (!field.empty()) {
        response->headers[field] = value;
    }
    field.clear();
    value.clear();

    return http_parsing::SUCCESS;
}

void ResponseDecoder::HandleBody(const char *data, size_t length)
{
    BUS_ASSERT(response != nullptr);

    std::string bodyWaitString = GetWaitString();
    // http attack
    if ((response->body.size() + bodyWaitString.size() + length) > MAX_HTTP_BODY_LENGTH) {
        BUSLOG_INFO("Response body is too large !");
        failure = true;
        return;
    }

    (void)response->body.append(bodyWaitString);
    (void)response->body.append(data, length);
    if (!response->body.empty() && isLongChunked) {
        responseCallback(response);
        response->body.clear();
    }
}

int ResponseDecoder::HandleMessageComplete()
{
    BUS_ASSERT(response != nullptr);

    uint16_t statusCode = GetStatusCode();
    auto iterCode = g_toResponse.find(statusCode);
    if (g_toResponse.end() != iterCode) {
        response->retCode = g_toResponse[statusCode];
    } else {
        failure = true;
        return http_parsing::FAILURE;
    }

    if (failure) {
        delete response;
        response = nullptr;
    } else {
        responses.push_back(response);
        response = nullptr;
    }

    return http_parsing::SUCCESS;
}

/* RequestDecoder */
RequestDecoder::RequestDecoder() : request(nullptr), requests(std::deque<http::Request *>())
{
    Initialize();
}

RequestDecoder::~RequestDecoder()
{
    if (request != nullptr) {
        delete request;
        request = nullptr;
    }

    for (unsigned i = 0; i < requests.size(); i++) {
        if (requests[i] != nullptr) {
            delete requests[i];
            requests[i] = nullptr;
        }
    }
    requests.clear();
}

std::deque<http::Request *> RequestDecoder::Decode(const char *data, size_t length)
{
    size_t requestParsed = Parse(data, length);
    bool bigSize = http_parsing::OverWaitSize(GetParserStatus(), GetWaitStrSize());
    if (requestParsed != length || bigSize) {
        http::HttpParserError paserRequestError = GetErrorCode();
        BUSLOG_INFO("parse data fail, parsedSize={}, length={}, parseError={}, bigsize={}", requestParsed, length,
                    static_cast<std::underlying_type_t<http::HttpParserError>>(paserRequestError), bigSize);
        failure = true;
    }

    if (!requests.empty()) {
        std::deque<http::Request *> result = requests;
        requests.clear();
        return result;
    }

    return std::deque<http::Request *>();
}

void RequestDecoder::HandleMessageBegin()
{
    // http attack
    if (failure) {
        UpdateError(HTTP_INVALID_BODY_LENGTH);
        return;
    }

    header = ParserHeaderType::HEADER_FIELD;
    field.clear();
    value.clear();
    query.clear();
    url.clear();
    request = new (std::nothrow) http::Request();
    BUS_OOM_EXIT(request);
}

void RequestDecoder::HandleUrl(const char *data, size_t length)
{
    size_t endPoint = length >= 1 ? length - 1 : 0;
    url = GetWaitString();
    // http attack
    if ((url.size() + endPoint) > MAX_HTTP_URL_LENGTH) {
        UpdateError(HTTP_INVALID_URL_LENGTH);
        return;
    }

    (void)url.append(data, endPoint);

    Try<http::URL> tUrl = http::URL::Decode(url, url[0] != '/');
    if (tUrl.IsError()) {
        UpdateError(http::HTTP_INVALID_URL);
        return;
    }
}

void RequestDecoder::HandleHeaderField(const char *data, size_t length)
{
    if (header != ParserHeaderType::HEADER_FIELD) {
        if (!field.empty()) {
            request->headers[field] = value;
        }
        field.clear();
        value.clear();
    }
    size_t requestEndPoint = length >= 1 ? length - 1 : 0;
    field = GetWaitString();
    // http attack
    if ((field.size() + requestEndPoint) > MAX_HTTP_FIELD_LENGTH) {
        UpdateError(HTTP_INVALID_FIELD_LENGTH);
        return;
    }

    (void)field.append(data, requestEndPoint);
    header = ParserHeaderType::HEADER_FIELD;
}

void RequestDecoder::HandleHeaderValue(const char *data, size_t length)
{
    std::string requestHeaderWaitString = GetWaitString();
    // http attack
    if ((value.size() + requestHeaderWaitString.size() + length) > MAX_HTTP_VALUE_LENGTH) {
        UpdateError(HTTP_INVALID_VALUE_LENGTH);
        return;
    }

    (void)value.append(requestHeaderWaitString);
    (void)value.append(data, length);
    header = ParserHeaderType::HEADER_VALUE;
}

int RequestDecoder::HandleHeadersComplete()
{
    BUS_ASSERT(request != nullptr);

    // Add final header.
    if (!field.empty()) {
        request->headers[field] = value;
    }
    field.clear();
    value.clear();

    request->method = GetMethodString();
    request->keepAlive = GetKeepAlive();
    size_t schemeIndex = url.find("://");
    bool domainDecode = (schemeIndex != std::string::npos);
    Try<http::URL> httpUrl = http::URL::Decode(url, domainDecode);
    url.clear();
    if (httpUrl.IsError()) {
        return http_parsing::FAILURE;
    }
    request->url = httpUrl.Get();

    return http_parsing::SUCCESS;
}

void RequestDecoder::HandleBody(const char *data, size_t length)
{
    std::string bodyWaitString = GetWaitString();
    // http attack
    if ((request->body.size() + bodyWaitString.size() + length) > MAX_HTTP_BODY_LENGTH) {
        BUSLOG_INFO("Request body is too large !");
        failure = true;
        return;
    }

    (void)request->body.append(bodyWaitString);
    (void)request->body.append(data, length);
}

int RequestDecoder::HandleMessageComplete()
{
    if (failure) {
        delete request;
        request = nullptr;
    } else {
        requests.push_back(request);
        request = nullptr;
    }

    return http_parsing::SUCCESS;
}

}    // namespace litebus
