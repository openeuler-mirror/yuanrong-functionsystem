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

/**
 * End-to-end test for litebus dual-port feature.
 *
 * Scenario:
 *   - litebus starts with TWO TCP listeners:
 *       external port (extPort)  — external TLS or plaintext
 *       local port    (localPort) — plaintext on 127.0.0.1, for same-node callers
 *   - An HttpActor is registered on "/probe" that reads and echoes back the
 *     value of the X-Internal-Src header it received.
 *   - curl sends GET /probe to:
 *       (A) local port  → response body must contain "internal=1"
 *       (B) external port → response body must contain "internal=0"
 *
 * This validates that `http_iomgr.cpp` injects X-Internal-Src: 1 only for
 * connections accepted on the local listener fd.
 *
 * Note: This binary must be run in isolation (its own process) because
 *       litebus::Initialize is guarded by a one-shot atomic_bool.
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <curl/curl.h>
#include <gtest/gtest.h>
#include <signal.h>

#include "actor/buslog.hpp"
#include "async/future.hpp"
#include "executils.hpp"
#include "httpd/http_actor.hpp"
#include "httpd/http_sysmgr.hpp"
#include "litebus.hpp"
#include "utils/os_utils.hpp"

using namespace litebus;
using namespace litebus::http;
using namespace std::chrono_literals;

/* ─────────────────────────────────────────────────────────────
 * Utilities
 * ───────────────────────────────────────────────────────────── */

static int FindFreePort()
{
    int port = litebus::find_available_port();
    EXPECT_GT(port, 0) << "find_available_port() returned invalid port: " << port;
    return port;
}

static size_t CurlWriteCb(void *ptr, size_t size, size_t nmemb, std::string *s)
{
    s->append(static_cast<char *>(ptr), size * nmemb);
    return size * nmemb;
}

/* Perform an HTTP GET request to `url` and return (httpCode, body). */
static std::pair<long, std::string> HttpGet(const std::string &url)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return {-1, ""};
    }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }
    curl_easy_cleanup(curl);
    return {code, body};
}

/* Perform an HTTP GET with a single extra request header. */
static std::pair<long, std::string> HttpGetWithHeader(const std::string &url, const std::string &header)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        return {-1, ""};
    }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_slist *hdrs = curl_slist_append(nullptr, header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return {code, body};
}

/* ─────────────────────────────────────────────────────────────
 * ProbeActor — echoes X-Internal-Src header value in response
 * ───────────────────────────────────────────────────────────── */

class ProbeActor : public HttpActor {
public:
    explicit ProbeActor(const std::string &name) : HttpActor(name) {}

private:
    void Init() override
    {
        BUSLOG_INFO("ProbeActor::Init()");
        AddRoute("/probe", &ProbeActor::HandleProbe);
    }

    /**
     * Reads the X-Internal-Src header from the request (injected by http_iomgr
     * if the connection arrived on the local listener) and returns it in the body.
     * The body format is "internal=<value>" where value is "1" if injected, "0" otherwise.
     */
    Future<Response> HandleProbe(const Request &request)
    {
        auto it = request.headers.find("X-Internal-Src");
        std::string value = (it != request.headers.end()) ? it->second : "0";
        BUSLOG_INFO("ProbeActor: X-Internal-Src={}", value);
        return Response(ResponseCode::OK, "internal=" + value);
    }
};

/* ─────────────────────────────────────────────────────────────
 * Wait helper: poll until condition is true or timeout
 * ───────────────────────────────────────────────────────────── */

