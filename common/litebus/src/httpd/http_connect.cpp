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

#include "httpd/http_connect.hpp"
#include "httpd/http_client.hpp"

#include "actor/actor.hpp"
#include "actor/actormgr.hpp"
#include "actor/buslog.hpp"
#include "async/defer.hpp"
#include "async/asyncafter.hpp"
#include "async/future.hpp"
#include "async/option.hpp"
#include "litebus.hpp"
#include "utils/os_utils.hpp"

#include "securec.h"

#include <list>

static const std::string CONNECT_PREFIX = "CONNECT_";
static uint64_t g_requestTimeout = 90000;

using std::placeholders::_1;

using litebus::ActorBase;
using litebus::AID;
using litebus::AsyncAfter;
using litebus::Defer;
using litebus::Future;
using litebus::Option;
using litebus::Promise;
using litebus::Timer;

namespace litebus {
namespace http {

using Pipeline = std::queue<std::shared_ptr<litebus::Promise<litebus::http::Response>>>;

// We spawn 'HttpConnectionActor' as an manager actor to handle user's http request
class HttpConnectionActor : public ActorBase {
public:
    ~HttpConnectionActor() override
    {
        delete pipeline;
        pipeline = nullptr;
    }

    HttpConnectionActor(const HttpConnectionActor &) = delete;
    HttpConnectionActor &operator=(const HttpConnectionActor &) = delete;

    HttpConnectionActor(const int conSeq, const URL &url)
        : ActorBase(CONNECT_PREFIX + std::to_string(conSeq)),
          connectSeq(conSeq),
          pipeline(new(std::nothrow) Pipeline()),
          connectUrl(url),
          timeOut(false),
          responseTimer(Timer())
    {
        BUS_OOM_EXIT(pipeline);
    }

    Future<Response> LaunchRequest(const Request &request, const bool &timeOutFlag)
    {
        if (!CompareUrlDomain(request.url, connectUrl)) {
            return Status(static_cast<int32_t>(INVALID_REQUEST));
        }

        if (pipeline == nullptr) {
            BUSLOG_WARN("Connection has been closed,conSeq={}", connectSeq);
            return Status(static_cast<int32_t>(CONNECTION_REFUSED));
        }

        if (disconnection.GetFuture().IsOK()) {
            BUSLOG_WARN("Connection is closing,conSeq={}", connectSeq);
            return Status(static_cast<int32_t>(CONNECTION_REFUSED));
        }

        if (SendResult.GetFuture().IsError()) {
            BUSLOG_WARN("Launch request failed before,conSeq={}", connectSeq);
            return Status(static_cast<int32_t>(CONNECTION_REFUSED));
        }

        timeOut = timeOutFlag;

        // NOTE : we must push promise into pipeline before send
        std::shared_ptr<litebus::Promise<Response>> promise(std::make_shared<litebus::Promise<Response>>());
        BUS_OOM_EXIT(promise);

        pipeline->push(promise);

        int tempSeq = connectSeq;

        Future<bool> sendResult = HttpClient::GetInstance()->LaunchRequest(request, tempSeq);
        (void)sendResult.OnComplete(
            Defer(GetAID(), &HttpConnectionActor::HandleRequestCompleted, std::placeholders::_1, request));

        return promise->GetFuture();
    }

    void HandleResponse(const Response &response)
    {
        if (pipeline == nullptr) {
            BUSLOG_WARN("Connection has been closed,conSeq={}", connectSeq);
            return;
        }

        // process any decoded responses
        bool closeFlag = false;

        if (pipeline->empty()) {
            (void)Disconnect();
            return;
        }

        // cancel responseTimer when response receives
        if (timeOut) {
            (void)TimerTools::Cancel(responseTimer);
        }

        std::shared_ptr<litebus::Promise<Response>> promise = pipeline->front();

        promise->SetValue(response);
        pipeline->pop();

        HeaderMap headers = response.headers;
        auto headerIter = headers.find("Connection");
        if ((headerIter == headers.end()) || (headers["Connection"] == "close")) {
            BUSLOG_DEBUG("This is the last response, close the connection, conSeq:{}", connectSeq);
            closeFlag = true;
            while (!pipeline->empty()) {
                promise = pipeline->front();
                promise->SetFailed(static_cast<int32_t>(CANNOT_SEND_AFTER_SHUTDOWN));
                pipeline->pop();
            }
        }

        if (closeFlag && pipeline->empty()) {
            (void)Disconnect();
        }

        return;
    }

