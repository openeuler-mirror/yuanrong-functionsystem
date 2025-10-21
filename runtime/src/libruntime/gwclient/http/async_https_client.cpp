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

#include "src/libruntime/gwclient/http/async_https_client.h"
#include "src/utility/logger/logger.h"

namespace YR {
namespace Libruntime {
AsyncHttpsClient::AsyncHttpsClient(const std::shared_ptr<asio::io_context> &ioc,
                                   const std::shared_ptr<asio::ssl::context> &ctx, const std::string &serverName)
    : ioc_(ioc), ctx_(ctx), resolver_(asio::make_strand(*ioc)), serverName_(serverName)
{
}

AsyncHttpsClient::~AsyncHttpsClient()
{
    if (isConnectionAlive_) {
        GracefulExit();
    }
}

void AsyncHttpsClient::SubmitInvokeRequest(const http::verb &method, const std::string &target,
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
    req_.set("HOST", connParam_.ip + ":" + connParam_.port);
    req_.body() = body;
    req_.prepare_payload();
    resParser_ = std::make_shared<http::response_parser<http::string_body>>();
    resParser_->body_limit(std::numeric_limits<std::uint64_t>::max());
    http::async_write(*stream_, req_,
                      beast::bind_front_handler(&AsyncHttpsClient::OnWrite, shared_from_this(), requestId));
}

ErrorInfo AsyncHttpsClient::Init(const ConnectionParam &param)
{
    // A new stream_ must be generated for the reconnection. Otherwise, an error is reported:
    // protocol is shutdown(SSL routines, ssl write internal)
    stream_ = std::make_shared<beast::ssl_stream<beast::tcp_stream>>(asio::make_strand(*ioc_), *ctx_);
    // Set SNI Hostname (hosts need this to handshake successfully)
    YRLOG_INFO("Https init, serverAddr = {}:{}", param.ip, param.port);
    connParam_ = param;
    std::string msg;
    if (!SSL_set_tlsext_host_name(stream_->native_handle(), serverName_.c_str())) {
        YRLOG_ERROR("failed to set servername: {}", serverName_);
        msg = "failed to set servername during initing invoke client, serverName:" + serverName_;
        return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, msg);
    }

    try {
        // sync connect
        auto const results = resolver_.resolve(param.ip, param.port);
        (void)beast::get_lowest_layer(*stream_).connect(results);
    } catch (const std::exception &e) {
        std::stringstream ss;
        ss << "failed to connect to all addresses, target: " << param.ip << ":" << param.port;
        ss << ", exception: " << e.what();
        YRLOG_DEBUG(ss.str());
        return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, ss.str());
    }

    boost::system::error_code ec;
    stream_->handshake(ssl::stream_base::client, ec);
    if (ec) {
        YRLOG_ERROR("handshake error: {}", ec.message().c_str());
        msg = "failed to handshake error during initing invoke client, err:" + serverName_;
        return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, msg);
    }
    lastActiveTime_ = std::chrono::high_resolution_clock::now();
    isConnectionAlive_ = true;
    isUsed_ = false;
    return ErrorInfo();
}

void AsyncHttpsClient::OnWrite(const std::shared_ptr<std::string> requestId, const beast::error_code &ec,
                               std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (!ec) {
        http::async_read(*stream_, this->buf_, *resParser_,
                         beast::bind_front_handler(&AsyncHttpsClient::OnRead, shared_from_this(), requestId));
        return;
    }
    YRLOG_ERROR("requestId {} failed to write, err message: {}, this client disconnect", *requestId,
                ec.message().c_str());
    if (!retried_) {
        YRLOG_DEBUG("requestId {} start to retry once", *requestId);
        retried_ = true;
        if (ReInit().OK()) {
            http::async_write(*stream_, req_,
                              beast::bind_front_handler(&AsyncHttpsClient::OnWrite, shared_from_this(), requestId));
            return;
        }
    }
    if (callback_) {
        callback_(HTTP_CONNECTION_ERROR_MSG, ec, HTTP_CONNECTION_ERROR_CODE);
    }
    isConnectionAlive_ = false;
    isUsed_ = false;
    return;
}

void AsyncHttpsClient::OnRead(const std::shared_ptr<std::string> requestId, const beast::error_code &ec,
                              std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        YRLOG_ERROR("requestId {} failed to read response , err message: {}, this client disconnect", *requestId,
                    ec.message().c_str());
        this->isConnectionAlive_ = false;
    }

    if (callback_) {
        callback_(resParser_->get().body(), ec, resParser_->get().result_int());
    }
    resParser_.reset();
    buf_.clear();
    req_.clear();
    isUsed_ = false;
    lastActiveTime_ = std::chrono::high_resolution_clock::now();
}

void AsyncHttpsClient::GracefulExit() noexcept
{
    boost::system::error_code ec = {};
    if (stream_) {
        stream_->shutdown(ec);
    }
    if (ec) {
        YRLOG_WARN("shutdown fail {}", ec.message().c_str());
    }
    isConnectionAlive_ = false;
}

ErrorInfo AsyncHttpsClient::ReInit()
{
    GracefulExit();
    return Init(connParam_);
}

void AsyncHttpsClient::Stop()
{
    if (stream_) {
        stream_.reset();
    }
}
}  // namespace Libruntime
}  // namespace YR