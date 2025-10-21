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

#ifndef __HTTP_HPP__
#define __HTTP_HPP__

#include <list>
#include <map>
#include <memory>
#include <strings.h>
#include <string>
#include <unordered_map>

#include <netinet/in.h>

#include "actor/buslog.hpp"
#include "actor/msg.hpp"
#include "async/future.hpp"
#include "async/option.hpp"
#include "async/try.hpp"

namespace litebus {

constexpr uint32_t RECV_BUFFER_SIZE = 8192;
constexpr int MAX_CON_NUM = 10000;

namespace http {

const std::string HTTP_SCHEME = "http";
const std::string HTTPS_SCHEME = "https";

struct CaseInsensitiveCmpFun {
    bool operator()(const std::string &left, const std::string &right) const
    {
        return strcasecmp(left.c_str(), right.c_str()) < 0;
    }
};

using HeaderMap = std::map<std::string, std::string, CaseInsensitiveCmpFun>;

struct URL {
    URL() = default;

    URL(const std::string &tScheme, const std::string &tIp, uint16_t tPort = 80, const std::string &tPath = "/",
        const std::unordered_map<std::string, std::string> &tQuery = (std::unordered_map<std::string, std::string>()),
        const std::unordered_map<std::string, std::vector<std::string>> &trawQuery =
            (std::unordered_map<std::string, std::vector<std::string>>()))
        : scheme(tScheme), ip(tIp), port(tPort), path(tPath), query(tQuery), rawQuery(trawQuery) {
    }

    // protocol type: http/https
    Option<std::string> scheme;
    Option<std::string> ip;
    Option<uint16_t> port;
    std::string path;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::vector<std::string>> rawQuery;

    static Try<URL> Decode(const std::string &url, bool domainDecode = true);
};

inline std::ostream &operator<<(std::ostream &os, const URL &url)
{
    if (url.scheme.IsSome()) {
        os << url.scheme.Get();
    }
    os << "://";

    if (url.ip.IsSome()) {
        os << url.ip.Get();
    }
    os << ":";

    if (url.port.IsSome()) {
        os << std::to_string(url.port.Get());
    }
    os << url.path;

    if (!url.query.empty()) {
        std::list<std::string> queryList;
        for (const auto &queryItem : url.query) {
            queryList.push_back(queryItem.first + "=" + queryItem.second);
        }

        auto it = queryList.begin();
        if (queryList.size() >= 1) {
            os << "?" << *it;
        }
        ++it;
        for (; it != queryList.end(); ++it) {
            os << "&" << *it;
        }
    }

    return os;
}

inline bool CompareUrlDomain(const URL &url1, const URL &url2)
{
    return (url1.scheme.IsSome() && url2.scheme.IsSome() && url1.scheme.Get() == url2.scheme.Get()) &&
           (url1.ip.IsSome() && url2.ip.IsSome() && url1.ip.Get() == url2.ip.Get()) &&
           (url1.port.IsSome() && url2.port.IsSome() && url1.port.Get() == url2.port.Get());
}

struct Request {
public:
    Request() = default;
    Request(const std::string &tMethod, const bool &tKeepAlive, const URL &tUrl,
            const HeaderMap &tHeaders = HeaderMap(), const std::string &tBody = "",
            const Option<std::string> &tClient = None(), const Option<uint64_t> &tTimeout = None())
        : method(tMethod),
          keepAlive(tKeepAlive),
          url(tUrl),
          headers(tHeaders),
          body(tBody),
          client(tClient),
          timeout(tTimeout)
    {
    }
    std::string method;
    bool keepAlive = false;    // HTTP1.1 should be true
    URL url;