    Future<bool> Disconnected()
    {
        return disconnection.GetFuture();
    }

    Future<bool> Disconnect()
    {
        return HttpClient::GetInstance()
            ->Disconnect(connectSeq)
            .Then(Defer(GetAID(), &HttpConnectionActor::HandleDisconnect, CANNOT_SEND_AFTER_SHUTDOWN));
    }

    Future<bool> HandleDisconnect(const int errCode)
    {
        if (pipeline == nullptr) {
            BUSLOG_DEBUG("Connection has been closed,conSeq={}", connectSeq);
            return true;
        }

        // cancel responseTimer when disconnect receives
        if (timeOut) {
            (void)TimerTools::Cancel(responseTimer);
        }

        // free g_pipeline
        while (!pipeline->empty()) {
            std::shared_ptr<litebus::Promise<Response>> promise = pipeline->front();
            promise->SetFailed((errCode == 0) ? static_cast<int>(CONNECTION_REFUSED) : errCode);
            pipeline->pop();
        }
        delete pipeline;
        pipeline = nullptr;

        disconnection.SetValue(true);

        return true;
    }

    void HandleRequestCompleted(const Future<bool> &ret, const Request &request)
    {
        // ret should be ok or error
        if (ret.IsError()) {
            // when httpclient returns error, it means the connection has been closed, we only need to clean pipeline
            BUSLOG_WARN("Request send failed,conSeq={}", connectSeq);
            SendResult.SetFailed(static_cast<int32_t>(CONNECTION_REFUSED));
            (void)HandleDisconnect(static_cast<int>(CONNECTION_REFUSED));
        } else {
            // launch a timer to check response timeout, call 'HandleRequestTimeOut' when timeout
            if (timeOut) {
                responseTimer = AsyncAfter(request.timeout.IsSome() ? request.timeout.Get() : g_requestTimeout,
                                           GetAID(), &HttpConnectionActor::HandleRequestTimeOut);
            }
        }
        return;
    }

    void HandleRequestTimeOut()
    {
        BUSLOG_WARN("Launch request timeout,conSeq={}", connectSeq);
        HttpClient::GetInstance()
            ->Disconnect(connectSeq)
            .Then(Defer(GetAID(), &HttpConnectionActor::HandleDisconnect, CONNECTION_TIMEOUT));

        return;
    }

protected:
    void Finalize() override
    {
    }

private:
    int connectSeq;

    Pipeline *pipeline;

    Promise<bool> SendResult;

    Promise<bool> disconnection;

    URL connectUrl;

    bool timeOut;

    Timer responseTimer;
};

// NOTE: A wrapper Class to spawn/terminate 'HttpConnectionActor'
struct HttpConnect::HttpConnection {
    HttpConnection(const int conSeq, const URL &url)
        : conActorId(Spawn(std::make_shared<HttpConnectionActor>(conSeq, url)))
    {
    }

    ~HttpConnection()
    {
        try {
            BUSLOG_DEBUG("HttpConnect is destroying, aid={}", std::string(conActorId));
            Terminate(conActorId);
        } catch (...) {
            // Ignore
        }
    }

