/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#include "port_forward_mapping.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <vector>

namespace functionsystem {
namespace {

std::string Lower(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::optional<uint16_t> ParsePort(std::string_view value)
{
    uint32_t port = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
    if (result.ec != std::errc() || result.ptr != value.data() + value.size() || port == 0 || port > 65535) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(port);
}

std::vector<std::string_view> Split(std::string_view value, char separator)
{
    std::vector<std::string_view> parts;
    size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(separator, begin);
        parts.emplace_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return parts;
}

std::optional<std::pair<PortRouteKind, std::string>> ParseRouteToken(std::string_view token,
                                                                     bool &legacyTransport)
{
    const auto normalized = Lower(token);
    const auto kindAndScheme = Split(normalized, '+');
    if (kindAndScheme.size() == 1) {
        if (normalized == "http" || normalized == "https") {
            return std::make_pair(PortRouteKind::PUBLIC, normalized);
        }
        if (normalized == "tcp") {
            legacyTransport = true;
            return std::make_pair(PortRouteKind::PUBLIC, std::string("http"));
        }
        if (normalized == "direct") {
            return std::make_pair(PortRouteKind::DIRECT, std::string("http"));
        }
        if (normalized == "tunnel") {
            return std::make_pair(PortRouteKind::TUNNEL, std::string("http"));
        }
        return std::nullopt;
    }
    if (kindAndScheme.size() != 2 || (kindAndScheme[1] != "http" && kindAndScheme[1] != "https")) {
        return std::nullopt;
    }

    PortRouteKind kind;
    if (kindAndScheme[0] == "direct") {
        kind = PortRouteKind::DIRECT;
    } else if (kindAndScheme[0] == "tunnel") {
        kind = PortRouteKind::TUNNEL;
    } else if (kindAndScheme[0] == "public") {
        kind = PortRouteKind::PUBLIC;
    } else {
        return std::nullopt;
    }
    return std::make_pair(kind, std::string(kindAndScheme[1]));
}

}  // namespace

std::optional<PortRouteKind> ParsePortRouteKind(std::string_view value)
{
    const auto normalized = Lower(value);
    if (normalized == "public") {
        return PortRouteKind::PUBLIC;
    }
    if (normalized == "direct") {
        return PortRouteKind::DIRECT;
    }
    if (normalized == "tunnel") {
        return PortRouteKind::TUNNEL;
    }
    return std::nullopt;
}

std::optional<PortForwardMapping> ParsePortForwardMapping(std::string_view value)
{
    const auto parts = Split(value, ':');
    std::string_view routeToken = "http";
    std::string_view hostPortText;
    std::string_view containerPortText;
    if (parts.size() == 2) {
        hostPortText = parts[0];
        containerPortText = parts[1];
    } else if (parts.size() == 3) {
        routeToken = parts[0];
        hostPortText = parts[1];
        containerPortText = parts[2];
    } else {
        return std::nullopt;
    }

    const auto hostPort = ParsePort(hostPortText);
    const auto containerPort = ParsePort(containerPortText);
    if (!hostPort.has_value() || !containerPort.has_value()) {
        return std::nullopt;
    }

    bool legacyTransport = false;
    const auto route = ParseRouteToken(routeToken, legacyTransport);
    if (!route.has_value()) {
        return std::nullopt;
    }
    return PortForwardMapping{route->first, route->second, *hostPort, *containerPort, legacyTransport};
}

std::string FormatPortForwardMapping(const PortForwardMapping &mapping)
{
    std::string routeKind;
    switch (mapping.routeKind) {
        case PortRouteKind::DIRECT:
            routeKind = "direct";
            break;
        case PortRouteKind::TUNNEL:
            routeKind = "tunnel";
            break;
        case PortRouteKind::PUBLIC:
            routeKind = "public";
            break;
        default:
            return {};
    }
    const auto inputScheme = Lower(mapping.backendScheme);
    std::string backendScheme = inputScheme;
    if (inputScheme == "tcp" || inputScheme == "ws") {
        backendScheme = "http";
    } else if (inputScheme == "wss") {
        backendScheme = "https";
    }
    const std::string routeToken = routeKind + "+" + backendScheme;
    return routeToken + ":" + std::to_string(mapping.hostPort) + ":" + std::to_string(mapping.containerPort);
}

}  // namespace functionsystem
