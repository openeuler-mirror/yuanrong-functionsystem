/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "async_uds_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "securec.h"

namespace functionsystem::runtime_manager {

namespace {
constexpr const char *EVLOOP_THREAD_NAME = "YR_ExecutorIO";

std::unique_ptr<litebus::EvLoop> g_executorEvLoop;
std::mutex g_evloopMutex;

constexpr size_t HEADER_SEPARATOR_LEN = 4;
constexpr size_t CONTENT_LENGTH_PREFIX_LEN = 16;
constexpr size_t MAX_RESPONSE_SIZE = 16 * 1024 * 1024;  // 16 MiB cap to bound memory
}  // namespace

std::string AsyncUdsClient::BuildHttpRequest(const std::string &method, const std::string &path,
                                             const std::string &body)
{
    auto hasCtrlChar = [](const std::string &s) {
        return std::any_of(s.begin(), s.end(), [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; });
    };
    if (hasCtrlChar(method) || hasCtrlChar(path)) {
        YRLOG_ERROR("AsyncUdsClient: refuse control char in request line: {} {}", method, path);
        return "";
    }
    std::ostringstream oss;
    oss << method << " " << path << " HTTP/1.1\r\n";
    oss << "Host: localhost\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "Content-Length: " << body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

litebus::EvLoop &AsyncUdsClient::EvLoopRef()
{
    std::lock_guard<std::mutex> lock(g_evloopMutex);
    if (g_executorEvLoop == nullptr) {
        g_executorEvLoop = std::make_unique<litebus::EvLoop>();
        if (!g_executorEvLoop->Init(EVLOOP_THREAD_NAME)) {
            YRLOG_ERROR("AsyncUdsClient: failed to init EvLoop");
            g_executorEvLoop.reset();
        } else {
            YRLOG_INFO("AsyncUdsClient: EvLoop initialized (thread={})", EVLOOP_THREAD_NAME);
        }
    }
    // Init failed (epoll_create unavailable): never deref nullptr. Return a dead EvLoop whose
    // AddFdEvent/DelFdEvent are safe no-ops (epoll_ctl on efd=-1 returns EBADF->BUS_ERROR), so
    // in-flight callbacks degrade to logged dropped requests instead of crashing.
    if (g_executorEvLoop == nullptr) {
        static litebus::EvLoop deadEvLoop;
        return deadEvLoop;
    }
    return *g_executorEvLoop;
}

bool AsyncUdsClient::InitEvLoop()
{
    std::lock_guard<std::mutex> lock(g_evloopMutex);
    if (g_executorEvLoop == nullptr) {
        g_executorEvLoop = std::make_unique<litebus::EvLoop>();
        if (!g_executorEvLoop->Init(EVLOOP_THREAD_NAME)) {
            YRLOG_ERROR("AsyncUdsClient: EvLoop Init failed");
            g_executorEvLoop.reset();
        } else {
            YRLOG_INFO("AsyncUdsClient: EvLoop initialized (thread={})", EVLOOP_THREAD_NAME);
        }
    }
    return g_executorEvLoop != nullptr;
}

void AsyncUdsClient::FinishEvLoop()
{
    std::lock_guard<std::mutex> lock(g_evloopMutex);
    if (g_executorEvLoop != nullptr) {
        g_executorEvLoop->Finish();
        g_executorEvLoop.reset();
    }
}

bool AsyncUdsClient::StartConnect(RequestContext *ctx)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        YRLOG_ERROR("AsyncUdsClient: socket() failed: {}", std::strerror(errno));
        return false;
    }
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (ctx->socketPath.length() >= sizeof(addr.sun_path)) {
        YRLOG_ERROR("AsyncUdsClient: socket path too long: {}", ctx->socketPath);
        (void)::close(fd);
        return false;
    }
    (void)strncpy_s(addr.sun_path, sizeof(addr.sun_path), ctx->socketPath.c_str(),
                    ctx->socketPath.length());

