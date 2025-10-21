/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#include "src/libruntime/gwclient/http/async_http_client.h"
#include "src/utility/logger/logger.h"

namespace YR {
namespace Libruntime {
const int DEFAULT_HTTP_VERSION = 11;
int g_defaultIdleTime = 600;
const uint HTTP_CONNECTION_ERROR_CODE = 999;
const char *HTTP_CONNECTION_ERROR_MSG = "connection error";
AsyncHttpClient::AsyncHttpClient(std::shared_ptr<asio::io_context> ctx)
    : resolver_(asio::make_strand(*ctx)), stream_(asio::make_strand(*ctx))
{
}

AsyncHttpClient::~AsyncHttpClient()
{
    if (isConnectionAlive_) {
        GracefulExit();
    }
}

void AsyncHttpClient::GracefulExit() noexcept
{
    beast::error_code ec;
    stream_.socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    if (ec) {
        YRLOG_WARN("failed to shutdown stream: {}", ec.message().c_str());
    }
    stream_.close();
    isConnectionAlive_ = false;
}

void AsyncHttpClient::SubmitInvokeRequest(const http::verb &method, const std::string &target,
                                          const std::unordered_map<std::string, std::string> &headers,
                                          const std::string &body, const std::shared_ptr<std::string> requestId,
                                          const HttpCallbackFunction &receiver)
{
    retried_ = false;
    callback_ = receiver;
    req_ = {method, target, DEFAULT_HTTP_VERSION};
    for (auto &iter : headers) {
        req_.set(iter.first, iter.second);
    }
    req_.set("HOST", this->connParam_.ip + ":" + this->connParam_.port);
    req_.body() = body;
    // set body size in headers
    req_.prepare_payload();
    resParser_ = std::make_shared<http::response_parser<http::string_body>>();
    resParser_->body_limit(std::numeric_limits<std::uint64_t>::max());
    http::async_write(stream_, req_,
                      beast::bind_front_handler(&AsyncHttpClient::OnWrite, shared_from_this(), requestId));
}

ErrorInfo AsyncHttpClient::Init(const ConnectionParam &param)
{
    YRLOG_DEBUG("Http init, serverAddr = {}:{}", param.ip, param.port);
    connParam_ = param;
    // sync connection
    try {
        auto const resolveRes = resolver_.resolve(param.ip, param.port);
        stream_.connect(resolveRes);
    } catch (const std::exception &e) {
        std::stringstream ss;
        ss << "failed to connect to all addresses, target: ";
        ss << param.ip;
        ss << ":";
        ss << param.port;
        ss << ", exception: ";
        ss << e.what();
        YRLOG_DEBUG(ss.str());
        return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, ss.str());
    }
    lastActiveTime_ = std::chrono::high_resolution_clock::now();
    isConnectionAlive_ = true;
    isUsed_ = false;
    return ErrorInfo();
}

void AsyncHttpClient::OnRead(const std::shared_ptr<std::string> requestId, const beast::error_code &ec,
                             std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        YRLOG_ERROR("requestId {} failed to read response , err message: {}, this client disconnect", *requestId,
                    ec.message().c_str());
        isConnectionAlive_ = false;
    }
    if (callback_) {
        callback_(resParser_->get().body(), ec, resParser_->get().result_int());
    }
    resParser_.reset();
    req_.clear();
    buf_.clear();
    isUsed_ = false;
    lastActiveTime_ = std::chrono::high_resolution_clock::now();
}

ErrorInfo AsyncHttpClient::ReInit()
{
    GracefulExit();
    isConnectionAlive_ = false;
    return Init(connParam_);
}

void AsyncHttpClient::OnWrite(const std::shared_ptr<std::string> requestId, beast::error_code &ec,
                              std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        YRLOG_ERROR("requestId {} failed to write, err message: {}, this client disconnect", *requestId,
                    ec.message().c_str());
        if (!retried_) {
            YRLOG_DEBUG("requestId {} start to retry once", *requestId);
            retried_ = true;
            if (ReInit().OK()) {
                http::async_write(stream_, req_,
                                  beast::bind_front_handler(&AsyncHttpClient::OnWrite, shared_from_this(), requestId));
                return;
            }
        }
        isConnectionAlive_ = false;
        if (callback_) {
            callback_(HTTP_CONNECTION_ERROR_MSG, ec, HTTP_CONNECTION_ERROR_CODE);
        }
        isUsed_ = false;
        return;
    }
    http::async_read(stream_, buf_, *resParser_,
                     beast::bind_front_handler(&AsyncHttpClient::OnRead, shared_from_this(), requestId));
}

void AsyncHttpClient::Cancel()
{
    boost::system::error_code ec;
    stream_.socket().cancel(ec);
}

void AsyncHttpClient::Stop()
{
    if (isConnectionAlive_) {
        GracefulExit();
    }
}
}  // namespace Libruntime
}  // namespace YR