static bool WaitUntil(std::function<bool()> cond, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

/* ─────────────────────────────────────────────────────────────
 * Test fixture
 * ───────────────────────────────────────────────────────────── */

class DualPortE2ETest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        // Allocate two distinct ports; retry until they differ to avoid a
        // FindFreePort() race where OS reuses the same port for both calls.
        // Bound the retry count to prevent an infinite loop.
        extPort_ = FindFreePort();
        constexpr int kMaxPortRetries = 100;
        int portRetries = 0;
        do {
            localPort_ = FindFreePort();
            ++portRetries;
        } while (localPort_ == extPort_ && portRetries < kMaxPortRetries);
        ASSERT_LT(portRetries, kMaxPortRetries) << "Failed to find two distinct free ports after 100 attempts";

        std::string extUrl   = "tcp://127.0.0.1:" + std::to_string(extPort_);
        std::string localUrl = "tcp://127.0.0.1:" + std::to_string(localPort_);

        BUSLOG_INFO("DualPortE2ETest: extUrl={} localUrl={}", extUrl, localUrl);

        litebus::LitebusInitOptions opts;
        opts.tcpUrl = extUrl;
        opts.tcpUrlAdv = extUrl;
        opts.threadCount = 4;
        opts.tcpLocalUrl = localUrl;
        opts.tcpLocalUrlAdv = localUrl;
        int rc = litebus::Initialize(opts);
        ASSERT_EQ(rc, BUS_OK) << "litebus::Initialize failed: " << rc;
        initialized_ = true;

        /* Spawn the probe actor and the HTTP sysmgr */
        litebus::Spawn(std::make_shared<ProbeActor>("ProbeActor"));
        litebus::Spawn(std::make_shared<litebus::http::HttpSysMgr>("SysManager"));

        /* Wait until both ports return HTTP 200 before running any test.
         * This replaces the previous fixed 200ms sleep which was unreliable
         * on loaded CI environments. */
        auto extProbeUrl   = "http://127.0.0.1:" + std::to_string(extPort_)   + "/ProbeActor/probe";
        auto localProbeUrl = "http://127.0.0.1:" + std::to_string(localPort_) + "/ProbeActor/probe";
        bool ready = WaitUntil([&] {
            return HttpGet(extProbeUrl).first == 200 && HttpGet(localProbeUrl).first == 200;
        }, 10s);
        ASSERT_TRUE(ready) << "Timed out waiting for both ports to become ready";
    }

    static void TearDownTestSuite()
    {
        if (initialized_) {
            litebus::TerminateAll();
            litebus::Finalize();
        }
    }

    /* Port numbers chosen at startup */
    static int extPort_;
    static int localPort_;
    static bool initialized_;
};

int  DualPortE2ETest::extPort_   = 0;
int  DualPortE2ETest::localPort_ = 0;
bool DualPortE2ETest::initialized_ = false;

/* ─────────────────────────────────────────────────────────────
 * E2E Tests
 * ───────────────────────────────────────────────────────────── */

/**
 * Feature: Local port injects X-Internal-Src: 1
 * Description: HTTP GET to the local (127.0.0.1) plaintext port must cause
 *              http_iomgr to inject X-Internal-Src: 1 before the request
 *              reaches the handler. ProbeActor echoes it in the body.
 * Expected: response body == "internal=1"
 */
TEST_F(DualPortE2ETest, LocalPortInjectsInternalSrcHeader)
{
    std::string url = "http://127.0.0.1:" + std::to_string(localPort_) + "/ProbeActor/probe";

    std::pair<long, std::string> result;
    bool ok = WaitUntil([&] {
        result = HttpGet(url);
        return result.first == 200 && result.second.find("internal=1") != std::string::npos;
    }, 5s);

    EXPECT_TRUE(ok) << "Expected 'internal=1' in response from local port; "
                    << "HTTP " << result.first << " body=" << result.second;
}

/**
 * Feature: External port does NOT inject X-Internal-Src
 * Description: HTTP GET to the external (regular) port must NOT have
 *              X-Internal-Src: 1 injected. ProbeActor returns "internal=0".
 * Expected: response body == "internal=0"
 */
TEST_F(DualPortE2ETest, ExternalPortDoesNotInjectInternalSrcHeader)
{
    std::string url = "http://127.0.0.1:" + std::to_string(extPort_) + "/ProbeActor/probe";

    std::pair<long, std::string> result;
    bool ok = WaitUntil([&] {
        result = HttpGet(url);
        return result.first == 200 && result.second.find("internal=0") != std::string::npos;
    }, 5s);

    EXPECT_TRUE(ok) << "Expected 'internal=0' in response from external port; "
                    << "HTTP " << result.first << " body=" << result.second;
}