    AID conActorId;
};

HttpConnect::HttpConnect(const int conSeq, const URL &url)
    : connection(std::make_shared<HttpConnect::HttpConnection>(conSeq, url))
{
}

Future<HttpConnect> HttpConnect::ConnectEstablishedCallback(const int &conSeq, const URL &url)
{
    if (conSeq >= 0) {
        BUSLOG_DEBUG("Connect succeed, conSeq={}", conSeq);
        return HttpConnect(conSeq, url);
    } else {
        int retCode = 0;
        retCode = retCode - conSeq;
        BUSLOG_WARN("Connect failed, errCode={}", retCode);
        return Status(retCode);
    }
}

Future<Response> HttpConnect::ConnectAndLaunchReqCallback(const int conSeq, const Request &request,
                                                          const bool timeOutFlag)
{
    if (conSeq >= 0) {
        BUSLOG_DEBUG("Connect succeed, conSeq={}", conSeq);
        HttpConnect connect = HttpConnect(conSeq, request.url);
        Future<Response> response = connect.LaunchRequest(request, timeOutFlag);
        // NOTE : we must maintain a copy of the HttpConnect(which is reference-counted)
        // until the disconnection promise has been set
        (void)connect.Disconnected().OnComplete([connect]() {});
        return response;
    } else {
        int retCode = 0;
        retCode = retCode - conSeq;
        BUSLOG_WARN("Connect failed, errCode={}", retCode);
        return Status(retCode);
    }
}

void HttpConnect::ConnectClosedCallback(const int conSeq, const int errCode)
{
    std::string actorName = "CONNECT_" + std::to_string(conSeq);
    (void)Async(actorName, &HttpConnectionActor::HandleDisconnect, errCode);
    return;
}

void HttpConnect::ResponseCompletedCallback(const int conSeq, Response *responsePtr)
{
    std::string actorName = "CONNECT_" + std::to_string(conSeq);
    Response responseObj = *responsePtr;
    delete responsePtr;
    Async(actorName, &HttpConnectionActor::HandleResponse, responseObj);
    return;
}

Future<bool> HttpConnect::Disconnect() const
{
    return Async(connection->conActorId, &HttpConnectionActor::Disconnect);
}

Future<bool> HttpConnect::Disconnected() const
{
    return Async(connection->conActorId, &HttpConnectionActor::Disconnected);
}

Future<Response> HttpConnect::LaunchRequest(const Request &request, const bool &timeOutFlag)
{
    return Async(connection->conActorId, &HttpConnectionActor::LaunchRequest, request, timeOutFlag);
}

Future<HttpConnect> Connect(const URL &url, litebus::Option<std::string> credencial)
{
    if (url.ip.IsNone() || url.port.IsNone() || url.scheme.IsNone()) {
        BUSLOG_WARN("Couldn't connect with no ip,port,scheme.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    if (url.scheme.Get() != HTTPS_SCHEME && url.scheme.Get() != HTTP_SCHEME) {
        BUSLOG_WARN("Only support 'http' and 'https'");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

#ifndef SSL_ENABLED
    if (url.scheme.Get() == HTTPS_SCHEME) {
        BUSLOG_WARN("Couldn't connect to url with 'https' while ssl isnot enable.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }
#endif

    return HttpClient::GetInstance()->Connect(url, credencial).Then(
        std::bind(&HttpConnect::ConnectEstablishedCallback, ::_1, url));
}

Future<Response> LaunchRequest(const Request &request)
{
    // check request
    if (!CheckReqUrl(request.url)) {
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    if (request.keepAlive) {
        BUSLOG_WARN("Couldn't create keep-alive request normally.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    HeaderMap headerMap = request.headers;
    auto conItem = headerMap.find("Connection");
    if (headerMap.end() != conItem && headerMap["Connection"] != "close") {
        BUSLOG_WARN("Only 'Connection:close' are allowed in headers.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    if (!CheckReqType(request.method)) {
        BUSLOG_WARN("Only 'POST GET PUT DELETE PATCH' are allowed.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    BUSLOG_DEBUG("Launch request, ip:{},port:{},path:{}", request.url.ip.Get(), request.url.port.Get(),
                 request.url.path);
    return HttpClient::GetInstance()
        ->Connect(request.url, request.credential)
        .Then(std::bind(&HttpConnect::ConnectAndLaunchReqCallback, ::_1, request, true));
}

Future<Response> LaunchRequest(const Request &request, const ResponseCallback &callback)
{
    if (!callback) {
        BUSLOG_WARN("Callback is empty");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    // check request
    if (!CheckReqUrl(request.url)) {
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    if (!CheckReqType(request.method)) {
        BUSLOG_WARN("Only 'POST GET PUT DELETE PATCH' are allowed.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    BUSLOG_DEBUG("Launch request, ip:{},port:{},path:{}", request.url.ip.Get(), request.url.port.Get(),
                 request.url.path);
    return HttpClient::GetInstance()
        ->Connect(request.url, request.credential)
        .Then(std::bind(&HttpClient::RegisterResponseCallBack, ::_1, callback))
        .Then(std::bind(&HttpConnect::ConnectAndLaunchReqCallback, ::_1, request, false));
}

Future<Response> Post(const URL &url, const Option<std::unordered_map<std::string, std::string>> &headers,
                      const Option<std::string> &body, const Option<std::string> &contentType,
                      const Option<uint64_t> &reqTimeout)
{
    if (body.IsNone() && contentType.IsSome()) {
        BUSLOG_WARN("Couldn't create post request with a content-type but no body.");
        return Status(static_cast<int32_t>(INVALID_REQUEST));
    }

    Request request("POST", false, url);

    if (headers.IsSome()) {
        for (const auto &headerItem : headers.Get()) {
            request.headers[headerItem.first] = headerItem.second;
        }
    }

    if (body.IsSome()) {
        request.body = body.Get();
    }

    if (contentType.IsSome()) {
        request.headers["Content-Type"] = contentType.Get();
    }

    if (reqTimeout.IsSome()) {
        request.timeout = reqTimeout.Get();
    } else {
        request.timeout = g_requestTimeout;
    }

    return LaunchRequest(request);
}

Future<Response> Get(const URL &url, const Option<std::unordered_map<std::string, std::string>> &headers,
                     const Option<uint64_t> &reqTimeout)
{
    Request request("GET", false, url);

    if (headers.IsSome()) {
        for (const auto &headerItem : headers.Get()) {
            request.headers[headerItem.first] = headerItem.second;
        }
    }

    if (reqTimeout.IsSome()) {
        request.timeout = reqTimeout.Get();
    } else {
        request.timeout = g_requestTimeout;
    }

    return LaunchRequest(request);
}

bool CheckReqType(const std::string &method)
{
    return (ALLOW_METHOD.find(method) != ALLOW_METHOD.end());
}

bool CheckReqUrl(const URL &url)
{
    if (url.scheme.IsNone()) {
        BUSLOG_WARN("Couldn't create http request with no scheme.");
        return false;
    }

    if (url.scheme.Get() != HTTPS_SCHEME && url.scheme.Get() != HTTP_SCHEME) {
        BUSLOG_WARN("Only support 'http' and 'https'");
        return false;
    }

#ifndef SSL_ENABLED
    if (url.scheme.Get() == HTTPS_SCHEME) {
        BUSLOG_WARN("Couldn't create http request with 'https' while ssl isnot enable.");
        return false;
    }
#endif

    if (url.ip.IsNone()) {
        BUSLOG_WARN("Couldn't create http request with no ip.");
        return false;
    }

    if (url.port.IsNone()) {
        BUSLOG_WARN("Couldn't create http request with no port.");
        return false;
    }
    return true;
}

std::string GetHttpError(int32_t httpErrCode)
{
    BUSLOG_WARN("Http error:{}", httpErrCode);
    if (httpErrCode == 0) {
        return std::string("Unknown error.");
    }

    return litebus::os::Strerror(httpErrCode);
}

void SetHttpRequestTimeOut(uint64_t duration)
{
    BUSLOG_INFO("Http timeout:{}", duration);
    g_requestTimeout = duration;
}

}    // namespace http
}    // namespace litebus
