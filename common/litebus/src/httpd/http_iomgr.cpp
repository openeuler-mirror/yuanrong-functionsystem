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

#include <deque>

#include <cerrno>
#include <securec.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/eventfd.h>

#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/async.hpp"
#include "async/uuid_generator.hpp"
#include "async/option.hpp"
#include "httpd/http.hpp"
#include "httpd/http_iomgr.hpp"
#ifdef SSL_ENABLED
#include "ssl/openssl_wrapper.hpp"
#include "ssl/ssl_socket.hpp"
#endif
#include "httpd/http_decoder.hpp"
#include "httpd/http_pipeline_proxy.hpp"
#include "httpd/http_sysmgr.hpp"
#include "litebus.hpp"
#include "tcp/tcpmgr.hpp"
#include "utils/string_utils.hpp"
#include "utils/os_utils.hpp"

using namespace std;
using namespace litebus;
using litebus::http::Request;

using namespace std;
using std::placeholders::_1;
using namespace litebus;
using litebus::http::HttpPipelineProxy;
using litebus::http::Request;
using litebus::http::Response;
using litebus::http::ResponseCode;
using litebus::http::URL;

using namespace std;

// HTTP proxies.
static map<int, shared_ptr<http::HttpPipelineProxy>> g_httpProxies = {};