    int ret = ::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (ret == 0) {
        ctx->fd = fd;
        YRLOG_INFO("{} connected to socket: {} (fd={}, {} {})", ctx->logTag, ctx->socketPath, fd, ctx->method,
            ctx->fullPath);
        return true;
    }
    if (errno == EINPROGRESS) {
        ctx->fd = fd;
        YRLOG_INFO("{} connecting to socket: {} (fd={}, EINPROGRESS, {} {})", ctx->logTag, ctx->socketPath, fd,
            ctx->method, ctx->fullPath);
        if (ctx->evloop->AddFdEvent(fd, EPOLLOUT, &AsyncUdsClient::OnConnectWritable, ctx) != 0) {
            YRLOG_ERROR("AsyncUdsClient: AddFdEvent(EPOLLOUT connect) failed for fd {}", fd);
            (void)::close(fd);
            ctx->fd = -1;
            return false;
        }
        return false;
    }
    YRLOG_ERROR("{} connect failed: {} {} ({}, elapsed={}ms)", ctx->logTag, ctx->method, ctx->fullPath,
                ctx->socketPath, ElapsedMs(ctx));
    (void)::close(fd);
    return false;
}

void AsyncUdsClient::OnConnectWritable(int fd, uint32_t /* events */, void *data) // NOLINT(g.FUN.05-cpp)
{
    auto *ctx = static_cast<RequestContext *>(data);
    if (ctx == nullptr || ctx->fd != fd) {
        return;
    }
    int err = 0;
    socklen_t errLen = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errLen) < 0 || err != 0) {
        YRLOG_ERROR("{} connect failed: {} {} ({}, elapsed={}ms)", ctx->logTag, ctx->method, ctx->fullPath,
                    ctx->socketPath, ElapsedMs(ctx));
        FailRequest(ctx, FailStage::CONNECT);
        return;
    }
    // ModifyFdEvent only changes the events mask, not the stored handler, so re-arming
    // OnWritable requires Del+Add (same as the EPOLLIN switch in DoSend). On AddFdEvent
    // failure, FailRequest->CleanupFd calls DelFdEvent again — DelFdEvent is idempotent
    // (FindEvent returns null, BUS_ERROR) so this is safe; mark fd -1 to skip the repeat.
    (void)ctx->evloop->DelFdEvent(fd);
    if (ctx->evloop->AddFdEvent(fd, EPOLLOUT, &AsyncUdsClient::OnWritable, ctx) != 0) {
        YRLOG_ERROR("{} re-arm EPOLLOUT failed: {} {} ({})", ctx->logTag, ctx->method, ctx->fullPath, ctx->socketPath);
        (void)::close(fd);
        ctx->fd = -1;
        FailRequest(ctx, FailStage::CONNECT);
        return;
    }
    YRLOG_INFO("{} connected to socket: {} (fd={}, async connect done, {} {})", ctx->logTag, ctx->socketPath,
        fd, ctx->method, ctx->fullPath);
    OnWritable(fd, EPOLLOUT, data);
}

bool AsyncUdsClient::DoSend(RequestContext *ctx)
{
    while (ctx->sendOffset < ctx->httpRequest.size()) {
        ssize_t sent = ::send(ctx->fd, ctx->httpRequest.data() + ctx->sendOffset,
                              ctx->httpRequest.size() - ctx->sendOffset, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            YRLOG_ERROR("{} send failed: {} {} ({}, elapsed={}ms)", ctx->logTag, ctx->method, ctx->fullPath,
                        ctx->socketPath, ElapsedMs(ctx));
            return false;
        }
        if (sent == 0) {
            return false;
        }
        ctx->sendOffset += static_cast<size_t>(sent);
    }
    // Send done: switch to read mode. ModifyFdEvent only changes the events
    // mask, not the stored handler, so the fd must be re-registered with
    // OnReadable via Del+Add (ctx survives — DelFdEvent frees only EventData).
    (void)ctx->evloop->DelFdEvent(ctx->fd);
    if (ctx->evloop->AddFdEvent(ctx->fd, EPOLLIN, &AsyncUdsClient::OnReadable, ctx) != 0) {
        YRLOG_ERROR("{} switch to EPOLLIN failed: {} {} ({})", ctx->logTag, ctx->method, ctx->fullPath,
                    ctx->socketPath);
        (void)::close(ctx->fd);
        ctx->fd = -1;
        return false;
    }
    return true;
}

