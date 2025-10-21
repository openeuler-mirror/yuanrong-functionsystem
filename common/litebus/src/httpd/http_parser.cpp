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

#include <securec.h>

#include "httpd/http_parser.hpp"
namespace litebus {
namespace http {

static const char CR = '\r';
static const char HT = '\t';
static const char LF = '\n';
static const char NUL = '\0';
static const char SPACE = ' ';

static const unsigned char UNIT_SEPARATOR_ASCII_INDEX = 31;
static const unsigned char DEL_ASCII_INDEX = 127;

static const unsigned int DEC_CONVERT = 10;
static const unsigned int HEX_CONVERT = 16;

static const unsigned int MAX_CONVERT_INDEX = 128;

static const unsigned int MAX_CHECK_HTTP_CONNECTION_INDEX = 4;

static const unsigned int HTTP_CODE_NUM = 100;
static const unsigned int HTTP_CODE_NO_CONTENT = 204;
static const unsigned int HTTP_CODE_NOT_MODIFIED = 304;

static const unsigned short INIT_HTTP_MAJOR = 0;
static const unsigned short INIT_HTTP_MINOR = 9;
static const unsigned short MAX_HTTP_VERSION = 999;
static const unsigned short MAX_HTTP_HEAD_NUM = 999;
static const unsigned int MAX_HTTP_CONTENT_LENGTH = 20971520; // 20M
static const unsigned int MAX_HTTP_CHUNK_LENGTH = 20971520; // 20M
static const unsigned int MAX_HTTP_CODE_LENGTH = 65535;
#undef ULLONG_MAX
static const uint64_t ULLONG_MAX = ((uint64_t)-1);

static const char *HTTP_VERISON_STRING = "HTTP";

static const char HEADER_CONVERT_TOKENS[MAX_CONVERT_INDEX] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   '!', 0,   '#', '$', '%', '&', '\'', 0,   0,   '*', '+',
    0,   '-', '.', 0,   '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0,   0,   0,   0,    0,   0,   0,   'a',
    'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',  't', 'u', 'v', 'w',
    'x', 'y', 'z', 0,   0,   0,   '^', '_', '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',  'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0,   '|', 0,   '~', 0,
};

static const int HEX_CONVERT_TOKENS[MAX_CONVERT_INDEX] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,
    4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

static const char *g_methodStrings[] = {
    "DELETE", "GET", "POST", "PUT", "PATCH", "UNKNOWN",
};

static const char *g_headerStrings[] = {
    "connection", "content-length", "keep-alive", "proxy-connection", "transfer-encoding", "upgrade", "general",
};

static const char *g_valueStrings[] = {
    "chunked", "close", "keep-alive", "upgrade", "general",
};

// funtions to parser,  all HttpParserStatus functions should list in turn
std::function<void(HttpParser &, char)> HttpParser::paurserFuncs[HTTP_PARSER_STATUS_COUNT] = {
    &HttpParser::DoNothing,
    &HttpParser::ParseRequestStart,
    &HttpParser::ParseRequestMethodStart,
    &HttpParser::ParseRequestMethod,
    &HttpParser::ParseRequestUrlStart,
    &HttpParser::ParseRequestUrl,
    &HttpParser::ParseResponseStart,
    &HttpParser::ParseResponseCode,
    &HttpParser::ParseResponseStatus,
    &HttpParser::ParseHttpVersionStart,
    &HttpParser::ParseHttpVersionScheme,
    &HttpParser::ParseHttpVersionMajor,
    &HttpParser::ParseHttpVersionPoint,
    &HttpParser::ParseHttpVersionMinor,
    &HttpParser::ParseHttpVersionEnd,
    &HttpParser::ParseHeadersStart,
    &HttpParser::ParseHeadersFieldStart,
    &HttpParser::ParseHeadersField,
    &HttpParser::ParseHeadersFieldEnd,
    &HttpParser::ParseHeadersValueStart,
    &HttpParser::ParseHeadersValue,
    &HttpParser::ParseHeadersValueEnd,
    &HttpParser::ParseHeadersEnd,
    &HttpParser::ParseBodyStart,
    &HttpParser::ParseBodyCheck,
    &HttpParser::ParseBody,
    &HttpParser::ParseBodyStringStart,
    &HttpParser::ParseBodyStringCheck,
    &HttpParser::ParseBodyString,
    &HttpParser::ParseBodyIgnore,
    &HttpParser::ParseBodyIdentityEOF
};

HttpParser::HttpParser()
    : failure(false),
      header(ParserHeaderType::HEADER_FIELD),
      isLongChunked(false),
      type(HTTP_BOTH),
      error(HTTP_PARSER_OK),
      status(S_PARSER_START),
      headerStatus(HS_GENERAL),
      valueStatus(VS_GENERAL),
      method(HTTP_UNKNOWN),
      httpMajor(INIT_HTTP_MAJOR),
      httpMinor(INIT_HTTP_MINOR),
      flags(0),
      index(0),
      code(0),
      headerNum(0),
      contentLength(ULLONG_MAX),
      keepAlive(false),
      key(""),
      headers(HeaderMap()),
      waitStr(""),
      bufCur(nullptr),
      bufPre(nullptr)
{
}

HttpParser::~HttpParser()
{
    if (bufCur != nullptr) {
        bufCur = nullptr;
    }
    if (bufPre != nullptr) {
        bufPre = nullptr;
    }
}

void HttpParser::Initialize()
{
    UpdateType(HTTP_BOTH);
    UpdateError(HTTP_PARSER_OK);
    UpdateStatus(S_PARSER_START);
    UpdateHeaderStatus(HS_GENERAL);
    UpdateValueStatus(VS_GENERAL);
    UpdateMethod(HTTP_UNKNOWN);
    UpdateHttpMajor(INIT_HTTP_MAJOR);
    UpdateHttpMinor(INIT_HTTP_MINOR);
    UpdateFlagsToZero();
    UpdateIndexToZero();
    UpdateCodeToZero();
    UpdateNumOfHeadersToZero();
    UpdateContentLengthToMax();
    keepAlive = false;
    key.clear();
    headers.clear();
    waitStr.clear();
}

void HttpParser::UpdateType(HttpParserType tType)
{
    this->type = tType;
}

void HttpParser::UpdateStatus(HttpParserStatus tStatus)
{
    this->status = tStatus;
}

void HttpParser::UpdateStatusToNewMessage()
{
    if (type == HTTP_REQUEST) {
        UpdateStatus(S_REQUEST_START);
    } else if (type == HTTP_RESPONSE) {
        UpdateStatus(S_RESPONSE_START);
    }
    std::string().swap(waitStr);
    waitStr.clear();
}

void HttpParser::UpdateHeaderStatus(HttpHeaderStatus tStatus)
{
    this->headerStatus = tStatus;
}

void HttpParser::UpdateValueStatus(HttpValueStatus tStatus)
{
    this->valueStatus = tStatus;
}

void HttpParser::UpdateFlags(HttpParserFlag flag)
{
    this->flags |= static_cast<unsigned int>(flag);
}

void HttpParser::UpdateFlagsToZero()
{
    this->flags = 0;
}

void HttpParser::UpdateFlagsByHeaderStatus()
{
    switch (headerStatus) {
        case HS_UPGRADE:
            UpdateFlags(F_UPGRADE);
            break;
        case HS_CONTENT_LENGTH:
            UpdateFlags(F_CONTENTLENGTH);
            break;
        default:
            break;
    }
}

void HttpParser::UpdateFlagsByValueStatus()
{
    switch (valueStatus) {
        case VS_CHUNKED:
            UpdateFlags(F_CHUNKED);
            break;
        case VS_CLOSE:
            UpdateFlags(F_CONNECTION_CLOSE);
            break;
        case VS_KEEP_ALIVE:
            UpdateFlags(F_CONNECTION_KEEP_ALIVE);
            break;
        case VS_UPGRADE:
            UpdateFlags(F_CONNECTION_UPGRADE);
            break;
        default:
            break;
    }
}

void HttpParser::UpdateIndex()
{
    ++(this->index);
}

void HttpParser::UpdateIndexToZero()
{
    this->index = 0;
}

void HttpParser::UpdateCode(unsigned int tCode)
{
    this->code = tCode;
}

void HttpParser::UpdateCodeToZero()
{
    this->code = 0;
}

void HttpParser::UpdateNumOfHeaders()
{
    ++(this->headerNum);
}

void HttpParser::UpdateNumOfHeadersToZero()
{
    this->headerNum = 0;
}

void HttpParser::UpdateContentLength(unsigned long length)
{
    this->contentLength = length;
}

void HttpParser::UpdateContentLengthToMax()
{
    this->contentLength = ULLONG_MAX;
}

void HttpParser::UpdateBuffer()
{
    this->index = 0;
    this->bufPre = bufCur;
}

void HttpParser::UpdateError(HttpParserError tError)
{
    this->error = tError;
}

void HttpParser::UpdateMethod(HttpMethod tMethod)
{
    this->method = tMethod;
}

bool HttpParser::MessageNeedsEof() const
{
    if (type == HTTP_REQUEST) {
        return false;
    }

    if (code / HTTP_CODE_NUM == 1 ||                       /* 1xx e.g. Continue */
        code == HTTP_CODE_NO_CONTENT ||                    /* No Content */
        code == HTTP_CODE_NOT_MODIFIED ||                  /* Not Modified */
        (flags & static_cast<unsigned int>(F_SKIPBODY))) { /* response to a HEAD request */
        return false;
    }

    if ((flags & static_cast<unsigned int>(F_CHUNKED)) || (flags & static_cast<unsigned int>(F_CONTENTLENGTH))) {
        return false;
    }

    return true;
}

void HttpParser::UpdateKeepAlive()
{
    if (httpMajor > 0 && httpMinor > 0) {
        /* HTTP/1.1 */
        if (flags & static_cast<unsigned int>(F_CONNECTION_CLOSE)) {
            keepAlive = false;
            return;
        }
    } else {
        /* HTTP/1.0 or earlier */
        if (!(flags & static_cast<unsigned int>(F_CONNECTION_KEEP_ALIVE))) {
            keepAlive = false;
            return;
        }
    }

    keepAlive = !MessageNeedsEof();
}

void HttpParser::UpdateHttpMajor(const unsigned short major)
{
    this->httpMajor = major;
}

void HttpParser::UpdateHttpMajorToZero()
{
    this->httpMajor = 0;
}

void HttpParser::UpdateHttpMinor(const unsigned short minor)
{
    this->httpMinor = minor;
}

void HttpParser::UpdateHttpMinorToZero()
{
    this->httpMinor = 0;
}

HttpParserError HttpParser::GetErrorCode() const
{
    return this->error;
}

bool HttpParser::Failed() const
{
    return failure;
}

std::string HttpParser::GetMethodString() const
{
    return g_methodStrings[static_cast<unsigned int>(this->method)];
}

bool HttpParser::GetKeepAlive() const
{
    return this->keepAlive;
}

unsigned int HttpParser::GetStatusCode() const
{
    return this->code;
}

std::string HttpParser::GetWaitString()
{
    return this->waitStr;
}

void HttpParser::ParseStart(char ch)
{
    if (ch == CR || ch == LF || type != HTTP_BOTH) {
        return;
    }

    Initialize();

    if (ch == 'H') {
        UpdateType(HTTP_RESPONSE);
        UpdateStatus(S_RESPONSE_START);
        return;
    }

    UpdateType(HTTP_REQUEST);
    UpdateStatus(S_REQUEST_START);
}

size_t HttpParser::ParseBranch(const char *tData, size_t length)
{
    size_t parsed = 0;

    if (length == 0) {
        return parsed;
    }

    switch (type) {
        case HTTP_REQUEST:
            parsed = ParseRequest(tData, length);
            break;
        case HTTP_RESPONSE:
            parsed = ParseResponse(tData, length);
            break;
        default:
            parsed = ParseBothResReq(tData, length);
            break;
    }

    return parsed;
}

void HttpParser::ParseRequestStart(char ch)
{
    if (ch == CR || ch == LF) {
        return;
    }

    switch (ch) {
        case 'D':
            UpdateMethod(HTTP_DELETE);
            break;
        case 'G':
            UpdateMethod(HTTP_GET);
            break;
        case 'P':
            UpdateMethod(HTTP_POST);
            break;
        default:
            BUSLOG_ERROR("parse request error: http invalid method");
            UpdateError(HTTP_INVALID_METHOD);
            return;
    }

    UpdateStatus(S_REQUEST_METHOD_START);
    HandleMessageBegin();
    UpdateBuffer();
    UpdateFlagsToZero();
    UpdateContentLengthToMax();
    UpdateIndex();
}

void HttpParser::ParseRequestMethodStart(char ch)
{
    if (method == HTTP_POST && ch == 'U') {
        UpdateMethod(HTTP_PUT);
        UpdateIndex();
        return;
    }
    if (method == HTTP_POST && ch == 'A') {
        UpdateMethod(HTTP_PATCH);
        UpdateIndex();
        return;
    }

    UpdateStatus(S_REQUEST_METHOD);
    ParseRequestMethod(ch);
}

void HttpParser::ParseRequestMethod(char ch)
{
    if (ch == NUL) {
        BUSLOG_ERROR("parse request error: http invalid method");
        UpdateError(HTTP_INVALID_METHOD);
        return;
    }

    UpdateIndex();

    const char *methodStr = g_methodStrings[static_cast<unsigned int>(method)];
    unsigned int checkPoint = waitStr.size() > 0 ? ((waitStr.size() - 1) + index) : (index - 1);
    if (ch == SPACE && methodStr[checkPoint] == NUL) {
        UpdateStatus(S_REQUEST_URL_START);
        std::string().swap(waitStr);
        waitStr.clear();
        UpdateBuffer();
        return;
    }

    if (ch == methodStr[checkPoint]) {
        return;
    }

    BUSLOG_ERROR("parse request error: http invalid method");
    UpdateError(HTTP_INVALID_METHOD);
}

void HttpParser::ParseRequestUrlStart(char ch)
{
    if (ch == SPACE) {
        return;
    }

    UpdateStatus(S_REQUEST_URL);
    UpdateBuffer();
    UpdateIndex();
}

void HttpParser::ParseRequestUrl(char ch)
{
    UpdateIndex();
    if (ch != SPACE && ch != CR && ch != LF) {
        return;
    }

    UpdateStatus(S_HTTP_VERSION_START);
    HandleUrl(bufPre, index);
    std::string().swap(waitStr);
    waitStr.clear();
    UpdateBuffer();

    if (ch == CR || ch == LF) {
        ParseHttpVersionEnd(ch);
    }
}

void HttpParser::ParseHttpVersionStart(char ch)
{
    if (ch == SPACE) {
        return;
    }

    if (ch == 'H') {
        UpdateStatus(S_HTTP_VERSION_SCHEME);
        UpdateBuffer();
        UpdateIndex();
        return;
    }

    UpdateError(HTTP_INVALID_VERSION);
}

void HttpParser::ParseHttpVersionScheme(char ch)
{
    UpdateIndex();
    unsigned int checkPoint = waitStr.size() > 0 ? ((waitStr.size() - 1) + index) : (index - 1);
    if (ch == HTTP_VERISON_STRING[checkPoint]) {
        return;
    }

    if (HTTP_VERISON_STRING[checkPoint] != NUL) {
        UpdateError(HTTP_INVALID_SCHEME);
        return;
    }

    if (ch == '/') {
        UpdateStatus(S_HTTP_VERSION_MAJOR);
        std::string().swap(waitStr);
        waitStr.clear();
        UpdateBuffer();
        return;
    }

    UpdateError(HTTP_INVALID_SCHEME);
    UpdateHttpMajorToZero();
}

void HttpParser::ParseHttpVersionMajor(char ch)
{
    if (ch < '0' || ch > '9') {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    UpdateHttpMajor(static_cast<unsigned short>(ch - '0'));
    UpdateStatus(S_HTTP_VERSION_POINT);
}

void HttpParser::ParseHttpVersionPoint(char ch)
{
    if (ch == '.') {
        UpdateStatus(S_HTTP_VERSION_MINOR);
        UpdateHttpMinorToZero();
        return;
    }

    if (ch < '0' || ch > '9') {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    unsigned short major = (httpMajor * DEC_CONVERT) + (ch - '0');
    if (major > MAX_HTTP_VERSION) {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    UpdateHttpMajor(major);
}

void HttpParser::ParseHttpVersionMinor(char ch)
{
    if (ch < '0' || ch > '9') {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    UpdateHttpMinor(static_cast<unsigned short>(ch - '0'));
    UpdateStatus(S_HTTP_VERSION_END);
}

void HttpParser::ParseHttpVersionEnd(char ch)
{
    UpdateCodeToZero();

    if (ch == CR) {
        UpdateStatus(S_HEADERS_START);
        UpdateBuffer();
        return;
    }

    if (ch == LF) {
        UpdateStatus(S_HEADERS_FIELD_START);
        return;
    }

    if (ch == SPACE && type == HTTP_RESPONSE) {
        UpdateStatus(S_RESPONSE_CODE);
        UpdateBuffer();
        return;
    }

    if (ch < '0' || ch > '9') {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    unsigned short minor = (httpMinor * DEC_CONVERT) + (ch - '0');
    if (minor > MAX_HTTP_VERSION) {
        UpdateError(HTTP_INVALID_VERSION);
        return;
    }

    UpdateHttpMinor(minor);
}

void HttpParser::ParseHeadersStart(char ch)
{
    if (ch != LF) {
        UpdateError(HTTP_LF_EXPECTED);
        return;
    }

    UpdateStatus(S_HEADERS_FIELD_START);
    UpdateBuffer();
}

void HttpParser::ParseHeadersFieldStart(char ch)
{
    if (ch == CR) {
        UpdateStatus(S_HEADERS_END);
        return;
    }

    if (ch == LF) {
        UpdateStatus(S_HEADERS_END);
        ParseHeadersEnd(ch);
        return;
    }

    if (ch == SPACE || ch == HT) {
        if (headers[key].empty()) {
            UpdateStatus(S_HEADERS_FIELD_END);
            ParseHeadersFieldEnd(ch);
            return;
        }

        UpdateStatus(S_HEADERS_VALUE_START);
        ParseHeadersValueStart(ch);
        return;
    }

    ParseHeadersStatus(ch);

    UpdateStatus(S_HEADERS_FIELD);
    UpdateBuffer();
    UpdateIndex();
}

void HttpParser::ParseHeadersStatus(char ch)
{
    if (static_cast<unsigned int>(ch) >= MAX_CONVERT_INDEX) {
        UpdateError(HTTP_INVALID_HEADER_TOKEN);
        return;
    }

    char c = HEADER_CONVERT_TOKENS[static_cast<unsigned int>(ch)];
    if (c == 0) {
        UpdateError(HTTP_INVALID_HEADER_TOKEN);
        return;
    }

    switch (c) {
        case 'c':
            UpdateHeaderStatus(HS_CONNECTION);
            break;
        case 'p':
            UpdateHeaderStatus(HS_PROXY_CONNECTION);
            break;
        case 't':
            UpdateHeaderStatus(HS_TRANSFER_ENCODIG);
            break;
        case 'u':
            UpdateHeaderStatus(HS_UPGRADE);
            break;
        default:
            UpdateHeaderStatus(HS_GENERAL);
            break;
    }
}

void HttpParser::ParseHeadersConnection(char ch)
{
    if (waitStr.size() + index != MAX_CHECK_HTTP_CONNECTION_INDEX) {
        return;
    }

    if (ch == 't') {
        UpdateHeaderStatus(HS_CONTENT_LENGTH);
    }
}

void HttpParser::ParseHeadersFlags(char ch)
{
    if (headerStatus == HS_GENERAL) {
        return;
    }

    if (headerStatus == HS_CONNECTION) {
        ParseHeadersConnection(ch);
    }

    const char *headerStr = g_headerStrings[static_cast<unsigned int>(headerStatus)];
    unsigned int checkPoint = waitStr.size() > 0 ? ((waitStr.size() - 1) + index) : (index - 1);
    if (ch == headerStr[checkPoint]) {
        return;
    }

    UpdateHeaderStatus(HS_GENERAL);
}

void HttpParser::ParseHeadersField(char ch)
{
    UpdateIndex();

    if (ch == ':') {
        key = waitStr;
        key += std::string(bufPre, index - 1);
        HandleHeaderField(bufPre, index);
        std::string().swap(waitStr);
        waitStr.clear();
        UpdateFlagsByHeaderStatus();
        UpdateNumOfHeaders();
        UpdateBuffer();
        UpdateValueStatus(VS_GENERAL);
        UpdateStatus(S_HEADERS_FIELD_END);
        if (this->headerNum > MAX_HTTP_HEAD_NUM) {
            UpdateError(HTTP_INVALID_HEADER_NUM);
        }
        return;
    }

    if (static_cast<unsigned int>(ch) >= MAX_CONVERT_INDEX) {
        UpdateError(HTTP_INVALID_HEADER_TOKEN);
        return;
    }

    char c = HEADER_CONVERT_TOKENS[static_cast<unsigned int>(ch)];
    if (c == 0) {
        UpdateError(HTTP_INVALID_HEADER_TOKEN);
        return;
    }

    ParseHeadersFlags(c);
}

void HttpParser::ParseHeadersFieldEnd(char ch)
{
    if (ch == SPACE || ch == HT) {
        return;
    }

    UpdateStatus(S_HEADERS_VALUE_START);
    ParseHeadersValueStart(ch);
}

void HttpParser::ParseHeadersValueStart(char ch)
{
    UpdateStatus(S_HEADERS_VALUE);
    UpdateBuffer();
    ParseHeadersValue(ch);
}

void HttpParser::ParseHeadersValueContent(char ch)
{
    if (contentLength == ULLONG_MAX) {
        contentLength = 0;
    }

    if (ch == SPACE) {
        return;
    }

    if (ch < '0' || ch > '9' || contentLength > MAX_HTTP_CONTENT_LENGTH) {
        UpdateError(HTTP_INVALID_CONTENT_LENGTH);
        return;
    }

    UpdateContentLength((contentLength * DEC_CONVERT) + (ch - '0'));
}

void HttpParser::CheckHeadersValueAndUpdate(char ch)
{
    const char *valueStr = g_valueStrings[static_cast<unsigned int>(valueStatus)];
    unsigned int checkPoint = waitStr.size() > 0 ? ((waitStr.size() - 1) + index) : (index - 1);
    if (checkPoint >= strlen(valueStr)) {
        UpdateValueStatus(VS_GENERAL);
        return;
    }
    if (ch == valueStr[checkPoint]) {
        return;
    }

    UpdateValueStatus(VS_GENERAL);
}

void HttpParser::ParseHeadersValueEncoding(char ch)
{
    if (index == 1) {
        UpdateValueStatus(VS_CHUNKED);
    } else if (valueStatus == VS_GENERAL) {
        return;
    }

    CheckHeadersValueAndUpdate(ch);
}

void HttpParser::ParseHeadersValueClose(char ch)
{
    if (index == 1) {
        if (ch == 'c') {
            UpdateValueStatus(VS_CLOSE);
        } else if (ch == 'k') {
            UpdateValueStatus(VS_KEEP_ALIVE);
        } else if (ch == 'u') {
            UpdateValueStatus(VS_UPGRADE);
        }
    } else if (valueStatus == VS_GENERAL) {
        return;
    }

    CheckHeadersValueAndUpdate(ch);
}

void HttpParser::ParseHeadersValue(char ch)
{
    if (ch == CR) {
        UpdateStatus(S_HEADERS_VALUE_END);
        return;
    }

    if (ch == LF) {
        UpdateStatus(S_HEADERS_VALUE_END);
        ParseHeadersValueEnd(ch);
        return;
    }

    if (!(ch == HT || (static_cast<unsigned char>(ch) > UNIT_SEPARATOR_ASCII_INDEX &&
                       static_cast<unsigned char>(ch) != DEL_ASCII_INDEX))) {
        UpdateError(HTTP_INVALID_VALUE_TOKEN);
        return;
    }

    UpdateIndex();

    if (headerStatus == HS_CONTENT_LENGTH) {
        ParseHeadersValueContent(ch);
    } else if (headerStatus == HS_TRANSFER_ENCODIG) {
        ParseHeadersValueEncoding(ch);
    } else if (headerStatus == HS_CONNECTION || headerStatus == HS_PROXY_CONNECTION || headerStatus == HS_UPGRADE) {
        ParseHeadersValueClose(ch);
    }
}

void HttpParser::ParseHeadersValueEnd(char ch)
{
    std::string headValue = waitStr;
    (void)headValue.append(bufPre, index);
    (void)headers[key].append(headValue);
    HandleHeaderValue(bufPre, index);
    std::string().swap(waitStr);
    waitStr.clear();
    UpdateBuffer();
    UpdateFlagsByValueStatus();
    UpdateStatus(S_HEADERS_FIELD_START);

    if (ch != LF) {
        ParseHeadersFieldStart(ch);
    }
}

void HttpParser::ParseHeadersEnd(char ch)
{
    if (ch != LF) {
        UpdateError(HTTP_LF_EXPECTED);
        return;
    }

    UpdateStatus(S_BODY_START);

    UpdateKeepAlive();

    if (HandleHeadersComplete() != 0) {
        UpdateFlags(F_SKIPBODY);
    }

    bool tUpgrade =
        ((flags & (static_cast<unsigned int>(F_UPGRADE) | static_cast<unsigned int>(F_CONNECTION_UPGRADE))) ==
         (static_cast<unsigned int>(F_UPGRADE) | static_cast<unsigned int>(F_CONNECTION_UPGRADE)));
    bool hasBody =
        (flags & static_cast<unsigned int>(F_CHUNKED)) || (contentLength > 0 && contentLength != ULLONG_MAX);
    if (tUpgrade && ((flags & static_cast<unsigned int>(F_SKIPBODY)) || !hasBody)) {
        UpdateStatusToNewMessage();
        (void)HandleMessageComplete();
    }

    if (flags & static_cast<unsigned int>(F_SKIPBODY)) {
        UpdateStatusToNewMessage();
        (void)HandleMessageComplete();
    } else if (flags & static_cast<unsigned int>(F_CHUNKED)) {
        UpdateStatus(S_BODY_START);
    } else {
        if (contentLength == 0) {
            UpdateStatusToNewMessage();
            (void)HandleMessageComplete();
        } else if (contentLength != ULLONG_MAX) {
            UpdateStatus(S_BODY_START);
        } else {
            if (!MessageNeedsEof()) {
                /* Assume content-length 0 - read the next */
                UpdateStatusToNewMessage();
                (void)HandleMessageComplete();
            } else {
                UpdateStatus(S_BODY_IDENTITY_EOF);
            }
        }
    }

    // clear request.headers and response.headers
    UpdateNumOfHeadersToZero();
    HeaderMap().swap(headers);
    headers.clear();
}

void HttpParser::ParseBodyStart(char ch)
{
    UpdateBuffer();
    UpdateStatus(S_BODY);
    ParseBody(ch);
}

void HttpParser::ParseBodyCheck(char ch)
{
    if (ch != LF) {
        UpdateError(HTTP_LF_EXPECTED);
        return;
    }

    ParseBodyStart(ch);
}

void HttpParser::ParseBody(char ch)
{
    if (flags & static_cast<unsigned int>(F_CHUNKED)) {
        ParseBodyChunked(ch);
    } else if (contentLength > 0 && contentLength != ULLONG_MAX) {
        ParseBodyGeneral(ch);
    } else if (flags & static_cast<unsigned int>(F_UPGRADE)) {
        ParseBodyUpgrade(ch);
    } else {
        ParseBodyOthers(ch);
    }
}

void HttpParser::ParseBodyStringStart(char ch)
{
    if (ch != LF) {
        UpdateError(HTTP_LF_EXPECTED);
        return;
    }

    UpdateStatus(S_BODY_STRING_CHECK);
}

void HttpParser::ParseBodyStringCheck(char ch)
{
    if (contentLength == 0) {
        UpdateBuffer();
        UpdateStatus(S_HEADERS_FIELD_START);
        UpdateFlagsToZero();
        ParseHeadersFieldStart(ch);
        return;
    }

    UpdateBuffer();
    UpdateStatus(S_BODY_STRING);
    ParseBodyString(ch);
}

void HttpParser::ParseBodyString(char ch)
{
    UpdateIndex();

    if (contentLength == ULLONG_MAX) {
        if (isLongChunked) {
            UpdateBuffer();
        }
        if (ch == CR || ch == LF) {
            return;
        }

        UpdateStatus(S_BODY_START);
        ParseBodyChunked(ch);
        return;
    }

    if (waitStr.size() + index == contentLength) {
        HandleBody(bufPre, index);
        std::string().swap(waitStr);
        waitStr.clear();
        UpdateContentLengthToMax();
        UpdateBuffer();
    }
}

void HttpParser::ParseBodyGeneral(char)
{
    UpdateIndex();

    if (contentLength == ULLONG_MAX) {
        return;
    }

    if (waitStr.size() + index == contentLength) {
        HandleBody(bufPre, index);
        std::string().swap(waitStr);
        waitStr.clear();
        UpdateContentLengthToMax();

        UpdateStatusToNewMessage();

        (void)HandleMessageComplete();

        UpdateBuffer();
        return;
    }
}

void HttpParser::ParseBodyChunked(char ch)
{
    if (ch == CR) {
        UpdateStatus(S_BODY_STRING_START);
        return;
    }

    if (ch == LF) {
        UpdateStatus(S_BODY_STRING_CHECK);
        return;
    }

    if (contentLength == ULLONG_MAX) {
        contentLength = 0;
    }

    if (ch == SPACE) {
        return;
    }

    if (ch == ';' || ch == ' ') {
        UpdateStatus(S_BODY_IGNORE);
        return;
    }

    unsigned int arrNum = (unsigned)static_cast<int>(ch);
    if (arrNum >= MAX_CONVERT_INDEX) {
        UpdateError(HTTP_INVALID_CHAR);
        return;
    }
    int convertValue = HEX_CONVERT_TOKENS[arrNum];
    if (convertValue < 0 || contentLength > MAX_HTTP_CHUNK_LENGTH) {
        UpdateError(HTTP_INVALID_CHUNK_SIZE);
        return;
    }

    UpdateContentLength((contentLength * HEX_CONVERT) + convertValue);
}

void HttpParser::ParseBodyUpgrade(char)
{
    if (index == 0) {
        UpdateBuffer();
    }

    UpdateIndex();
}

void HttpParser::ParseBodyOthers(char)
{
    if (type != HTTP_RESPONSE) {
        return;
    }

    UpdateIndex();
}

void HttpParser::ParseBodyIgnore(char ch)
{
    if (ch == CR || ch == LF) {
        UpdateStatus(S_BODY_START);
        ParseBodyChunked(ch);
        return;
    }
}

void HttpParser::ParseBodyIdentityEOF(char ch)
{
    if (type != HTTP_RESPONSE) {
        return;
    }

    if ((index == 0) && (ch == CR || ch == LF)) {
        return;
    }

    if (index == 0) {
        UpdateBuffer();
    }

    UpdateIndex();
}

void HttpParser::ParseResponseStart(char ch)
{
    UpdateStatus(S_HTTP_VERSION_START);
    UpdateBuffer();
    HandleMessageBegin();
    ParseHttpVersionStart(ch);
}

void HttpParser::ParseResponseCode(char ch)
{
    if (ch == SPACE) {
        UpdateStatus(S_RESPONSE_STATUS);
        UpdateBuffer();
        return;
    }

    if (ch == CR || ch == LF) {
        ParseResponseStatus(ch);
        return;
    }

    if (ch < '0' || ch > '9' || code > MAX_HTTP_CODE_LENGTH) {
        UpdateError(HTTP_INVALID_RESPONSE_CODE);
        return;
    }

    UpdateCode(code * DEC_CONVERT + (ch - '0'));
}

void HttpParser::ParseResponseStatus(char ch)
{
    if (ch == CR) {
        UpdateStatus(S_HEADERS_START);
        UpdateBuffer();
        return;
    }

    if (ch == LF) {
        UpdateBuffer();
        UpdateStatus(S_HEADERS_FIELD_START);
        return;
    }
}

bool HttpParser::CheckStatus(const char *tData, size_t length)
{
    if (error != HTTP_PARSER_OK) {
        return false;
    }

    if (tData == nullptr) {
        UpdateError(HTTP_INVALID_DATA);
        return false;
    }

    if (length == 0) {
        switch (status) {
            case S_BODY_IDENTITY_EOF:
                UpdateStatusToNewMessage();
                (void)HandleMessageComplete();
                return false;
            default:
                return false;
        }
    }

    return true;
}

size_t HttpParser::Parse(const char *tData, size_t length)
{
    size_t parsed = 0;
    if (CheckStatus(tData, length)) {
        parsed = ParseBranch(tData, length);
    }

    return parsed;
}

size_t HttpParser::ParseBothResReq(const char *tData, size_t length)
{
    const char *bufBgn = tData;
    const char *bufEnd = tData + length;
    const char *tBufCur = bufBgn;

    char ch = NUL;
    size_t parsed = 0;

    for (; tBufCur != bufEnd; ++tBufCur) {
        ch = *tBufCur;
        ParseStart(ch);

        if (type != HTTP_BOTH) {
            parsed += ParseBranch(tBufCur, static_cast<size_t>(bufEnd - tBufCur));
            break;
        }

        ++parsed;
    }

    return parsed;
}

size_t HttpParser::ParseReqOrRes(const char *tData, size_t length, bool isRequest)
{
    const char *bufBgn = tData;
    const char *bufEnd = tData + length;

    bufCur = bufBgn;
    bufPre = bufBgn;

    char ch = NUL;

    for (; bufCur != bufEnd; ++bufCur) {
        ch = *bufCur;

        if (GetErrorCode() != HTTP_PARSER_OK) {
            return static_cast<size_t>(bufCur - bufBgn);
        }
        if (status < static_cast<int>(HTTP_PARSER_STATUS_COUNT)) {
            paurserFuncs[status](*this, ch);
        }
    }

    if (index == 0) {
        return length;
    }

    // body upgrade without end flag
    if ((status == static_cast<int>(S_BODY) && (flags & static_cast<unsigned int>(F_UPGRADE))) ||
        (status == static_cast<int>(S_BODY_IDENTITY_EOF) && !isRequest)) {
        HandleBody(bufPre, index);
        UpdateBuffer();
    } else {
        if ((status == static_cast<int>(S_BODY_STRING) || status == static_cast<int>(S_BODY_STRING_START) ||
             status == static_cast<int>(S_BODY_STRING_CHECK)) && contentLength == ULLONG_MAX) {
            UpdateBuffer();
            return length;
        }
        (void)waitStr.append(bufPre, index);
        UpdateBuffer();
    }

    return length;
}

size_t HttpParser::ParseRequest(const char *tData, size_t length)
{
    return ParseReqOrRes(tData, length, true);
}

size_t HttpParser::ParseResponse(const char *tData, size_t length)
{
    return ParseReqOrRes(tData, length, false);
}

HttpParserStatus HttpParser::GetParserStatus() const
{
    return status;
}

unsigned int HttpParser::GetWaitStrSize() const
{
    return waitStr.length();
}

}    // namespace http
}    // namespace litebus
