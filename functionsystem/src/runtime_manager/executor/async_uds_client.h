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

#ifndef RUNTIME_MANAGER_EXECUTOR_ASYNC_UDS_CLIENT_H
#define RUNTIME_MANAGER_EXECUTOR_ASYNC_UDS_CLIENT_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "async/future.hpp"
#include "evloop/evloop.hpp"
#include "nlohmann/json.hpp"

namespace functionsystem::runtime_manager {

// AsyncUdsClient: process-singleton async HTTP-over-UDS client. Offloads
// connect/send/recv to a dedicated EvLoop (epoll) thread, returning Future<json>
// so the actor thread is never blocked on IO. EvLoop::Init uses pthread_create
// (not fork), so no new process is created.
//
// Promise::SetValue/SetFailed is spin-lock protected, so resolving the Future
// from the EvLoop thread is safe. Callers MUST consume the returned Future via
// Defer(GetAID(), ...) so map writes happen on the actor thread; a bare lambda
// .OnComplete runs on the EvLoop thread and races with actor-thread map reads.
class AsyncUdsClient {
public:
    using ResponseParser = std::function<nlohmann::json(const std::string &rawResponse)>;

    struct RequestOptions {
        std::string socketPath;
        std::string method;
        std::string path;
        nlohmann::json body;
        std::string logTag;
        ResponseParser parser;
    };

    static litebus::Future<nlohmann::json> RequestAsync(const RequestOptions &opts);

    static std::string BuildHttpRequest(const std::string &method, const std::string &path,
                                        const std::string &body);

    // Failure stage encoded into the Future error code so callers can classify the failure
    // (connect/send/recv/parse). Values chosen in a range that won't collide with StatusCode.
    enum class RequestErrorStage : int32_t {
        NONE = 0,
        CONNECT = 0x4C434E00,  // "LCN\0" — connect
        SEND = 0x4C534E00,     // "LSN\0" — send
        RECV = 0x4C524E00,     // "LRN\0" — recv
        PARSE = 0x4C504500,    // "LPE\0" — parse
    };
    static RequestErrorStage DecodeErrorStage(int32_t errCode);

    static bool InitEvLoop();
    static void FinishEvLoop();

private:
    AsyncUdsClient() = default;
    ~AsyncUdsClient() = default;

    enum class FailStage { CONNECT, SEND, RECV, PARSE };
    struct RequestContext {
        litebus::Promise<nlohmann::json> promise;
        std::string httpRequest;
        std::string response;
        std::string socketPath;
        std::string method;
        std::string fullPath;
        std::string logTag;
        ResponseParser parser;
        std::chrono::steady_clock::time_point startTime;
        size_t sendOffset = 0;
        size_t contentLength = 0;
        bool hasContentLength = false;
        bool headerParsed = false;
        int fd = -1;
        litebus::EvLoop *evloop = nullptr;
    };

    // void *data mandated by EvLoop EventHandler signature (evloop.hpp: EventHandler = void(*)(int, uint32_t, void*))
    static void OnWritable(int fd, uint32_t /* events */, void *data);        // NOLINT(g.FUN.05-cpp)
    static void OnReadable(int fd, uint32_t /* events */, void *data);        // NOLINT(g.FUN.05-cpp)
    static void OnConnectWritable(int fd, uint32_t /* events */, void *data); // NOLINT(g.FUN.05-cpp)

    static bool StartConnect(RequestContext *ctx);
    static bool DoSend(RequestContext *ctx);
    static void ParseHeaderIfReady(RequestContext *ctx);
    static bool IsResponseComplete(const RequestContext *ctx);
    static void CompleteRequest(RequestContext *ctx);
    static void FailRequest(RequestContext *ctx, FailStage stage);
    static void CleanupFd(RequestContext *ctx);
    static long ElapsedMs(const RequestContext *ctx);

    static litebus::EvLoop &EvLoopRef();
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_ASYNC_UDS_CLIENT_H
