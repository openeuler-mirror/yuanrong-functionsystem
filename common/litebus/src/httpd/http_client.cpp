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

#include <list>
#include <memory>
#include <string>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "evloop/evloop.hpp"
#include "iomgr/evbufmgr.hpp"
#include "iomgr/linkmgr.hpp"
#include "tcp/tcpmgr.hpp"

#include "tcp/tcp_socket.hpp"
#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"

#include "ssl/ssl_socket.hpp"
#endif

#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "httpd/http_decoder.hpp"
#include "httpd/http_client.hpp"

#include "async/uuid_generator.hpp"
#include "actor/buslog.hpp"
#include "litebus.hpp"
#include "securec.h"

using namespace std;
using namespace litebus;
using litebus::http::HeaderMap;
using litebus::http::OK;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::URL;

namespace litebus {
namespace http {
constexpr auto MAX_RECVRSP_COUNT = 3;

std::map<int, Connection *> HttpClient::g_connections;

namespace clientUtil {

void CloseConnection(int conSeq)
{
    auto conIter = HttpClient::g_connections.find(conSeq);
    if (conIter == HttpClient::g_connections.end()) {
        return;
    }

    Connection *conn = HttpClient::g_connections[conSeq];

    if (conn == nullptr) {
        return;
    }

    BUSLOG_DEBUG("Close connection, conSeq={},fd={},to={}", conn->sequence, conn->fd, conn->to.c_str());
    if (conn->recvEvloop != nullptr) {
        (void)conn->recvEvloop->DelFdEvent(conn->fd);
    }

    if ((!conn->to.empty()) && (conn->recvMsgBase != nullptr)) {
        delete conn->recvMsgBase;
        conn->recvMsgBase = nullptr;
    }

    if (conn->sendTotalLen != 0 && conn->sendMsgBase != nullptr) {
        delete conn->sendMsgBase;
        conn->sendMsgBase = nullptr;
    }

    MessageBase *tmpmsg = nullptr;
    while (!conn->sendQueue.empty()) {
        tmpmsg = conn->sendQueue.front();
        conn->sendQueue.pop();
        delete tmpmsg;
        tmpmsg = nullptr;
    }

    if (conn->socketOperate != nullptr) {
        conn->socketOperate->Close(conn);
        delete conn->socketOperate;
        conn->socketOperate = nullptr;
    }

    // free decoder
    if (conn->decoder != nullptr) {
        ResponseDecoder *responseDecoder = static_cast<ResponseDecoder *>(conn->decoder);
        delete responseDecoder;
        conn->decoder = nullptr;
    }

    if (conn->sendMetrics != nullptr) {
        delete conn->sendMetrics;
        conn->sendMetrics = nullptr;
    }

    // free g_connections
    delete conn;
    conn = nullptr;
    (void)HttpClient::g_connections.erase(conSeq);

    return;
}

}    // namespace clientUtil
HttpClient::HttpClient()
{
}

HttpClient::~HttpClient()
{
    try {
        BUSLOG_DEBUG("Http client is distroying.");

        if (gEvloop != nullptr) {
            gEvloop->Finish();

            delete gEvloop;
            gEvloop = nullptr;
        }
    } catch (...) {
        // Ignore
    }
}

bool HttpClient::Initialize()
{
    gEvloop = new (std::nothrow) EvLoop();
    if (gEvloop == nullptr) {
        BUSLOG_WARN("New evLoop failed.");
        return false;
    }

    bool ok = gEvloop->Init(HTTP_CLIENT_EVLOOP_THREADNAME);
    if (!ok) {
        BUSLOG_WARN("EvLoop init failed.");
        delete gEvloop;
        gEvloop = nullptr;
        return false;
    }
    return true;
}

int HttpClient::RecvRsp(Connection *connection, uint32_t size)
{
    int conSeq = connection->sequence;
    int retval;
    uint32_t avail;

    char *buf = nullptr;
    if (size > 0) {
        buf = new (std::nothrow) char[size];
        BUS_OOM_EXIT(buf);
    } else {
        return -1;
    }
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(buf, size, 0, size);
    retval = connection->socketOperate->Recv(connection, buf, size, avail);
    if (retval < 0) {
        connection->connState = ConnectionState::DISCONNECTING;
    }

    // decode response
    ResponseDecoder *decoder = static_cast<ResponseDecoder *>(connection->decoder);
    bool flgEOF = retval < 0;
    deque<Response *> responses = decoder->Decode(static_cast<const char *>(buf), avail, flgEOF);
    if (!responses.empty()) {
        // handle response
        for (unsigned i = 0; i < responses.size(); i++) {
            HttpConnect::ResponseCompletedCallback(conSeq, responses[i]);
        }
        responses.clear();
    }

    if (decoder->Failed() || connection->connState == ConnectionState::DISCONNECTING) {
        if (decoder->Failed()) {
            BUSLOG_ERROR("Decode error, conSeq:{}, data:{}", conSeq, buf);
        } else {
            BUSLOG_DEBUG("Decode error, conSeq:{}", conSeq);
        }

        if (decoder->IsLongChunked()) {
            auto response = new (std::nothrow) http::Response(litebus::http::SERVICE_UNAVAILABLE);
            BUS_OOM_EXIT(response);
            HttpConnect::ResponseCompletedCallback(conSeq, response);
        }
        HttpConnect::ConnectClosedCallback(conSeq, static_cast<int>(CONNECTION_RESET_BY_PEER));
        connection->connState = ConnectionState::DISCONNECTING;
        delete[] buf;
        return -1;
    }

    delete[] buf;
    return avail;
}

void HttpClient::ReadCallBack(void *context)
{
    if (context == nullptr) {
        BUSLOG_WARN("Ctx is null.");
        return;
    }

    Connection *connection = nullptr;
    connection = static_cast<Connection *>(context);
    int retval;
    int count = 0;
    connection->recvMsgType = ParseType::KHTTP_RSP;

    do {
        retval = RecvRsp(connection, RECV_BUFFER_SIZE);
        if (retval < 0) {
            return;
        }

        retval = connection->socketOperate->Pending(connection);
        if (retval > 0) {
            retval = RecvRsp(connection, (uint32_t)retval);
            if (retval < 0) {
                return;
            }
        }
        ++count;
    } while (retval > 0 && count < MAX_RECVRSP_COUNT);

    return;
}

void HttpClient::EventCallBack(void *context)
{
    Connection *connection = static_cast<Connection *>(context);

    if (connection->connState == ConnectionState::CONNECTED) {
        tcpUtil::ConnectionSend(connection);
    } else if (connection->connState == ConnectionState::DISCONNECTING) {
        BUSLOG_DEBUG("Http eventcallback, disconnected, errno:{}", connection->errCode);
        HttpConnect::ConnectClosedCallback(connection->sequence, connection->errCode);
        clientUtil::CloseConnection(connection->sequence);
    }
}

void HttpClient::WriteCallBack(void *context)
{
    Connection *conn = static_cast<Connection *>(context);
    if (conn->connState == ConnectionState::CONNECTED) {
        tcpUtil::ConnectionSend(conn);
    }
}

int HttpClient::AllocateConnId() const
{
    bool idConflict = true;
    int id = 0;
    for (int i = 0; i < MAX_CON_NUM; i++) {
        id = litebus::localid_generator::GenHttpClientConnId();
        const auto conIter = HttpClient::g_connections.find(id);
        if (conIter == HttpClient::g_connections.end()) {
            idConflict = false;
            break;
        }
    }
    return idConflict ? 0 : id;
}

void HttpClient::WriteMsgToBuf(MessageBase *msg, const Request &request) const
{
    std::ostringstream out;

    // add method
    string pathUrl = request.url.path;
    if (request.url.path.find("/") == 0) {
        pathUrl = request.url.path.substr(1);
    }
    out << request.method << " /" << pathUrl;

    // add query
    if (!request.url.query.empty()) {
        vector<string> queryList;
        for (const auto &queryItem : request.url.query) {
            queryList.push_back(queryItem.first + "=" + queryItem.second);
        }

        if (queryList.size() >= 1) {
            out << "?" << queryList[0];
        }

        for (size_t i = 1; i < queryList.size(); i++) {
            out << "&" << queryList[i];
        }
    }

    out << " HTTP/1.1\r\n";

    // add header
    HeaderMap headerMap = request.headers;

    // tell the server to close or persist connection
    auto conItem = headerMap.find("Connection");
    if (headerMap.end() == conItem) {
        if (!request.keepAlive) {
            headerMap["Connection"] = "close";
        } else {
            headerMap["Connection"] = "keep-alive";
        }
    }

    // NOTE : By default send http request with Content-Length
    headerMap["Content-Length"] = std::to_string(request.body.length());

    // By default add Host in header
    auto HostItem = headerMap.find("Host");
    if (headerMap.end() == HostItem) {
        headerMap["Host"] = request.url.ip.Get();
    }

    for (const auto &headerItem : headerMap) {
        out << headerItem.first << ": " << headerItem.second << "\r\n";
    }

    out << "\r\n";
    out << request.body;

    BUS_OOM_EXIT(msg);
    msg->body = out.str();

    BUSLOG_DEBUG("Encode msg, request url,body size, url:{},size:{}", request.url.path, request.body.size());
    return;
}

Future<bool> HttpClient::LaunchRequest(const litebus::http::Request &request, const int &conSequence)
{
    // Hold a promise for each request
    Promise<bool> promise;
    Future<bool> response = promise.GetFuture();
    MessageBase *msg = new (std::nothrow) MessageBase(MessageBase::Type::KHTTP);
    BUS_OOM_EXIT(msg);

    WriteMsgToBuf(msg, request);

    // Send http request in gEvloop
    (void)gEvloop->AddFuncToEvLoop([promise, msg, conSequence] {
        // check connection
        auto conItem = g_connections.find(conSequence);
        if (g_connections.end() == conItem) {
            BUSLOG_DEBUG("Couldn't find the connection,please create it first, conSeq:{}", conSequence);
            promise.SetFailed(static_cast<int32_t>(CONNECTION_REFUSED));
            auto *ptr = msg;
            delete ptr;
            ptr = nullptr;

            return;
        }

        Connection *connection = g_connections[conSequence];

        // send message
        BUSLOG_DEBUG("Send message on a exist connection, conSeq={},fd={},to={}", connection->sequence, connection->fd,
                     connection->to);
        connection->sendQueue.emplace(msg);

        if (connection->connState == ConnectionState::CONNECTED) {
            tcpUtil::ConnectionSend(connection);
        }

        promise.SetValue(true);

        return;
    });

    return response;
}

Connection *HttpClient::CreateHttpConnection(const std::string &fromUrl, const std::string &toUrl,
                                             const std::string urlScheme,
                                             const litebus::Option<std::string> &credencial,
                                             EvLoop *connLoop)
{
    int id = AllocateConnId();
    if (id == 0) {
        BUSLOG_WARN("Allocate connect id fail");
        return nullptr;
    }

    // create a new connection
    Connection *connection = nullptr;
    connection = new (std::nothrow) Connection();
    if (connection == nullptr) {
        BUSLOG_WARN("Malloc HttpConnection fail");
        return nullptr;
    }

    connection->from = fromUrl;
    connection->to = toUrl;
    connection->recvMsgBase = nullptr;
    connection->sequence = id;
    connection->sendEvloop = connLoop;
    connection->recvEvloop = connLoop;

// Make sure the scheme field is either http or https.
#ifdef SSL_ENABLED
    if (credencial.IsSome()) {
        connection->credencial = credencial.Get();
        BUSLOG_INFO("using HttpConnection credencial {}", connection->credencial);
    }
    if (urlScheme == HTTPS_SCHEME) {
        connection->socketOperate = new (std::nothrow) SSLSocketOperate();
    } else {
        connection->socketOperate = new (std::nothrow) TCPSocketOperate();
    }
#else
    connection->socketOperate = new (std::nothrow) TCPSocketOperate();
#endif

    BUS_OOM_EXIT(connection->socketOperate);
    connection->decoder = new (std::nothrow) ResponseDecoder();
    BUS_OOM_EXIT(connection->decoder);

    BUSLOG_DEBUG("Create a new connection, conSeq:{},fd:{},to:{}", connection->sequence, connection->fd, toUrl);
    return connection;
}

Future<int> HttpClient::Connect(const litebus::http::URL &url, const litebus::Option<std::string> &credencial)
{
    BUSLOG_DEBUG("Make connection, ip:{},port:{}", url.ip.Get(), url.port.Get());
    // Hold a promise for each connection
    Promise<int> promise;
    Future<int> result = promise.GetFuture();

    std::string ip = url.ip.Get();
    uint16_t port = url.port.Get();
    std::string urlScheme = url.scheme.Get();
    string toUrl = ip + ":" + std::to_string(port);
    // NOTE : use litebus http url instead
    string fromUrl = litebus::GetLitebusAddress().ip + std::to_string(litebus::GetLitebusAddress().port);
    // Make connection in gEvloop
    (void)gEvloop->AddFuncToEvLoop([promise, fromUrl, urlScheme, toUrl, credencial, this] {
        const int retCode = 0;
        if (g_connections.size() == MAX_CON_NUM) {
            BUSLOG_WARN("Connection meets the maximum.");
            promise.SetValue(retCode - static_cast<int>(CONNECTION_MEET_MAXIMUN));
            return;
        }
        Connection *connection = CreateHttpConnection(fromUrl, toUrl, urlScheme, credencial, this->gEvloop);
        if (connection == nullptr) {
            promise.SetValue(retCode - static_cast<int>(MEMORY_ALLOCATION_FAILED));
            return;
        }
        // NOTE : we must save connection before doing connect
        g_connections[connection->sequence] = connection;
        // do connect
        int conState = tcpUtil::DoConnect(toUrl, connection, HttpClient::EventCallBack, HttpClient::WriteCallBack,
                                          HttpClient::ReadCallBack);
        if (conState < 0) {
            BUSLOG_INFO("Connection fail and send fail, conSeq:{},fd:{},toUrl:{},errno:{}", connection->sequence,
                        connection->fd, toUrl, errno);
            promise.SetValue(retCode - static_cast<int>(CONNECTION_REFUSED));
            clientUtil::CloseConnection(connection->sequence);
            return;
        }
        promise.SetValue(connection->sequence);
        return;
    });
    return result;
}

Future<bool> HttpClient::Disconnect(int conSeq)
{
    Promise<bool> promise;
    Future<bool> result = promise.GetFuture();

    (void)gEvloop->AddFuncToEvLoop([conSeq, promise] {
        auto conItem = g_connections.find(conSeq);
        if (g_connections.end() == conItem) {
            BUSLOG_DEBUG("Couldn't find the connection,it may be closed, conSeq:{}", conSeq);
            promise.SetValue(true);
            return;
        }

        clientUtil::CloseConnection(conSeq);
        promise.SetValue(true);
        return;
    });
    return result;
}

Future<int> HttpClient::RegisterResponseCallBack(int conSeq, const ResponseCallback &callback)
{
    Promise<int> promise;
    Future<int> result = promise.GetFuture();

    auto conItem = g_connections.find(conSeq);
    if (conItem == g_connections.end()) {
        BUSLOG_DEBUG("Couldn't find the connection,it may be closed, conSeq:{}", conSeq);
        promise.SetFailed(static_cast<int32_t>(CONNECTION_REFUSED));
        return result;
    }
    Connection *connection = conItem->second;

    ResponseDecoder *decoder = dynamic_cast<ResponseDecoder *>(connection->decoder);
    decoder->RegisterResponseCallBack(callback);
    promise.SetValue(conSeq);
    return result;
}
}    // namespace http
}    // namespace litebus
