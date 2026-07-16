/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "common/utils/port_forward_mapping.h"

#include "gtest/gtest.h"

namespace functionsystem {
namespace {

TEST(PortForwardMappingTest, ParsesCanonicalRouteKinds)
{
    auto direct = ParsePortForwardMapping("direct+http:40001:50090");
    ASSERT_TRUE(direct.has_value());
    EXPECT_EQ(direct->routeKind, PortRouteKind::DIRECT);
    EXPECT_EQ(direct->backendScheme, "http");
    EXPECT_EQ(direct->hostPort, 40001);
    EXPECT_EQ(direct->containerPort, 50090);

    auto tunnel = ParsePortForwardMapping("tunnel+http:40002:8765");
    ASSERT_TRUE(tunnel.has_value());
    EXPECT_EQ(tunnel->routeKind, PortRouteKind::TUNNEL);
    EXPECT_EQ(tunnel->backendScheme, "http");
}

TEST(PortForwardMappingTest, ParsesPublicAndLegacyFormats)
{
    auto https = ParsePortForwardMapping("public+https:40003:8443");
    ASSERT_TRUE(https.has_value());
    EXPECT_EQ(https->routeKind, PortRouteKind::PUBLIC);
    EXPECT_EQ(https->backendScheme, "https");

    auto old = ParsePortForwardMapping("40004:8080");
    ASSERT_TRUE(old.has_value());
    EXPECT_EQ(old->backendScheme, "http");

    auto tcp = ParsePortForwardMapping("tcp:40005:7000");
    ASSERT_TRUE(tcp.has_value());
    EXPECT_TRUE(tcp->legacyTransport);
    EXPECT_EQ(tcp->backendScheme, "http");

    EXPECT_TRUE(ParsePortForwardMapping("direct:40006:50090").has_value());
    EXPECT_TRUE(ParsePortForwardMapping("tunnel:40007:8765").has_value());
}

TEST(PortForwardMappingTest, RejectsUnsupportedOrInvalidMappings)
{
    EXPECT_FALSE(ParsePortForwardMapping("direct+udp:40001:53").has_value());
    EXPECT_FALSE(ParsePortForwardMapping("unknown+http:40001:80").has_value());
    EXPECT_FALSE(ParsePortForwardMapping("http:0:80").has_value());
    EXPECT_FALSE(ParsePortForwardMapping("http:40001:65536").has_value());
    EXPECT_FALSE(ParsePortForwardMapping("a:b:c:d").has_value());
}

TEST(PortForwardMappingTest, FormatsCanonicalMappings)
{
    EXPECT_EQ(FormatPortForwardMapping({PortRouteKind::DIRECT, "http", 40001, 50090, false}),
              "direct+http:40001:50090");
    EXPECT_EQ(FormatPortForwardMapping({PortRouteKind::TUNNEL, "https", 40002, 8765, false}),
              "tunnel+https:40002:8765");
    EXPECT_EQ(FormatPortForwardMapping({PortRouteKind::PUBLIC, "http", 40003, 8080, false}),
              "public+http:40003:8080");
}

}  // namespace
}  // namespace functionsystem
