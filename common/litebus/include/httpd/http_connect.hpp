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

#ifndef __LITEBUS_HTTP_CONNECT_H__
#define __LITEBUS_HTTP_CONNECT_H__

#include <string>
#include <set>
#include "httpd/http.hpp"

namespace litebus {

namespace http {

// correspond with errno
enum HttpErrorCode {
    CONNECTION_MEET_MAXIMUN = 48,
    CONNECTION_RESET_BY_PEER = 104,
    CONNECTION_REFUSED = 111,
    CONNECTION_TIMEOUT = 110,
    INVALID_REQUEST = 53,
    MEMORY_ALLOCATION_FAILED = 12,
    CANNOT_SEND_AFTER_SHUTDOWN = 108
};

const std::set<std::string> ALLOW_METHOD = { "DELETE", "GET", "POST", "PUT", "PATCH" };

class HttpConnect {
public:
    HttpConnect(){};

    /**
     * Forward declaration, 'HttpConenct' is reference-counted, here we use connection to do
     * garbage collection
     */
    struct HttpConnection;

    HttpConnect(const int conSeq, const URL &url);

    virtual ~HttpConnect()
    {
    }

    HttpConnect &operator=(const HttpConnect &that)
    {
        if (&that != this) {
            connection = that.connection;
        }
        return *this;
    }

    HttpConnect(const HttpConnect &con) : connection(con.connection)
    {
    }

    bool operator==(const HttpConnect &con) const
    {
        return connection == con.connection;
    }
    bool operator!=(const HttpConnect &con) const
    {
        return !(*this == con);
    }

    /**
     * A callback called when a new connection has been established
     */
    static Future<HttpConnect> ConnectEstablishedCallback(const int &conSeq, const URL &url);

    /**
     * A callback called when a new connection has been established to launch a request
     */
    static Future<Response> ConnectAndLaunchReqCallback(const int conSeq, const Request &request,
                                                        const bool timeOutFlag = true);

    /**
     * A callback called when a new connection has been closed
     */
    static void ConnectClosedCallback(const int conSeq, const int errCode);

    /**
     * A callback called when responses has been decoded
     */
    static void ResponseCompletedCallback(const int conSeq, Response *response);

    /**
     * Send a http request to server. Note that if the request has a header with 'Connection: close',
     * it means this connection will close after the response received
     */
    Future<Response> LaunchRequest(const Request &request, const bool &timeOutFlag = false);

    /**
     * Close this connection
     */
    Future<bool> Disconnect() const;

    /**
     * future should be ready when Disconnect() is called
     */
    Future<bool> Disconnected() const;

private:
    /**
     * Friend function declaration
     */
    friend Future<HttpConnect> Connect(const URL &, litebus::Option<std::string>);

    std::shared_ptr<HttpConnection> connection;
};

/**
 * creat a link to the specified url, we can use HttpConnect to launch
 * 'keep-alive' request as much as possiable
 */
Future<HttpConnect> Connect(const URL &url, litebus::Option<std::string> credencial = litebus::None());

/*
 * Send http request with method 'POST'
 */
Future<Response> Post(const URL &url, const Option<std::unordered_map<std::string, std::string>> &headers,
                      const Option<std::string> &body, const Option<std::string> &contentType,
                      const Option<uint64_t> &reqTimeout = None());

/**
 * Send http request with method 'GET'
 */
Future<Response> Get(const URL &url, const Option<std::unordered_map<std::string, std::string>> &headers,
                     const Option<uint64_t> &reqTimeout = None());

/**
 * Refactor other request(such as 'POST/GET/DELETE/PUT/PATCH') by specifying parameter in request
 */
Future<Response> LaunchRequest(const Request &request);

Future<Response> LaunchRequest(const Request &request, const ResponseCallback &responseCallback);

/**
 * Check request method, only 'POST/GET/DELETE/PUT/PATCH' are accepted
 */
bool CheckReqType(const std::string &method);

/**
 * Check request url(include scheme/ip/port)
 */
bool CheckReqUrl(const URL &url);

std::string GetHttpError(int32_t httpErrCode);

void SetHttpRequestTimeOut(uint64_t duration);

}    // namespace http
}    // namespace litebus

#endif
