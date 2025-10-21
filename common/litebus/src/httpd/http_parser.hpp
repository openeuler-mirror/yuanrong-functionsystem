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

#ifndef __LITEBUS_HTTP_PARSER_HPP__
#define __LITEBUS_HTTP_PARSER_HPP__

#include <map>
#include <string>

#include "httpd/http.hpp"

namespace litebus {
namespace http {

enum HttpMethod {
    HTTP_DELETE = 0,
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_UNKNOWN,
};

enum HttpParserType {
    HTTP_REQUEST = 0,
    HTTP_RESPONSE,
    HTTP_BOTH,
};

enum HttpParserStatus {
    // Parser Start
    S_PARSER_START = 0,

    // Request Start
    S_REQUEST_START = 1,

    // Request Method Start
    S_REQUEST_METHOD_START = 2,
    S_REQUEST_METHOD = 3,

    // Request Url
    S_REQUEST_URL_START = 4,
    S_REQUEST_URL = 5,

    // Response Start
    S_RESPONSE_START = 6,
    S_RESPONSE_CODE = 7,
    S_RESPONSE_STATUS = 8,

    // Http Version
    S_HTTP_VERSION_START = 9,
    S_HTTP_VERSION_SCHEME = 10,
    S_HTTP_VERSION_MAJOR = 11,
    S_HTTP_VERSION_POINT = 12,
    S_HTTP_VERSION_MINOR = 13,
    S_HTTP_VERSION_END = 14,

    // Headers Start
    S_HEADERS_START = 15,
    S_HEADERS_FIELD_START = 16,
    S_HEADERS_FIELD = 17,
    S_HEADERS_FIELD_END = 18,
    S_HEADERS_VALUE_START = 19,
    S_HEADERS_VALUE = 20,
    S_HEADERS_VALUE_END = 21,
    S_HEADERS_END = 22,

    // Body Start
    S_BODY_START = 23,
    S_BODY_CHECK = 24,
    S_BODY = 25,
    S_BODY_STRING_START = 26,
    S_BODY_STRING_CHECK = 27,
    S_BODY_STRING = 28,
    S_BODY_IGNORE = 29,

    S_BODY_IDENTITY_EOF = 30,
};
constexpr int HTTP_PARSER_STATUS_COUNT = 31;

enum HttpHeaderStatus {
    HS_CONNECTION = 0,
    HS_CONTENT_LENGTH,
    HS_KEEP_ALIVE,
    HS_PROXY_CONNECTION,
    HS_TRANSFER_ENCODIG,
    HS_UPGRADE,
    HS_GENERAL,
};

enum HttpValueStatus {
    VS_CHUNKED = 0,
    VS_CLOSE,
    VS_KEEP_ALIVE,
    VS_UPGRADE,
    VS_GENERAL,
};

enum HttpParserFlag {
    F_CHUNKED = 1,
    F_CONNECTION_CLOSE = (1 << 1),
    F_CONNECTION_KEEP_ALIVE = (1 << 2),
    F_CONNECTION_UPGRADE = (1 << 3),
    F_CONTENTLENGTH = (1 << 4),
    F_SKIPBODY = (1 << 5),
    F_TRAILING = (1 << 6),
    F_UPGRADE = (1 << 7),
};

enum HttpParserError {
    HTTP_PARSER_OK = 0,
    HTTP_INVALID_DATA,
    HTTP_INVALID_METHOD,
    HTTP_INVALID_URL,
    HTTP_INVALID_SCHEME,
    HTTP_INVALID_VERSION,
    HTTP_INVALID_RESPONSE_CODE,
    HTTP_LF_EXPECTED,
    HTTP_INVALID_HEADER_TOKEN,
    HTTP_INVALID_VALUE_TOKEN,
    HTTP_INVALID_CONTENT_LENGTH,
    HTTP_INVALID_CHUNK_SIZE,
    HTTP_INVALID_EOF,
    HTTP_INVALID_HEADER_NUM,
    HTTP_INVALID_CHAR,
    HTTP_INVALID_URL_LENGTH,
    HTTP_INVALID_FIELD_LENGTH,
    HTTP_INVALID_VALUE_LENGTH,
    HTTP_INVALID_BODY_LENGTH,
};
class ParserFuncMap;

class HttpParser {
public:
    HttpParser();
    virtual ~HttpParser();

    void Initialize();

    size_t Parse(const char *data, size_t length);

    size_t ParseRequest(const char *data, size_t length);

    size_t ParseResponse(const char *data, size_t length);

    size_t ParseReqOrRes(const char *tData, size_t length, bool isRequest);

    bool Failed() const;

    HttpParserError GetErrorCode() const;

    void UpdateError(HttpParserError error);

    std::string GetMethodString() const;

    bool GetKeepAlive() const;

    unsigned int GetStatusCode() const;

    std::string GetWaitString();

    bool IsLongChunked() const
    {
        return isLongChunked;
    }

protected:
    bool failure;

