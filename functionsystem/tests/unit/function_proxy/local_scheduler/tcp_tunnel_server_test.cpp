/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

// FS-T1: the tcp.tunnel header parser must accept the new optional correlation
// fields (traceID/traceparent) and stay compatible with legacy headers that do
// not carry them.

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "function_proxy/local_scheduler/tcp_tunnel_server.h"

namespace functionsystem::test {
using namespace local_scheduler;

namespace {

constexpr int kTestTunnelVersion = 1;

nlohmann::json BuildFullHeader()
{
    return nlohmann::json{
        { "tunnelVersion", kTestTunnelVersion },
        { "instanceID", "inst-tunnel-1" },
        { "protocol", "tcp" },
        { "targetPort", 18092 },
        { "requestID", "req-tunnel-1" },
        { "traceID", "trace-tunnel-1" },
        { "traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01" },
    };
}

}  // namespace

TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderAcceptsFullHeader)
{
    TunnelRequest request;
    std::string error;
    ASSERT_TRUE(ParseTunnelRequestHeader(BuildFullHeader(), request, error)) << error;
    EXPECT_EQ(request.instanceID, "inst-tunnel-1");
    EXPECT_EQ(request.requestID, "req-tunnel-1");
    EXPECT_EQ(request.targetPort, 18092);
    EXPECT_EQ(request.traceID, "trace-tunnel-1");
    EXPECT_EQ(request.traceParent, "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
}

TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderAcceptsLegacyHeaderWithoutTraceFields)
{
    auto header = nlohmann::json{
        { "tunnelVersion", kTestTunnelVersion },
        { "instanceID", "inst-legacy" },
        { "protocol", "tcp" },
        { "targetPort", 18092 },
        { "requestID", "req-legacy" },
    };

    TunnelRequest request;
    std::string error;
    ASSERT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
    EXPECT_EQ(request.instanceID, "inst-legacy");
    EXPECT_EQ(request.requestID, "req-legacy");
    // The correlation fields are optional: absent on old frontends, empty here.
    EXPECT_TRUE(request.traceID.empty());
    EXPECT_TRUE(request.traceParent.empty());
}

// Each correlation field is independently optional — a frontend may carry only
// a trace id or only a traceparent; the parser must not require them in pairs.
TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderAcceptsSingleCorrelationField)
{
    {
        auto header = BuildFullHeader();
        header.erase("traceparent");
        TunnelRequest request;
        std::string error;
        ASSERT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
        EXPECT_EQ(request.traceID, "trace-tunnel-1");
        EXPECT_TRUE(request.traceParent.empty());
    }
    {
        auto header = BuildFullHeader();
        header.erase("traceID");
        TunnelRequest request;
        std::string error;
        ASSERT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
        EXPECT_TRUE(request.traceID.empty());
        EXPECT_EQ(request.traceParent, "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
    }
}

// Present-but-empty correlation fields parse like absent ones (old frontends
// omit the keys; new frontends may send empty strings when tracing is off).
TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderAcceptsEmptyCorrelationFields)
{
    auto header = BuildFullHeader();
    header["traceID"] = "";
    header["traceparent"] = "";
    TunnelRequest request;
    std::string error;
    ASSERT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
    EXPECT_TRUE(request.traceID.empty());
    EXPECT_TRUE(request.traceParent.empty());
}

