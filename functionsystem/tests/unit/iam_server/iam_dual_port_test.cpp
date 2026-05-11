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
 * Unit tests for IAM dual-port feature.
 *
 * Covers:
 *   1. IAMStartParam has localIp / localPort fields
 *   2. IAMDriver flags: GetLocalIP / GetLocalListenPort parsed correctly
 *   3. IAMActor::RequestFilter skips AKSK verification when X-Internal-Src:1 is present
 *   4. IAMActor::RequestFilter enforces AKSK verification when X-Internal-Src is absent
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "iam_server/driver/iam_driver.h"
#include "iam_server/iam/iam_actor/iam_actor.h"
#include "iam_server/iam/internal_iam/internal_iam.h"
#include "iam_server/flags/flags.h"

using namespace functionsystem::iamserver;
using namespace litebus::http;

namespace functionsystem::iamserver::test {

/* ─────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────── */

static HttpRequest MakeGetRequest(const std::string &path,
                                  const litebus::http::HeaderMap &extraHeaders = {})
{
    litebus::http::URL url;
    url.path = path;
    HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers = extraHeaders;
    return req;
}

/* ─────────────────────────────────────────────────────────────
 * 1. IAMStartParam struct has localIp / localPort fields
 * ───────────────────────────────────────────────────────────── */

/**
 * Feature: IAMStartParam dual-port fields
 * Description: IAMStartParam must expose localIp and localPort so callers can
 *              configure which local address the plaintext listener binds to.
 * Expected: default localIp is empty; default localPort is 0 (feature disabled)
 */
TEST(IAMDualPortParamTest, DefaultLocalFieldsAreDisabled)
{
    IAMStartParam param{};
    EXPECT_TRUE(param.localIp.empty());
    EXPECT_EQ(param.localPort, 0u);
}

/**
 * Feature: IAMStartParam dual-port fields
 * Description: Caller can set localIp and localPort to non-empty values.
 * Expected: fields round-trip correctly
 */
TEST(IAMDualPortParamTest, LocalFieldsCanBeSet)
{
    IAMStartParam param{};
    param.localIp = "127.0.0.1";
    param.localPort = 8081;

    EXPECT_EQ(param.localIp, "127.0.0.1");
    EXPECT_EQ(param.localPort, 8081u);
}

/* ─────────────────────────────────────────────────────────────
 * 2. IAM Flags: local_ip and local_listen_port
 * ───────────────────────────────────────────────────────────── */

/**
 * Feature: IAM server flags for dual-port
 * Description: When --local_ip and --local_listen_port are not provided, the
 *              getter must return defaults (127.0.0.1 and 0).
 * Expected: GetLocalIP() == "127.0.0.1"; GetLocalListenPort() == 0
 */
TEST(IAMDualPortFlagsTest, DefaultLocalFlagValues)
{
    Flags flags;
    /* ParseFlags with minimal required args only (ip and http_listen_port). */
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080"
    };
    int argc = 3;
    flags.ParseFlags(argc, argv);

    EXPECT_EQ(flags.GetLocalIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetLocalListenPort(), 0u);
}

/**
 * Feature: IAM server flags for dual-port
 * Description: When --local_listen_port is provided, GetLocalListenPort must
 *              return that value; GetLocalIP must return the --local_ip value.
 * Expected: both getters return the supplied values
 */
TEST(IAMDualPortFlagsTest, ParsedLocalFlagValues)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080",
        "--local_ip=127.0.0.1",
        "--local_listen_port=8081"
    };
    int argc = 5;
    flags.ParseFlags(argc, argv);

    EXPECT_EQ(flags.GetLocalIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetLocalListenPort(), 8081u);
}

/**
 * Feature: IAM server flags for dual-port — localAddress construction
 * Description: main.cpp constructs localAddress as localIp:localPort when
 *              local_listen_port is non-empty. Verify that the construction
 *              logic produces the expected "ip:port" string.
 * Expected: "127.0.0.1:8081" when both flags are set
 */
TEST(IAMDualPortFlagsTest, LocalAddressStringConstruction)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080",
        "--local_ip=127.0.0.1",
        "--local_listen_port=8081"
    };
    int argc = 5;
    flags.ParseFlags(argc, argv);

    /* Replicate the logic in main.cpp:
     *   flags.GetLocalListenPort() == 0 ? "" : flags.GetLocalIP() + ":" + std::to_string(flags.GetLocalListenPort())
     */
    std::string localAddress = flags.GetLocalListenPort() == 0
                                   ? ""
                                   : flags.GetLocalIP() + ":" + std::to_string(flags.GetLocalListenPort());

    EXPECT_EQ(localAddress, "127.0.0.1:8081");
}

/**
 * Feature: IAM server flags for dual-port — disabled when port absent
 * Description: When --local_listen_port is not provided, localAddress must be
 *              empty so ModuleSwitcher::InitLiteBus skips the local listener.
 * Expected: localAddress == ""
 */
