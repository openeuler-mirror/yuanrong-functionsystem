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

#include <boost/beast/ssl.hpp>
#include <chrono>

#include "src/dto/config.h"
#include "src/libruntime/gwclient/http/async_http_client.h"
#include "src/libruntime/gwclient/http/async_https_client.h"
#include "src/libruntime/gwclient/http/client_manager.h"
#include "src/utility/logger/logger.h"
#include "src/utility/notification_utility.h"

namespace {
const uint32_t MAX_CONN_SIZE = 10000;
const int RETRY_TIME = 3;
const int INTERVAL_TIME = 2;
}  // namespace

namespace ssl = boost::asio::ssl;
namespace YR {
namespace Libruntime {
using YR::utility::NotificationUtility;
ClientManager::ClientManager(const std::shared_ptr<LibruntimeConfig> &libruntimeConfig) : librtCfg(libruntimeConfig)
{
    this->ioc = std::make_shared<boost::asio::io_context>();
    this->work = std::make_unique<asio::io_context::work>(*ioc);
    this->maxIocThread = libruntimeConfig->httpIocThreadsNum;
    this->enableMTLS = libruntimeConfig->enableMTLS;
}

ClientManager::~ClientManager()
{
    this->work.reset();
    ioc->stop();
    for (auto client : clients) {
        client->Stop();
    }
    for (uint32_t i = 0; i < asyncRunners.size(); i++) {
        this->asyncRunners[i]->join();
    }
}

ErrorInfo ClientManager::InitCtxAndIocThread()
{
    ErrorInfo err;
    if (enableMTLS) {
        try {
            auto ctx = std::make_shared<ssl::context>(ssl::context::tlsv12_client);
            ctx->set_options(ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                             ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
            ctx->set_verify_mode(ssl::verify_peer);
            ctx->load_verify_file(librtCfg->verifyFilePath);
            ctx->use_certificate_chain_file(librtCfg->certificateFilePath);
            ctx->use_private_key_file(librtCfg->privateKeyPath, ssl::context::pem);
            for (uint32_t i = 0; i < MAX_CONN_SIZE; i++) {
                this->clients.emplace_back(std::make_shared<AsyncHttpsClient>(this->ioc, ctx, librtCfg->serverName));
            }
        } catch (const std::exception &e) {
            YRLOG_ERROR("caught exception when init context : {}", e.what());
            err.SetErrCodeAndMsg(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, e.what());
            return err;
        } catch (...) {
            YRLOG_ERROR("caught unknown exception when init context");
            err.SetErrCodeAndMsg(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME,
                                 "caught unknown exception when init context");
            return err;
        }
    } else {
        for (uint32_t i = 0; i < MAX_CONN_SIZE; i++) {
            this->clients.emplace_back(std::make_shared<AsyncHttpClient>(this->ioc));
        }
    }

    for (uint32_t i = 0; i < maxIocThread; i++) {
        asyncRunners.push_back(std::make_unique<std::thread>([&] { this->ioc->run(); }));
        std::string name = "yr_client_io_" + std::to_string(i);
        pthread_setname_np(this->asyncRunners[i]->native_handle(), name.c_str());
    }
    return err;
}

ErrorInfo ClientManager::Init(const ConnectionParam &param)
{
    ErrorInfo error = InitCtxAndIocThread();
    if (!error.OK()) {
        return error;
    }
    this->connParam = param;
    connectedClientsCnt = YR::Libruntime::Config::Instance().YR_HTTP_CONNECTION_NUM();
    YRLOG_INFO("http initial connection num {}", connectedClientsCnt);
    if (connectedClientsCnt > MAX_CONN_SIZE) {
        YRLOG_WARN("Requested connections exceed maximum allowed ({}). Clamping to maximum.", MAX_CONN_SIZE);
        connectedClientsCnt = MAX_CONN_SIZE;
    }
    for (uint32_t i = 0; i < connectedClientsCnt; i++) {
        for (int j = 0; j < RETRY_TIME; j++) {
            error = clients[i]->Init(param);
            if (error.OK()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(INTERVAL_TIME));
        }
        if (!error.OK()) {
            return ErrorInfo(ErrorCode::ERR_INIT_CONNECTION_FAILED, ModuleCode::RUNTIME, error.Msg());
        }
    }
    return error;
}

void ClientManager::SubmitInvokeRequest(const http::verb &method, const std::string &target,
                                        const std::unordered_map<std::string, std::string> &headers,
                                        const std::string &body, const std::shared_ptr<std::string> requestId,
                                        const HttpCallbackFunction &receiver)
{
    std::unique_lock<std::mutex> lk(connMtx);
    if (clients.empty()) {
        YRLOG_ERROR("Clients are not initialized.");
        receiver(HTTP_CONNECTION_ERROR_MSG, boost::asio::error::make_error_code(boost::asio::error::connection_reset),
                 HTTP_CONNECTION_ERROR_CODE);
        return;
    }
    for (;;) {
        for (uint32_t i = 0; i < connectedClientsCnt; i++) {
            if (!this->clients[i]->Available()) {
                continue;
            }
            YRLOG_DEBUG("httpclient {} is available, will use this client", i);
            // while the connection idletime exceed setup timeout, the server may close the connection
            // in this situation, client should try to reconnect
            if (!this->clients[i]->IsActive()) {
                YRLOG_DEBUG("httpclient {} is not active, reinit now", i);
                auto err = this->clients[i]->ReInit();
                if (!err.OK()) {
                    YRLOG_DEBUG("httpclient {} is reInit failed, err: {}", i, err.CodeAndMsg());
                    receiver(HTTP_CONNECTION_ERROR_MSG,
                             boost::asio::error::make_error_code(boost::asio::error::connection_reset),
                             HTTP_CONNECTION_ERROR_CODE);
                    return;
                }
            }
            this->clients[i]->SetUnavailable();
            lk.unlock();
            this->clients[i]->SubmitInvokeRequest(method, target, headers, body, requestId, receiver);
            return;
        }

        if (connectedClientsCnt < MAX_CONN_SIZE) {
            connectedClientsCnt++;
            this->clients[connectedClientsCnt - 1]->Init(this->connParam);
            this->clients[connectedClientsCnt - 1]->SetUnavailable();
            this->clients[connectedClientsCnt - 1]->SubmitInvokeRequest(method, target, headers, body, requestId,
                                                                        receiver);
            return;
        }
        std::this_thread::yield();
    }
}
}  // namespace Libruntime
}  // namespace YR