void AsyncUdsClient::OnWritable(int fd, uint32_t /* events */, void *data)     // NOLINT(g.FUN.05-cpp)
{
    auto *ctx = static_cast<RequestContext *>(data);
    if (ctx == nullptr || ctx->fd != fd) {
        return;
    }
    if (!DoSend(ctx)) {
        FailRequest(ctx, FailStage::SEND);
    }
}

void AsyncUdsClient::ParseHeaderIfReady(RequestContext *ctx)
{
    if (ctx->headerParsed) {
        return;
    }
    size_t headerEnd = ctx->response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return;
    }
    ctx->headerParsed = true;
    std::string headers = ctx->response.substr(0, headerEnd);
    std::string lowerHeaders = headers;
    std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    size_t clPos = lowerHeaders.find("content-length:");
    if (clPos != std::string::npos) {
        size_t crlfPos = headers.find("\r\n", clPos);
        if (crlfPos != std::string::npos) {
            ctx->hasContentLength = true;
            try {
                ctx->contentLength = std::stoul(headers.substr(clPos + CONTENT_LENGTH_PREFIX_LEN,
                                                               crlfPos - clPos - CONTENT_LENGTH_PREFIX_LEN));
            } catch (const std::exception &e) {
                YRLOG_WARN("AsyncUdsClient: bad Content-Length: {}", e.what());
                ctx->hasContentLength = false;
            }
        }
    }
}

bool AsyncUdsClient::IsResponseComplete(const RequestContext *ctx)
{
    if (!ctx->headerParsed) {
        return false;
    }
    size_t headerEnd = ctx->response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }
    size_t bodyStart = headerEnd + HEADER_SEPARATOR_LEN;
    size_t bodyLen = ctx->response.size() - bodyStart;
    if (ctx->hasContentLength) {
        return bodyLen >= ctx->contentLength;
    }
    return false;  // Connection: close → rely on EOF (recv returns 0)
}

void AsyncUdsClient::OnReadable(int fd, uint32_t /* events */, void *data)     // NOLINT(g.FUN.05-cpp)
{
    auto *ctx = static_cast<RequestContext *>(data);
    if (ctx == nullptr || ctx->fd != fd) {
        return;
    }
    char buf[4096];
    for (;;) {
        ssize_t received = ::recv(fd, buf, sizeof(buf), 0);
        if (received > 0) {
            if (ctx->response.size() + static_cast<size_t>(received) > MAX_RESPONSE_SIZE) {
                YRLOG_ERROR("{} response too large: {} {} ({}, elapsed={}ms, bytes={})", ctx->logTag, ctx->method,
                            ctx->fullPath, ctx->socketPath, ElapsedMs(ctx), ctx->response.size());
                FailRequest(ctx, FailStage::RECV);
                return;
            }
            ctx->response.append(buf, static_cast<size_t>(received));
            ParseHeaderIfReady(ctx);
            if (IsResponseComplete(ctx)) {
                CompleteRequest(ctx);
                return;
            }
            continue;
        }
        if (received == 0) {  // EOF
            CompleteRequest(ctx);
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        YRLOG_ERROR("{} recv failed: {} {} ({}, elapsed={}ms)", ctx->logTag, ctx->method, ctx->fullPath,
                    ctx->socketPath, ElapsedMs(ctx));
        FailRequest(ctx, FailStage::RECV);
        return;
    }
}

void AsyncUdsClient::CompleteRequest(RequestContext *ctx)
{
    YRLOG_INFO("{} request done: {} {} (elapsed={}ms, bytes={})", ctx->logTag, ctx->method, ctx->fullPath,
               ElapsedMs(ctx), ctx->response.size());
    try {
        nlohmann::json result = ctx->parser ? ctx->parser(ctx->response) : nlohmann::json::parse(ctx->response);
        ctx->promise.SetValue(std::move(result));
    } catch (const std::exception &e) {
        YRLOG_ERROR("{} response parse failed: {} {} ({}, elapsed={}ms): {}", ctx->logTag, ctx->method,
                    ctx->fullPath, ctx->socketPath, ElapsedMs(ctx), e.what());
        FailRequest(ctx, FailStage::PARSE);
        return;
    }
    CleanupFd(ctx);
}

void AsyncUdsClient::FailRequest(RequestContext *ctx, FailStage stage)
{
    int32_t errCode = static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION);
    switch (stage) {
        case FailStage::CONNECT: errCode = static_cast<int32_t>(RequestErrorStage::CONNECT); break;
        case FailStage::SEND: errCode = static_cast<int32_t>(RequestErrorStage::SEND); break;
        case FailStage::RECV: errCode = static_cast<int32_t>(RequestErrorStage::RECV); break;
        case FailStage::PARSE: errCode = static_cast<int32_t>(RequestErrorStage::PARSE); break;
    }
    ctx->promise.SetFailed(errCode);
    CleanupFd(ctx);
}