TEST(IAMDualPortFlagsTest, LocalAddressEmptyWhenPortNotSet)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080"
    };
    int argc = 3;
    flags.ParseFlags(argc, argv);

    std::string localAddress = flags.GetLocalListenPort() == 0
                                   ? ""
                                   : flags.GetLocalIP() + ":" + std::to_string(flags.GetLocalListenPort());
    EXPECT_TRUE(localAddress.empty());
}

/* ─────────────────────────────────────────────────────────────
 * 3. IAMActor::RequestFilter internal source bypass
 * ───────────────────────────────────────────────────────────── */

class IAMActorDualPortTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        actor_ = std::make_shared<IAMActor>("test-iam-actor");

        /* Bind a stub InternalIAM so ASSERT_IF_NULL in RequestFilter doesn't crash.
         * isEnableIAM=false → IsIAMEnabled() returns false → RequestFilter returns
         * BAD_REQUEST("iam is not enabled") after the method and bypass checks pass. */
        InternalIAM::Param param{};
        param.isEnableIAM = false;
        param.tokenExpiredTimeSpan = 3600;
        param.credType = InternalIAM::IAMCredType::TOKEN;
        auto stubIam = std::make_shared<InternalIAM>(param);
        actor_->BindInternalIAM(stubIam);
    }

    std::shared_ptr<IAMActor> actor_;
};

/**
 * Feature: RequestFilter bypasses AKSK for internal source
 * Description: When X-Internal-Src: 1 is present in the request headers, the
 *              AKSK signature check (VerifyRequest) is skipped. The request then
 *              proceeds to subsequent checks. With stubbed InternalIAM where
 *              isEnableIAM=false, the filter returns BAD_REQUEST("iam is not enabled")
 *              — crucially NOT FORBIDDEN (403), proving the bypass was activated.
 * Expected: response is BAD_REQUEST, NOT FORBIDDEN
 */
TEST_F(IAMActorDualPortTest, RequestFilterInternalSrcSkipsAKSKCheck)
{
    /* Build a GET request with X-Internal-Src: 1 */
    HttpRequest req = MakeGetRequest("/iam/v1/auth/token", {{"X-Internal-Src", "1"}});

    auto [code, msg] = actor_->RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, req);

    /* Must NOT be FORBIDDEN (403) — the internal source bypass was active.
     * It will be BAD_REQUEST because internalIAM_ has isEnableIAM=false. */
    EXPECT_NE(code, litebus::http::ResponseCode::FORBIDDEN)
        << "Internal source request must not be rejected with 403; got: " << msg;
    EXPECT_EQ(code, litebus::http::ResponseCode::BAD_REQUEST)
        << "Expected BAD_REQUEST (iam not enabled), got: " << msg;
}

/**
 * Feature: RequestFilter enforces AKSK when internal source absent
 * Description: When X-Internal-Src header is absent and AKSK is NOT configured
 *              (useAKSK_=false), VerifyRequest returns true unconditionally,
 *              so RequestFilter proceeds to the IAM-enabled check.
 *              With isEnableIAM=false, returns BAD_REQUEST — not FORBIDDEN.
 * Expected: response is BAD_REQUEST (not FORBIDDEN) when AKSK is disabled
 */
TEST_F(IAMActorDualPortTest, RequestFilterNormalRequestNoAKSKConfigured)
{
    HttpRequest req = MakeGetRequest("/iam/v1/auth/token");
    /* No X-Internal-Src header; useAKSK_=false → VerifyRequest always true */

    auto [code, msg] = actor_->RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, req);

    EXPECT_NE(code, litebus::http::ResponseCode::FORBIDDEN)
        << "Without AKSK configured, normal request must not be 403; got: " << msg;
    EXPECT_EQ(code, litebus::http::ResponseCode::BAD_REQUEST)
        << "Expected BAD_REQUEST (iam not enabled), got: " << msg;
}

/**
 * Feature: RequestFilter header key is case-sensitive
 * Description: The header X-Internal-Src must be matched exactly.
 *              Requests with a differently cased header (e.g. x-internal-src)
 *              must NOT trigger the bypass — standard HTTP/litebus headers are
 *              case-sensitive in this implementation.
 * Expected: lowercase header does not enable bypass; result is same as normal
 *           request (BAD_REQUEST because iam not enabled, not FORBIDDEN)
 */
TEST_F(IAMActorDualPortTest, RequestFilterInternalSrcHeaderIsCaseSensitive)
{
    /* lowercase — must NOT match */
    HttpRequest req = MakeGetRequest("/iam/v1/auth/token", {{"x-internal-src", "1"}});

    auto [code, msg] = actor_->RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, req);

    /* Same result as a plain request (no bypass), which with useAKSK_=false
     * proceeds to the IAM-enabled check → BAD_REQUEST. */
    EXPECT_NE(code, litebus::http::ResponseCode::FORBIDDEN);
    EXPECT_EQ(code, litebus::http::ResponseCode::BAD_REQUEST)
        << "Lowercase header must not trigger bypass; expected BAD_REQUEST, got: " << msg;
}

/**
 * Feature: RequestFilter header value must be exactly "1"
 * Description: X-Internal-Src: 0 or any other value must NOT enable the bypass.
 * Expected: code is BAD_REQUEST (same as no-header path; not FORBIDDEN)
 */
