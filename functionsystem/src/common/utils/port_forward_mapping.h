/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef FUNCTIONSYSTEM_COMMON_UTILS_PORT_FORWARD_MAPPING_H
#define FUNCTIONSYSTEM_COMMON_UTILS_PORT_FORWARD_MAPPING_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace functionsystem {

enum class PortRouteKind {
    PUBLIC,
    DIRECT,
    TUNNEL,
};

struct PortForwardMapping {
    PortRouteKind routeKind = PortRouteKind::PUBLIC;
    std::string backendScheme = "http";
    uint16_t hostPort = 0;
    uint16_t containerPort = 0;
    // Parse-only compatibility marker used for diagnostics. Formatting always
    // emits the canonical routeKind+backendScheme representation.
    bool legacyTransport = false;
};

std::optional<PortRouteKind> ParsePortRouteKind(std::string_view value);
std::optional<PortForwardMapping> ParsePortForwardMapping(std::string_view value);
std::string FormatPortForwardMapping(const PortForwardMapping &mapping);

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_COMMON_UTILS_PORT_FORWARD_MAPPING_H