AsyncUdsClient::RequestErrorStage AsyncUdsClient::DecodeErrorStage(int32_t errCode)
{
    switch (static_cast<RequestErrorStage>(errCode)) {
        case RequestErrorStage::CONNECT:
        case RequestErrorStage::SEND:
        case RequestErrorStage::RECV:
        case RequestErrorStage::PARSE:
            return static_cast<RequestErrorStage>(errCode);
        default:
            return RequestErrorStage::NONE;
    }
}

long AsyncUdsClient::ElapsedMs(const RequestContext *ctx)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx->startTime).count();
}

void AsyncUdsClient::CleanupFd(RequestContext *ctx)
{
    if (ctx->fd >= 0) {
        (void)ctx->evloop->DelFdEvent(ctx->fd);
        (void)::close(ctx->fd);
        ctx->fd = -1;
    }
    delete ctx;
}

litebus::Future<nlohmann::json> AsyncUdsClient::RequestAsync(const RequestOptions &opts)
{
    litebus::Promise<nlohmann::json> promise;
    auto future = promise.GetFuture();

    if (!InitEvLoop()) {
        YRLOG_ERROR("{} connect failed: {} {} ({})", opts.logTag, opts.method, opts.path, opts.socketPath);
        promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        return future;
    }

    if (!opts.parser) {
        YRLOG_ERROR("{} refused: empty response parser: {} {} ({})", opts.logTag, opts.method, opts.path,
                    opts.socketPath);
        promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        return future;
    }

    auto *ctx = new (std::nothrow) RequestContext();
    if (ctx == nullptr) {
        YRLOG_ERROR("{} connect failed: {} {} ({})", opts.logTag, opts.method, opts.path, opts.socketPath);
        promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        return future;
    }
    ctx->promise = std::move(promise);
    ctx->socketPath = opts.socketPath;
    ctx->parser = opts.parser;
    ctx->method = opts.method;
    ctx->fullPath = opts.path;
    ctx->logTag = opts.logTag;
    ctx->startTime = std::chrono::steady_clock::now();
    ctx->httpRequest = AsyncUdsClient::BuildHttpRequest(opts.method, opts.path, opts.body.dump());
    ctx->evloop = &EvLoopRef();
    if (ctx->httpRequest.empty()) {
        YRLOG_ERROR("{} connect failed: {} {} ({})", ctx->logTag, ctx->method, ctx->fullPath, ctx->socketPath);
        ctx->promise.SetFailed(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
        delete ctx;
        return future;
    }

    bool connected = StartConnect(ctx);
    if (connected) {
        if (ctx->evloop->AddFdEvent(ctx->fd, EPOLLOUT, &AsyncUdsClient::OnWritable, ctx) != 0) {
            YRLOG_ERROR("{} connect failed: {} {} ({}, elapsed={}ms)", ctx->logTag, ctx->method, ctx->fullPath,
                ctx->socketPath, ElapsedMs(ctx));
            FailRequest(ctx, FailStage::CONNECT);
            return future;
        }
    } else if (ctx->fd < 0) {
        FailRequest(ctx, FailStage::CONNECT);
        return future;
    }
    return future;
}

}  // namespace functionsystem::runtime_manager