/**
 * Feature: Both ports serve concurrent requests independently
 * Description: When both ports receive a request simultaneously, each must
 *              independently produce the correct header injection behaviour.
 * Expected: local port response has "internal=1"; external port has "internal=0"
 */
TEST_F(DualPortE2ETest, BothPortsServeRequestsConcurrently)
{
    std::string localUrl = "http://127.0.0.1:" + std::to_string(localPort_) + "/ProbeActor/probe";
    std::string extUrl   = "http://127.0.0.1:" + std::to_string(extPort_)   + "/ProbeActor/probe";

    std::pair<long, std::string> localResult, extResult;

    /* Both ports are proven ready by SetUpTestSuite; a single concurrent pair
     * of requests is sufficient.  Threads are created once here, not inside a
     * retry loop, to avoid spawning O(N) threads on transient failures. */
    std::thread localThread([&] { localResult = HttpGet(localUrl); });
    std::thread extThread([&] { extResult = HttpGet(extUrl); });
    localThread.join();
    extThread.join();

    EXPECT_EQ(localResult.first, 200)
        << "local port HTTP error; body=" << localResult.second;
    EXPECT_NE(localResult.second.find("internal=1"), std::string::npos)
        << "local port response missing internal=1 flag; body=" << localResult.second;
    EXPECT_EQ(extResult.first, 200)
        << "external port HTTP error; body=" << extResult.second;
    EXPECT_NE(extResult.second.find("internal=0"), std::string::npos)
        << "external port response missing internal=0 flag; body=" << extResult.second;
}

/**
 * Feature: Local port returns HTTP 200 OK
 * Description: The local listener must be a fully functional HTTP server
 *              returning correct status codes.
 * Expected: HTTP 200 from the local port
 */
TEST_F(DualPortE2ETest, LocalPortReturnsHttp200)
{
    std::string url = "http://127.0.0.1:" + std::to_string(localPort_) + "/ProbeActor/probe";

    std::pair<long, std::string> result;
    bool ok = WaitUntil([&] {
        result = HttpGet(url);
        return result.first == 200;
    }, 5s);
    EXPECT_TRUE(ok) << "Local port returned unexpected status; code=" << result.first << " body=" << result.second;
}

/**
 * Feature: External port returns HTTP 200 OK
 * Description: The external listener must be a fully functional HTTP server
 *              returning correct status codes.
 * Expected: HTTP 200 from the external port
 */
TEST_F(DualPortE2ETest, ExternalPortReturnsHttp200)
{
    std::string url = "http://127.0.0.1:" + std::to_string(extPort_) + "/ProbeActor/probe";

    std::pair<long, std::string> result;
    bool ok = WaitUntil([&] {
        result = HttpGet(url);
        return result.first == 200;
    }, 5s);
    EXPECT_TRUE(ok) << "External port returned unexpected status; code=" << result.first << " body=" << result.second;
}

/**
 * Feature: External port strips forged X-Internal-Src header
 * Description: A malicious client that sends "X-Internal-Src: 1" to the external port
 *              must have the header stripped by the server before it reaches any actor.
 *              The ProbeActor must therefore still report "internal=0".
 * Expected: response body == "internal=0" even when the client supplies the header
 */
TEST_F(DualPortE2ETest, ExternalPortStripsForgedInternalSrcHeader)
{
    std::string url = "http://127.0.0.1:" + std::to_string(extPort_) + "/ProbeActor/probe";

    std::pair<long, std::string> result;
    bool ok = WaitUntil([&] {
        result = HttpGetWithHeader(url, "X-Internal-Src: 1");
        return result.first == 200 && result.second.find("internal=0") != std::string::npos;
    }, 5s);

    EXPECT_TRUE(ok) << "External port must strip a client-forged X-Internal-Src header; "
                    << "HTTP " << result.first << " body=" << result.second;
}

/* ─────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    curl_global_init(CURL_GLOBAL_ALL);

    int result = RUN_ALL_TESTS();

    curl_global_cleanup();
    return result;
}