namespace litebus {
const std::string SYSMGR_ACTOR_NAME = "SysManager";
const std::string HTTP_PIPELINE_PROXY_NAME = "HTTP_PIPELINE_PROXY";
const std::string HTTP_URL_DELIMITER = "/";

namespace iomgrUtil {
int RecvHttpReq(Connection *connection, IOMgr::MsgHandler msgHandler)
{
    int retval;

    retval = RecvHttpReq(connection, msgHandler, RECV_BUFFER_SIZE);
    if (retval < 0) {
        return retval;
    }

    retval = connection->socketOperate->Pending(connection);
    if (retval > 0) {
        retval = RecvHttpReq(connection, msgHandler, static_cast<uint32_t>(retval));
    }

    return retval;
}

int RecvHttpReq(Connection *connection, IOMgr::MsgHandler msgHandler, uint32_t size)
{
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

    if (avail == 0) {
        delete[] buf;
        return retval;
    }

    // TODD : check decoder returns
    RequestDecoder *decoder = nullptr;
    decoder = static_cast<RequestDecoder *>(connection->decoder);
    if (decoder == nullptr) {
        HttpIOMgr::SetEventCb(connection);
        decoder = new (std::nothrow) RequestDecoder();
        BUS_OOM_EXIT(decoder);
        connection->decoder = decoder;
    }

    deque<Request *> requests = decoder->Decode(static_cast<const char *>(buf), avail);

    delete[] buf;

    if (!requests.empty()) {
        // handle request
        for (unsigned i = 0; i < requests.size(); i++) {
            requests[i]->client = connection->peer;
            HttpIOMgr::HandleRequest(requests[i], connection, msgHandler);
        }
        requests.clear();
    }

    if (decoder->Failed() || connection->connState == ConnectionState::DISCONNECTING || connection->meetMaxClients
        || connection->parseFailed) {
        BUSLOG_WARN("Failed to decode data while receiving, fd={}, conSeq={}, meetMax={}, parseFailed={}",
                    connection->fd, connection->sequence, connection->meetMaxClients, connection->parseFailed);
        connection->connState = ConnectionState::DISCONNECTING;
        return -1;
    }

    return avail;
}

int RecvHttpRsp(Connection *conn, IOMgr::MsgHandler)
{
    int retval;
    uint32_t avail;
    uint32_t size = RECV_BUFFER_SIZE;
    char *buf = new (std::nothrow) char[size];
    BUS_OOM_EXIT(buf);
    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(buf, size, 0, size);
    retval = conn->socketOperate->Recv(conn, buf, size, avail);
    if (retval < 0) {
        conn->connState = ConnectionState::DISCONNECTING;
        delete[] buf;
        return -1;
    }
    delete[] buf;

    return 0;
}

}    // namespace iomgrUtil

int HttpIOMgr::AllocateConnId()
{
    if (g_httpProxies.size() == MAX_CON_NUM) {
        return 0;
    }

    bool idConflict = true;
    int id = 0;
    for (int i = 0; i < MAX_CON_NUM; i++) {
        id = litebus::localid_generator::GenHttpServerConnId();
        auto proxyIter = g_httpProxies.find(id);
        if (proxyIter == g_httpProxies.end()) {
            idConflict = false;
            break;
        }
    }
    return idConflict ? 0 : id;
}

void HttpIOMgr::ErrorCallback(void *context)
{
    if (context == nullptr) {
        BUSLOG_WARN("ctx is null.");
        return;
    }
    Connection *connection = static_cast<Connection *>(context);
    if (connection->connState != ConnectionState::CONNECTED) {
        DeleteHttpProxy(connection->sequence);

        // NOTE : close decoder
        if (connection->decoder != nullptr) {
            RequestDecoder *requestDecoder = static_cast<RequestDecoder *>(connection->decoder);
            delete requestDecoder;
            connection->decoder = nullptr;
        }
        ConnectionUtil::CloseConnection(connection);
    }
}

void HttpIOMgr::ParseKMsgUrl(const std::string &urlPath, std::string &kmsgActorName, std::string &kmsgTypeName)
{
    // find actor name ,begin with the first '/', end with the second '/'
    size_t actorNameEndIndex = urlPath.find('/', HTTP_URL_DELIMITER.size());
    if (actorNameEndIndex != string::npos) {
        // as : /***/, it means actor name is not null
        kmsgActorName = urlPath.substr(HTTP_URL_DELIMITER.size(), actorNameEndIndex - HTTP_URL_DELIMITER.size());
        kmsgTypeName = urlPath.substr(actorNameEndIndex + HTTP_URL_DELIMITER.size());
    } else {
        // as : /***, it means actor name is null
        kmsgTypeName = urlPath.substr(HTTP_URL_DELIMITER.size());
    }
}

void HttpIOMgr::HandleKMsgRequest(litebus::http::Request *request, Connection *connection,
                                  IOMgr::MsgHandler msgHandler)
{
    if (connection->parseFailed) {
        BUSLOG_WARN("Drop http message with url={}", request->url.path);
        return;
    }
    string &urlPath = request->url.path;

    if (urlPath.size() <= HTTP_URL_DELIMITER.size() || !strings::StartsWithPrefix(urlPath, "/")) {
        BUSLOG_WARN("receive http message with invalid url={}", urlPath);
        connection->parseFailed = true;
        return;
    }

    // parse actor name and msg name
    string kmsgActorName = std::string();
    string kmsgTypeName = std::string();
    ParseKMsgUrl(urlPath, kmsgActorName, kmsgTypeName);

    // parse from
    std::string from = std::string();
    auto iterLibprocess = request->headers.find("Libprocess-From");
    if (request->headers.end() != iterLibprocess) {
        // NOTE : from = request->headers["Libprocess-From"];
        from = iterLibprocess->second;
    }

    auto iterLitebus = request->headers.find("Litebus-From");
    if (request->headers.end() != iterLitebus) {
        // NOTE : from = AID(request->headers["Litebus-From"]);
        from = iterLitebus->second;
    }

    BUSLOG_DEBUG("receive message (from, to, toMsgName)=({}, {}, {})", from, kmsgActorName, kmsgTypeName);

    // NOTE : AID to(kmsgActorName.c_str());
    std::unique_ptr<MessageBase> message(new MessageBase(from, kmsgActorName, std::move(kmsgTypeName),
                                                         std::move(request->body), MessageBase::Type::KMSG));
    if (request->headers.find("Authorization") != request->headers.end()) {
        message->signature = std::move(request->headers.at("Authorization"));
    }

    bool validAid = message->from.OK();
    if (kmsgActorName.empty() || kmsgActorName == HTTP_URL_DELIMITER || !validAid) {
        BUSLOG_WARN("receive http message with invalid url, url:{},from:{},to:{}", urlPath, std::string(message->from),
                    std::string(message->to));
        connection->parseFailed = true;
        return;
    }

    if (msgHandler != nullptr) {
        msgHandler(std::move(message));
    }
}

AID HttpIOMgr::ParseHttpUrl(litebus::http::Request *request)
{
    AID receiver;
    receiver.SetUrl(ActorMgr::GetActorMgrRef()->GetUrl("tcp").c_str());
    string delegate = ActorMgr::GetActorMgrRef()->GetDelegate();
    vector<string> tokens = strings::Tokenize(request->url.path, "/");

    if (!delegate.empty() && !((tokens.size() > 0) && (ActorMgr::GetActorMgrRef()->GetActor(tokens[0]) != nullptr))) {
        receiver.SetName(delegate);
        request->url.path = (tokens.size() > 0) ? ("/" + delegate + request->url.path) : ("/" + delegate);
    } else {
        if (tokens.size() == 0) {
            receiver.SetName("");
        } else {
            receiver.SetName(tokens[0]);
        }
    }
    return receiver;
}

void HttpIOMgr::HandleDefaultRequest(litebus::http::Request *request, Connection *connection,
                                     IOMgr::MsgHandler msgHandler)
{
    // NOTE : handle http request here
    // We treat this message as a common http request.
    BUSLOG_DEBUG("receive http message with url={}", request->url.path);
    if (connection->meetMaxClients) {
        BUSLOG_WARN("Drop http message with url={}", request->url.path);
        return;
    }
    AID receiver = ParseHttpUrl(request);
    unique_ptr<Promise<Response>> promise(new (std::nothrow) Promise<Response>());
    BUS_OOM_EXIT(promise);
    // NOTE: We handle http response one by one to support HTTP/1.1
    // pipeline.
    shared_ptr<HttpPipelineProxy> httpPipelineProxy;
    if (g_httpProxies.find(connection->sequence) != g_httpProxies.end()) {
        httpPipelineProxy = g_httpProxies[connection->sequence];
    } else {
        int id = AllocateConnId();
        if (id == 0) {
            BUSLOG_WARN("Failed to allocate id, fd={}, pipeline size={}", connection->fd, g_httpProxies.size());
            connection->meetMaxClients = true;
            return;
        }
        connection->sequence = id;
        string proxyActorName = HTTP_PIPELINE_PROXY_NAME + +"(" + to_string(connection->sequence) + ")";
        httpPipelineProxy.reset(new (std::nothrow)
                                    HttpPipelineProxy(proxyActorName, connection, connection->sequence));
        BUS_OOM_EXIT(httpPipelineProxy);
        (void)Spawn(httpPipelineProxy);
        g_httpProxies[connection->sequence] = httpPipelineProxy;
        BUSLOG_DEBUG("create a new http pipeline proxy, fd={}, conSeq={}", connection->fd, connection->sequence);
    }

    if (ActorMgr::GetActorMgrRef()->GetActor(receiver) == nullptr) {
        // actor does not exist, return 404
        Async(httpPipelineProxy->GetAID(), &HttpPipelineProxy::Process, *request, http::NotFound());
        return;
    }

    Async(httpPipelineProxy->GetAID(), &HttpPipelineProxy::Process, *request, promise->GetFuture());

    std::unique_ptr<http::HttpMessage> httpMessage(new (std::nothrow) http::HttpMessage(
        *request, std::move(promise), AID(), receiver, request->url.path, MessageBase::Type::KHTTP));
    BUS_OOM_EXIT(httpMessage);
    if (msgHandler != nullptr) {
        msgHandler(std::move(httpMessage));
    }
}

void HttpIOMgr::HandleRequest(litebus::http::Request *request, Connection *connection, IOMgr::MsgHandler msgHandler)
{
    BUSLOG_DEBUG("url,method,client,body size, u:{},m:{},c:{},s:{}", request->url.path, request->method,
                 request->client.Get(), request->body.size());

    // NOTE : we need delete request ptr here
    bool isKMsg = false;
    if ((request->headers.find("Libprocess-From") != request->headers.end())
        || (request->headers.find("Litebus-From") != request->headers.end())) {
        isKMsg = true;
    }

    if (isKMsg) {
        HandleKMsgRequest(request, connection, msgHandler);
    } else {
        HandleDefaultRequest(request, connection, msgHandler);
    }
    delete request;
}

void HttpIOMgr::DeleteHttpProxy(int conSeq)
{
    map<int, shared_ptr<HttpPipelineProxy>>::iterator key = g_httpProxies.find(conSeq);
    if (key != g_httpProxies.end()) {
        BUSLOG_DEBUG("remove proxy, conSeq={}", conSeq);
        AID aid = g_httpProxies[conSeq]->GetAID();
        (void)g_httpProxies.erase(key);
        Terminate(aid);
    }
}

bool HttpIOMgr::CheckHttpCon(int conSeq)
{
    map<int, shared_ptr<HttpPipelineProxy>>::iterator key = g_httpProxies.find(conSeq);
    if (key != g_httpProxies.end()) {
        return true;
    }

    BUSLOG_WARN("proxy removed, conSeq={}", conSeq);
    return false;
}

void HttpIOMgr::EnableHttp()
{
    TCPMgr::RegisterRecvHttpCallBack(iomgrUtil::RecvHttpReq, iomgrUtil::RecvHttpRsp, CheckHttpCon);
    (void)litebus::Spawn(std::make_shared<http::HttpSysMgr>(SYSMGR_ACTOR_NAME));
}

void HttpIOMgr::SetEventCb(Connection *conn)
{
    conn->eventCallBack = HttpIOMgr::ErrorCallback;
    return;
}

}    // namespace litebus