    enum class ParserHeaderType { HEADER_FIELD, HEADER_VALUE } header;

    std::string field;
    std::string value;
    bool isLongChunked;

    virtual void HandleMessageBegin() = 0;

    virtual void HandleUrl(const char *data, size_t length) = 0;

    virtual void HandleHeaderField(const char *data, size_t length) = 0;

    virtual void HandleHeaderValue(const char *data, size_t length) = 0;

    virtual int HandleHeadersComplete() = 0;

    virtual void HandleBody(const char *data, size_t length) = 0;

    virtual int HandleMessageComplete() = 0;
    HttpParserStatus GetParserStatus() const;

    unsigned int GetWaitStrSize() const;

private:
    void UpdateType(HttpParserType tType);

    void UpdateStatus(HttpParserStatus status);

    void UpdateStatusToNewMessage();

    void UpdateHeaderStatus(HttpHeaderStatus status);

    void UpdateValueStatus(HttpValueStatus status);

    void UpdateFlags(HttpParserFlag flag);

    void UpdateFlagsToZero();

    void UpdateFlagsByHeaderStatus();

    void UpdateFlagsByValueStatus();

    void UpdateIndex();

    void UpdateIndexToZero();

    void UpdateCode(unsigned int code);

    void UpdateCodeToZero();

    void UpdateNumOfHeaders();

    void UpdateNumOfHeadersToZero();

    void UpdateContentLength(unsigned long length);

    void UpdateContentLengthToMax();

    void UpdateBuffer();

    void UpdateMethod(HttpMethod method);

    bool MessageNeedsEof() const;

    void UpdateKeepAlive();

    void UpdateUrlPort(uint16_t port);

    void UpdateHttpMajor(unsigned short major);

    void UpdateHttpMajorToZero();

    void UpdateHttpMinor(const unsigned short minor);

    void UpdateHttpMinorToZero();

    void ParseStart(char ch);

    bool CheckStatus(const char *data, size_t length);

    size_t ParseBothResReq(const char *data, size_t length);

    size_t ParseBranch(const char *data, size_t length);

    void ParseHeadersStatus(char ch);

    void ParseHeadersConnection(char ch);

    void ParseHeadersFlags(char ch);

    // Parse Request
    void ParseRequestStart(char ch);

    // Parse Request Method
    void ParseRequestMethodStart(char ch);

    void ParseRequestMethod(char ch);

    // Parse Request Url
    void ParseRequestUrlStart(char ch);

    void ParseRequestUrl(char ch);

    // Parse Http Version
    void ParseHttpVersionStart(char ch);

    void ParseHttpVersionScheme(char ch);

    void ParseHttpVersionMajor(char ch);

    void ParseHttpVersionPoint(char ch);

    void ParseHttpVersionMinor(char ch);

    void ParseHttpVersionEnd(char ch);

    // Parse Headers
    void ParseHeadersStart(char ch);

    void ParseHeadersFieldStart(char ch);

    void ParseHeadersField(char ch);

    void ParseHeadersFieldEnd(char ch);

    void ParseHeadersValueStart(char ch);

    void ParseHeadersValueContent(char ch);

    void ParseHeadersValueEncoding(char ch);

    void ParseHeadersValueClose(char ch);

    void ParseHeadersValue(char ch);

    void ParseHeadersValueEnd(char ch);

    void ParseHeadersEnd(char ch);

    void CheckHeadersValueAndUpdate(char ch);

    // Parse Body
    void ParseBodyStart(char ch);

    void ParseBodyCheck(char ch);

    void ParseBody(char ch);

    void ParseBodyStringStart(char ch);

    void ParseBodyStringCheck(char ch);

    void ParseBodyString(char ch);

    void ParseBodyGeneral(char ch);

    void ParseBodyChunked(char ch);

    void ParseBodyUpgrade(char ch);

    void ParseBodyOthers(char ch);

    void ParseBodyIgnore(char ch);

    // Parse Response
    void ParseResponseStart(char ch);

    void ParseResponseCode(char ch);

    void ParseResponseStatus(char ch);

    void ParseBodyIdentityEOF(char ch);

    void DoNothing(char) const {};

    HttpParserType type;
    HttpParserError error;
    HttpParserStatus status;
    HttpHeaderStatus headerStatus;
    HttpValueStatus valueStatus;

    HttpMethod method;

    unsigned short httpMajor;
    unsigned short httpMinor;

    unsigned int flags;
    unsigned int index;
    unsigned int code;
    unsigned int headerNum;

    uint64_t contentLength;
    bool keepAlive;
    std::string key;
    HeaderMap headers;
    std::string waitStr;

    const char *bufCur;
    const char *bufPre;
    // functions to parser, to avoid case expression, execute functions by the status
    // should make sure HttpParserStatus all in the funtions and in correct turn
    static std::function<void(HttpParser &, char)> paurserFuncs[HTTP_PARSER_STATUS_COUNT];
};

}    // namespace http
}    // namespace litebus

#endif