// Length bounds are per-field: traceID 64 (X-Trace-Id is an arbitrary
// uuid-ish string), traceparent 55 (W3C), identifiers 128. The exact boundary
// (bound passes, bound+1 rejects) is pinned so a silent bound change fails here.
TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderEnforcesPerFieldLengthBounds)
{
    {
        auto header = BuildFullHeader();
        header["traceID"] = std::string(64, 'a');  // at the bound: passes
        TunnelRequest request;
        std::string error;
        EXPECT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
    }
    {
        auto header = BuildFullHeader();
        header["traceID"] = std::string(65, 'a');  // one over: rejects
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        // "Exactly 55 passes" is already covered by
        // ParseTunnelRequestHeaderAcceptsFullHeader: BuildFullHeader's traceparent
        // is the real 55-character W3C value, so this block only pins the over-bound.
        auto header = BuildFullHeader();
        header["traceparent"] = std::string(56, 'a');  // one over: rejects
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    // Identifier bounds are symmetric: at the bound passes, one over rejects —
    // for instanceID and requestID alike.
    {
        auto header = BuildFullHeader();
        header["instanceID"] = std::string(128, 'i');
        TunnelRequest request;
        std::string error;
        EXPECT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
    }
    {
        auto header = BuildFullHeader();
        header["requestID"] = std::string(128, 'r');
        TunnelRequest request;
        std::string error;
        EXPECT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
    }
    {
        auto header = BuildFullHeader();
        header["instanceID"] = std::string(129, 'i');  // identifier over its bound
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        auto header = BuildFullHeader();
        header["requestID"] = std::string(129, 'r');
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
}

// Malformed values (control characters that could forge log lines) must reject
// the request for EVERY logged field — traceID/traceparent and the identifier
// fields alike. '\x00' and '\t' additionally cover non-CRLF control bytes.
TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderRejectsMalformedCorrelationFields)
{
    {
        auto header = BuildFullHeader();
        header["traceID"] = "trace\r\nforged-log-line";  // CR/LF injection via traceID
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        auto header = BuildFullHeader();
        header["traceparent"] = "00-4bf9\r\nforged-log-line";  // CR/LF injection via traceparent
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        auto header = BuildFullHeader();
        header["instanceID"] = "inst\nforged-log-line";  // injection via a logged identifier
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        auto header = BuildFullHeader();
        header["requestID"] = "req\tforged-log-line";  // other control byte
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
    {
        auto header = BuildFullHeader();
        header["requestID"] = std::string("req\0forged", 11);  // NUL byte
        TunnelRequest request;
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
    }
}

TEST(TcpTunnelServerHeaderTest, ParseTunnelRequestHeaderRejectsInvalidHeaders)
{
    auto makeHeader = []() { return BuildFullHeader(); };
    // Every rejected parse must also leave the out-param untouched (reset on
    // entry; fields are only written after ALL checks pass), so a caller can
    // never act on attacker-controlled leftovers.
    auto expectRejected = [](const nlohmann::json &header) {
        TunnelRequest request {};  // value-initialized: deterministic baseline
        std::string error;
        EXPECT_FALSE(ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(error.empty());
        // No field may carry a value from the rejected header (nor a stale value):
        // fields are only written after ALL checks pass.
        EXPECT_TRUE(request.instanceID.empty());
        EXPECT_TRUE(request.requestID.empty());
        EXPECT_EQ(request.targetPort, 0);
        EXPECT_TRUE(request.traceID.empty());
        EXPECT_TRUE(request.traceParent.empty());
    };

    {
        auto header = makeHeader();
        header.erase("instanceID");
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header.erase("requestID");  // required field missing entirely
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header["tunnelVersion"] = kTestTunnelVersion + 1;
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header.erase("protocol");
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header.erase("targetPort");  // required field missing entirely
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header["targetPort"] = "not-a-number";
        // A type-mismatched field throws inside nlohmann; the contract is that
        // the exception is caught and surfaces as a parse error — never as an
        // uncaught exception escaping the parser. Capture the result first so
        // the error assertion does not fire on an unrelated path.
        bool parsed = true;
        TunnelRequest request {};
        std::string error;
        EXPECT_NO_THROW(parsed = ParseTunnelRequestHeader(header, request, error));
        EXPECT_FALSE(parsed);
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(request.targetPort, 0);
    }
    {
        auto header = makeHeader();
        header["targetPort"] = 0;  // port 0 is not a valid target
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header["targetPort"] = -1;  // negative port
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header["targetPort"] = 65536;  // one past the valid range
        expectRejected(header);
    }
    {
        auto header = makeHeader();
        header["targetPort"] = 65535;  // top of the valid range passes
        TunnelRequest request {};
        std::string error;
        EXPECT_TRUE(ParseTunnelRequestHeader(header, request, error)) << error;
        EXPECT_EQ(request.targetPort, 65535);
    }
}

}  // namespace functionsystem::test
