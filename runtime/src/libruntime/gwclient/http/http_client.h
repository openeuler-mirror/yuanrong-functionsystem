/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
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

#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "absl/synchronization/mutex.h"
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include "src/libruntime/err_type.h"

namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
namespace beast = boost::beast;
namespace asio = boost::asio;
namespace YR {
namespace Libruntime {
using HttpCallbackFunction = std::function<void(const std::string &, const boost::beast::error_code &, const uint)>;
const http::verb POST = http::verb::post;
const http::verb DELETE = http::verb::delete_;
const http::verb GET = http::verb::get;
const http::verb PUT = http::verb::put;
extern const int DEFAULT_HTTP_VERSION;
extern int g_defaultIdleTime;
extern const uint HTTP_CONNECTION_ERROR_CODE;
extern const char *HTTP_CONNECTION_ERROR_MSG;
struct ConnectionParam {
    std::string ip;
    std::string port;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual ErrorInfo Init(const ConnectionParam &param) = 0;
    virtual void SubmitInvokeRequest(const http::verb &method, const std::string &target,
                                     const std::unordered_map<std::string, std::string> &headers,
                                     const std::string &body, const std::shared_ptr<std::string> requestId,
                                     const HttpCallbackFunction &receiver) = 0;
    virtual void RegisterHeartbeat(const std::string &jobID, int timeout) {};

    virtual bool Available() const
    {
        absl::ReaderMutexLock l(&mu_);
        return !this->isUsed_;
    };

    virtual bool IsActive() const
    {
        auto current = std::chrono::high_resolution_clock::now();
        auto idle = std::chrono::duration_cast<std::chrono::seconds>(current - this->lastActiveTime_).count();
        return isConnectionAlive_ && idle < g_defaultIdleTime;
    };

    virtual bool IsConnActive() const
    {
        absl::ReaderMutexLock l(&mu_);
        auto current = std::chrono::high_resolution_clock::now();
        auto idle = std::chrono::duration_cast<std::chrono::seconds>(current - this->lastActiveTime_).count();
        return isConnectionAlive_ && idle < idleTime_;
    };

    void SetAvailable()
    {
        absl::WriterMutexLock l(&mu_);
        isUsed_ = false;
    }

    virtual ErrorInfo ReInit()
    {
        return ErrorInfo();
    }

    virtual void Cancel() {}
    virtual void Stop() {}
    void SetUnavailable()
    {
        isUsed_ = true;
    }

protected:
    ConnectionParam connParam_;
    HttpCallbackFunction callback_;
    beast::flat_buffer buf_;
    std::shared_ptr<http::response_parser<http::string_body>> resParser_;
    http::request<http::string_body> req_;
    std::atomic<bool> isUsed_{false};
    std::atomic<bool> isConnectionAlive_{false};
    std::chrono::time_point<std::chrono::high_resolution_clock> lastActiveTime_;
    bool retried_{false};
    mutable absl::Mutex mu_;
    int idleTime_{120};
};

inline bool IsResponseSuccessful(const uint statusCode)
{
    const uint SUCCESS_CODE_MIN = 200;
    const uint SUCCESS_CODE_MAX = 299;
    return (statusCode >= SUCCESS_CODE_MIN && statusCode <= SUCCESS_CODE_MAX);
}
}  // namespace Libruntime
}  // namespace YR