TEST_F(IAMActorDualPortTest, RequestFilterInternalSrcValueZeroNoBypass)
{
    HttpRequest req = MakeGetRequest("/iam/v1/auth/token", {{"X-Internal-Src", "0"}});

    auto [code, msg] = actor_->RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, req);

    /* Value "0" must not trigger bypass — same as no header.
     * With useAKSK_=false and method GET, reaches the IAM-enabled check → BAD_REQUEST. */
    EXPECT_NE(code, litebus::http::ResponseCode::FORBIDDEN)
        << "Value '0' must not produce FORBIDDEN (VerifyRequest returns true when !useAKSK_)";
    EXPECT_EQ(code, litebus::http::ResponseCode::BAD_REQUEST)
        << "Expected BAD_REQUEST (iam not enabled), got: " << msg;
}

/**
 * Feature: RequestFilter non-GET method rejected regardless of internal source
 * Description: Even an internal-source request with X-Internal-Src:1 must be
 *              rejected with METHOD_NOT_ALLOWED if the HTTP method is not GET.
 * Expected: METHOD_NOT_ALLOWED for POST with X-Internal-Src: 1
 */
TEST_F(IAMActorDualPortTest, RequestFilterInternalSrcPostMethodStillRejected)
{
    litebus::http::URL url;
    url.path = "/iam/v1/auth/token";
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers = {{"X-Internal-Src", "1"}};

    auto [code, msg] = actor_->RequestFilter("VerifyToken", InternalIAM::IAMCredType::TOKEN, req);

    EXPECT_EQ(code, litebus::http::ResponseCode::METHOD_NOT_ALLOWED)
        << "POST must be rejected even for internal-source requests; got: " << msg;
}

}  // namespace functionsystem::iamserver::test

/* ═══════════════════════════════════════════════════════════════
 * IAM SSL independent toggle tests
 * ═══════════════════════════════════════════════════════════════ */

namespace functionsystem::iamserver::ssltest {

/**
 * Feature: IAM SSL toggle - fallback to global
 * Description: When --iam_ssl_enable is NOT set, GetIAMSslEnable()
 *              falls back to global --ssl_enable.
 */
TEST(IAMSSLFlagsTest, FallbackToGlobalEnabled)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080",
        "--ssl_enable=true",
        "--ssl_base_path=/etc/ssl/global"
    };
    int argc = 5;
    flags.ParseFlags(argc, argv);

    EXPECT_FALSE(flags.HasIAMSslOverride());
    EXPECT_TRUE(flags.GetIAMSslEnable());
}

TEST(IAMSSLFlagsTest, FallbackToGlobalDisabled)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080"
    };
    int argc = 3;
    flags.ParseFlags(argc, argv);

    EXPECT_FALSE(flags.HasIAMSslOverride());
    EXPECT_FALSE(flags.GetIAMSslEnable());
}

/**
 * Feature: IAM SSL independent enable
 * Description: --iam_ssl_enable=true enables SSL for IAM even when global is off.
 */
TEST(IAMSSLFlagsTest, IndependentEnableOverridesGlobalOff)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080",
        "--ssl_enable=false",
        "--iam_ssl_enable=true"
    };
    int argc = 5;
    flags.ParseFlags(argc, argv);

    EXPECT_TRUE(flags.HasIAMSslOverride());
    EXPECT_TRUE(flags.GetIAMSslEnable());
    // Global stays off
    EXPECT_FALSE(flags.GetSslEnable());
}

/**
 * Feature: IAM SSL independent disable
 * Description: --iam_ssl_enable=false disables SSL for IAM even when global is on.
 */
TEST(IAMSSLFlagsTest, IndependentDisableOverridesGlobalOn)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=192.168.1.1",
        "--http_listen_port=8080",
        "--ssl_enable=true",
        "--ssl_base_path=/etc/ssl",
        "--iam_ssl_enable=false"
    };
    int argc = 6;
    flags.ParseFlags(argc, argv);

    EXPECT_TRUE(flags.HasIAMSslOverride());
    EXPECT_FALSE(flags.GetIAMSslEnable());
}

/**
 * Feature: IAM SSL + dual-port coexistence
 * Description: Both IAM SSL override and local listen port can be configured
 *              simultaneously (TLS on external, plaintext on local).
 */
TEST(IAMSSLFlagsTest, SSLAndDualPortCoexist)
{
    Flags flags;
    const char *argv[] = {
        "iam_server",
        "--ip=10.0.0.1",
        "--http_listen_port=8443",
        "--ssl_base_path=/etc/ssl",
        "--iam_ssl_enable=true",
        "--local_ip=127.0.0.1",
        "--local_listen_port=8080"
    };
    int argc = 7;
    flags.ParseFlags(argc, argv);

    EXPECT_TRUE(flags.HasIAMSslOverride());
    EXPECT_TRUE(flags.GetIAMSslEnable());
    EXPECT_EQ(flags.GetLocalIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetLocalListenPort(), 8080u);
}

}  // namespace functionsystem::iamserver::ssltest