    HeaderMap headers;
    std::string body;
    Option<std::string> client;
    Option<uint64_t> timeout;
    Option<std::string> credential;
};

enum ResponseCode {
    CONTINUE = 100,
    SWITCHING_PROTOCOLS = 101,
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NON_AUTHORITATIVE_INFORMATION = 203,
    NO_CONTENT = 204,
    RESET_CONTENT = 205,
    PARTIAL_CONTENT = 206,
    MULTIPLE_CHOICES = 300,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    SEE_OTHER = 303,
    NOT_MODIFIED = 304,
    USE_PROXY = 305,
    TEMPORARY_REDIRECT = 307,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    PAYMENT_REQUIRED = 402,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    NOT_ACCEPTABLE = 406,
    PROXY_AUTHENTICATION_REQUIRED = 407,
    REQUEST_TIMEOUT = 408,
    CONFLICT = 409,
    GONE = 410,
    LENGTH_REQUIRED = 411,
    PRECONDITION_FAILED = 412,
    REQUEST_ENTITY_TOO_LARGE = 413,
    REQUEST_URI_TOO_LARGE = 414,
    UNSUPPORTED_MEDIA_TYPE = 415,
    REQUESTED_RANGE_NOT_SATISFIABLE = 416,
    EXPECTATION_FAILED = 417,
    UNPROCESSABLE_ENTITY = 422,
    PRECONDITION_REQUIRED = 428,
    TOO_MANY_REQUESTS = 429,
    REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
    HTTP_VERSION_NOT_SUPPORTED = 505,
    NETWORK_AUTHENTICATION_REQUIRED = 511,
};

enum ResponseBodyType { TEXT = 0, JSON = 1 };

struct Response {
public:
    Response() : retCode(OK)
    {
    }
    virtual ~Response()
    {
    }
    explicit Response(ResponseCode code) : retCode(code)
    {
    }
    static std::string GetStatusDescribe(const ResponseCode &code)
    {
        static std::unordered_map<ResponseCode, std::string> describeMap = {
            {CONTINUE, "Continue"},
            {SWITCHING_PROTOCOLS, "Switching Protocols"},
            {OK, "OK"},
            {CREATED, "Created"},
            {ACCEPTED, "Accepted"},
            {NON_AUTHORITATIVE_INFORMATION, "Non-Authoritative Information"},
            {NO_CONTENT, "No Content"},
            {RESET_CONTENT, "Reset Content"},
            {PARTIAL_CONTENT, "Partial Content"},
            {MULTIPLE_CHOICES, "Multiple Choices"},
            {MOVED_PERMANENTLY, "Moved Permanently"},
            {FOUND, "Found"},
            {SEE_OTHER, "See Other"},
            {NOT_MODIFIED, "Not Modified"},
            {USE_PROXY, "Use Proxy"},
            {TEMPORARY_REDIRECT, "Temporary Redirect"},
            {BAD_REQUEST, "Bad Request"},
            {UNAUTHORIZED, "Unauthorized"},
            {PAYMENT_REQUIRED, "Payment Required"},
            {FORBIDDEN, "Forbidden"},
            {NOT_FOUND, "Not Found"},
            {METHOD_NOT_ALLOWED, "Method Not Allowed"},
            {NOT_ACCEPTABLE, "Not Acceptable"},
            {PROXY_AUTHENTICATION_REQUIRED, "Proxy Authentication Required"},
            {REQUEST_TIMEOUT, "Request Time-out"},
            {CONFLICT, "Conflict"},
            {GONE, "Gone"},
            {LENGTH_REQUIRED, "Length Required"},
            {PRECONDITION_FAILED, "Precondition Failed"},
            {REQUEST_ENTITY_TOO_LARGE, "Request Entity Too Large"},
            {REQUEST_URI_TOO_LARGE, "Request-URI Too Large"},
            {UNSUPPORTED_MEDIA_TYPE, "Unsupported Media Type"},
            {REQUESTED_RANGE_NOT_SATISFIABLE, "Requested range not satisfiable"},
            {EXPECTATION_FAILED, "Expectation failed"},
            {PRECONDITION_REQUIRED, "Precondition Required"},
            {TOO_MANY_REQUESTS, "Too many requests"},
            {REQUEST_HEADER_FIELDS_TOO_LARGE, "Requests Header Fields Too Large"},
            {INTERNAL_SERVER_ERROR, "Internal Server Error"},
            {NOT_IMPLEMENTED, "Not Implemented"},
            {BAD_GATEWAY, "Bad Gateway"},
            {SERVICE_UNAVAILABLE, "Service Unavailable"},
            {GATEWAY_TIMEOUT, "Gateway Time-out"},
            {HTTP_VERSION_NOT_SUPPORTED, "HTTP Version not supported"},
            {NETWORK_AUTHENTICATION_REQUIRED, "Network Authentication Required"},
        };
        if (describeMap.find(code) != describeMap.end()) {
            return describeMap[code];
        }
        return "Unknown";
    }

    explicit Response(ResponseCode code, const std::string &b, ResponseBodyType type = TEXT) : retCode(code), body(b)
    {
        switch (type) {
            case TEXT: {
                headers["Content-Type"] = "text/plain";
                break;
            }
            case JSON: {
                headers["Content-Type"] = "application/json";
                break;
            }
            default:
                BUSLOG_WARN("Unrecognized Content-Type.");
        }
    }
    ResponseCode retCode;
    HeaderMap headers;
    std::string body;
    // NOTE: add more options in future
};
using ResponseCallback = std::function<void(const http::Response *)>;

struct Ok : public Response {
    Ok() : Response(ResponseCode::OK)
    {
    }

    explicit Ok(const std::string &body, ResponseBodyType type = TEXT) : Response(ResponseCode::OK, body, type)
    {
    }
};

struct Accepted : public Response {
    Accepted() : Response(ResponseCode::ACCEPTED)
    {
    }

    explicit Accepted(const std::string &body) : Response(ResponseCode::ACCEPTED, body)
    {
    }
};

struct BadRequest : public Response {
    BadRequest() : Response(ResponseCode::BAD_REQUEST)
    {
    }

    explicit BadRequest(const std::string &body) : Response(ResponseCode::BAD_REQUEST, body)
    {
    }
};

struct NotFound : public Response {
    NotFound() : Response(ResponseCode::NOT_FOUND)
    {
    }

    explicit NotFound(const std::string &body) : Response(ResponseCode::NOT_FOUND, body)
    {
    }
};

class HttpMessage : public MessageBase {
public:
    explicit HttpMessage(const Request &req, std::unique_ptr<Promise<Response>> respPromise, const AID &from,
                         const AID &to, const std::string &name, MessageBase::Type type)
        : MessageBase(from, to, name, type), request(req), responsePromise(std::move(respPromise))
    {
    }

    ~HttpMessage() override
    {
    }

    const Request &GetRequest() const
    {
        return request;
    }

    std::unique_ptr<Promise<Response>> GetResponsePromise()
    {
        return std::move(responsePromise);
    }

private:
    Request request;
    std::unique_ptr<Promise<Response>> responsePromise;
};

Try<std::string> Decode(const std::string &s);

namespace query {
using QueriesTry = Try<std::pair<std::unordered_map<std::string, std::string>,
                                 std::unordered_map<std::string, std::vector<std::string>>>>;
QueriesTry Decode(const std::string &query);
}    // namespace query
}    // namespace http
};    // namespace litebus

#endif
