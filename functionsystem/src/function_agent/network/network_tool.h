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

#ifndef FUNCTION_AGENT_NETWORK_NETWORK_H
#define FUNCTION_AGENT_NETWORK_NETWORK_H

#include <async/option.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace functionsystem::function_agent {

struct Addr {
    std::string ip;
    std::string netmask;
    std::string interface;
};

struct FirewallConfig {
    std::string chain;
    std::string table;
    std::string operation;
    std::string target;
    std::string args;
};

struct RouteConfig {
    std::string gateway;
    std::string cidr;
    std::string interface;
};

struct TunnelConfig {
    std::string tunnelName;
    std::string remoteIP;
    std::string mode;
    std::string localIP;
};

struct NetworkConfig {
    litebus::Option<RouteConfig> routeConfig;
    litebus::Option<TunnelConfig> tunnelConfig;
    litebus::Option<FirewallConfig> firewallConfig;
};

struct ProberConfig {
    std::string protocol;
    std::string address;
    int32_t interval = 0;
    int32_t timeout = 0;
    int32_t failureThreshold = 0;
};

const unsigned int BUFFER_SIZE = 200;
const std::string ICMP_PROTOCOL = "ICMP";

const std::string NETWORK_CONFIG = "networkConfig";
const std::string ROUTE_CONFIG = "routeConfig";
const std::string TUNNEL_CONFIG = "tunnelConfig";
const std::string FIREWALL_CONFIG = "firewallConfig";
const std::string PROBER_CONFIG = "proberConfig";

class NetworkTool {
public:
    static std::unordered_map<std::string, Addr> GetAddrs();
    static litebus::Option<Addr> GetAddr(const std::string &ip);
    static bool SetFirewall(const FirewallConfig &config);
    static bool SetRoute(const RouteConfig &config);
    static bool RestoreRoute(const std::vector<std::string> &config);
    static bool SetTunnel(const TunnelConfig &config);
    static std::vector<std::string> GetNameServerList();
    static std::vector<std::string> GetRouteConfig(const std::string &cidr);
    static bool Ping(const ProberConfig &config);
    static std::vector<NetworkConfig> ParseNetworkConfig(const std::string &str);
    static std::vector<ProberConfig> ParseProberConfig(const std::string &str);
    static bool Probe(const std::vector<ProberConfig> &configs);
    static bool IsIpsetExists(const std::string& ipsetName);
private:
    static RouteConfig ParseRouteConfig(const nlohmann::json &j);
    static TunnelConfig ParseTunnelConfig(const nlohmann::json &j);
    static FirewallConfig ParseFirewallConfig(const nlohmann::json &j);
};

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_NETWORK_NETWORK_H
