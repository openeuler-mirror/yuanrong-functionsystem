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


#include "network_tool.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#include <regex>

#include "common/logs/logging.h"
#include "common/utils.h"
#include "common/utils/exec_utils.h"

namespace functionsystem::function_agent {
const std::string IPV4_REGULAR_EXPRESSION =
    "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$";

const std::string CIDR_IPV4_REGULAR_EXPRESSION =
    "^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}"
    "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(/([0-9]|[1-2][0-9]|3[0-2]))?$";

std::unordered_map<std::string, Addr> NetworkTool::GetAddrs()
{
    std::unordered_map<std::string, Addr> addrs;
    struct ifaddrs *ifap;
    struct ifaddrs *ifa;
    (void)getifaddrs(&ifap);

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }

        char netmask[BUFFER_SIZE] = { '\0' };
        char ifaAddr[BUFFER_SIZE] = { '\0' };

        // set netmask
        (void)inet_ntop(
            ifa->ifa_addr->sa_family,
            ifa->ifa_addr->sa_family == AF_INET
                ? reinterpret_cast<void *>(&(reinterpret_cast<struct sockaddr_in *>(ifa->ifa_netmask))->sin_addr)
                : reinterpret_cast<void *>(&(reinterpret_cast<struct sockaddr_in6 *>(ifa->ifa_netmask))->sin6_addr),
            netmask, BUFFER_SIZE);

        // set ip address
        (void)inet_ntop(
            ifa->ifa_addr->sa_family,
            ifa->ifa_addr->sa_family == AF_INET
                ? reinterpret_cast<void *>(&(reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr))->sin_addr)
                : reinterpret_cast<void *>(&(reinterpret_cast<struct sockaddr_in6 *>(ifa->ifa_addr))->sin6_addr),
            ifaAddr, BUFFER_SIZE);

        if (auto addr = std::string(ifaAddr); !addr.empty()) {
            addrs[addr] = Addr{ addr, std::string(netmask), std::string(ifa->ifa_name) };
        }
    }

    freeifaddrs(ifap);
    return addrs;
}

litebus::Option<Addr> NetworkTool::GetAddr(const std::string &ip)
{
    auto addrs = GetAddrs();
    if (addrs.find(ip) == addrs.end()) {
        return litebus::None();
    }

    return addrs[ip];
}

bool NetworkTool::SetFirewall(const FirewallConfig &config)
{
    std::string command = "iptables -w";

    // set table
    if (config.table.empty()) {
        YRLOG_ERROR("firewall config table is empty.");
        return false;
    }
    command += " -t " + config.table;

    // set operation in chain
    if (config.operation.empty() || config.chain.empty() ||
        !std::regex_match(config.chain, std::regex("^(PREROUTING|INPUT|FORWARDING|OUTPUT|POSTROUTIONG)$"))) {
        YRLOG_ERROR("firewall config operation/chain is invalid.");
        return false;
    }
    if (config.operation == "add") {
        // set destination
        if (config.target.empty() || !std::regex_match(config.target, std::regex(IPV4_REGULAR_EXPRESSION))) {
            YRLOG_ERROR("firewall config target {} is invalid.", config.target);
            return false;
        }
        command += " -I " + config.chain;
        command += " -d " + config.target;
    } else if (config.operation == "delete") {
        command += " -D " + config.chain + " 1";
    } else {
        YRLOG_ERROR("firewall config operation {} is invalid.", config.operation);
        return false;
    }

    // set args
    if (std::regex_match(config.args, std::regex("^([a-zA-Z\\s\\-]+)$"))) {
        command += config.args;
    } else {
        YRLOG_ERROR("firewall config arguments {} are invalid.", config.args);
        return false;
    }

    if (!CheckIllegalChars(command)) {
        return false;
    }

    if (auto result = std::system(command.c_str()); result) {
        YRLOG_ERROR("set firewall failed. error message: {}", result);
        return false;
    }
    return true;
}

std::vector<std::string> NetworkTool::GetRouteConfig(const std::string &cidr)
{
    if (cidr.empty() || !std::regex_match(cidr, std::regex(CIDR_IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("cidr({}) is invalid", cidr);
        return std::vector<std::string>{};
    }
    std::string command = "ip route show " + cidr;

    if (!CheckIllegalChars(command)) {
        YRLOG_ERROR("failed to check illegal chars of command");
        return std::vector<std::string>{};
    }
    auto result = ExecuteCommand(command);
    if (!result.error.empty()) {
        YRLOG_ERROR("execute command {} failed, error: {}", command, result.error);
        return std::vector<std::string>{};
    }

    YRLOG_INFO("command {} output is {}", command, result.output);
    return litebus::strings::Split(result.output, "\n");
}

bool NetworkTool::SetRoute(const RouteConfig &config)
{
    std::string deleteCmd = "ip route del";
    std::string addCmd = "ip route add";

    // set cidr
    if (config.cidr.empty() || !std::regex_match(config.cidr, std::regex(CIDR_IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("route config cidr {} is invalid.", config.cidr);
        return false;
    }
    deleteCmd += " " + config.cidr;
    addCmd += " " + config.cidr;

    // set dev
    if (config.interface.empty()) {
        YRLOG_ERROR("route config interface is empty.");
        return false;
    }
    deleteCmd += " dev " + config.interface;
    addCmd += " dev " + config.interface;

    // set gateway
    if (config.gateway.empty() || !std::regex_match(config.gateway, std::regex(IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("route config gateway {} is invalid.", config.gateway);
        return false;
    }
    addCmd += " via " + config.gateway;
    auto localConfig = GetRouteConfig(config.cidr);

    if (!CheckIllegalChars(addCmd) || !CheckIllegalChars(deleteCmd)) {
        return false;
    }

    if (auto result = std::system(deleteCmd.c_str()); result) {
        YRLOG_WARN("delete route failed. error message: {}", result);
    }

    if (auto result = ExecuteCommand(addCmd); !result.error.empty()) {
        YRLOG_ERROR("add route failed. error message: {}", result.error);
        (void)RestoreRoute(localConfig);
        if (result.error.find("Network is unreachable") != std::string::npos ||
            result.error.find("Nexthop has invalid gateway") != std::string::npos) {
            return true;
        }
        return false;
    }

    return true;
}

bool NetworkTool::RestoreRoute(const std::vector<std::string> &config)
{
    std::string restoreCmd = "ip route add ";
    for (const auto &route : config) {
        if (route.empty()) {
            continue;
        }

        if (!CheckIllegalChars(restoreCmd + route)) {
            return false;
        }

        if (auto result = std::system((restoreCmd + route).c_str()); result) {
            YRLOG_ERROR("restore route {} failed. error message: {}", route, result);
        }
    }

    return true;
}

bool NetworkTool::SetTunnel(const TunnelConfig &config)
{
    std::string deleteCmd = "ip tunnel del";
    std::string addCmd = "ip tunnel add";

    // set tunnelName
    if (config.tunnelName.empty()) {
        YRLOG_ERROR("tunnel config tunnelName is empty.");
        return false;
    }
    deleteCmd += " " + config.tunnelName;
    addCmd += " " + config.tunnelName;

    // set mode
    if (config.mode.empty()) {
        YRLOG_ERROR("tunnel config mode is empty.");
        return false;
    }
    addCmd += " mode " + config.mode;

    // set local ip
    if (config.localIP.empty() || !std::regex_match(config.localIP, std::regex(IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("tunnel config local ip {} is invalid.", config.localIP);
        return false;
    }
    addCmd += " local " + config.localIP;

    // set remote ip
    if (config.remoteIP.empty() || !std::regex_match(config.remoteIP, std::regex(IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("tunnel config remote ip {} is invalid.", config.remoteIP);
        return false;
    }
    addCmd += " remote " + config.remoteIP;

    std::string setupCmd = "ip link set " + config.tunnelName + " up";

    if (!CheckIllegalChars(addCmd) || !CheckIllegalChars(deleteCmd) || !CheckIllegalChars(setupCmd)) {
        return false;
    }

    if (auto result = std::system(deleteCmd.c_str()); result) {
        YRLOG_WARN("delete tunnel failed, error message: {}", result);
    }

    if (auto result = std::system(addCmd.c_str()); result) {
        YRLOG_ERROR("add tunnel failed, error message: {}", result);
        return false;
    }

    if (auto result = std::system(setupCmd.c_str()); result) {
        YRLOG_ERROR("setup tunnel failed, error message: {}", result);
        (void)ExecuteCommand(deleteCmd);
        return false;
    }
    return true;
}

std::vector<std::string> NetworkTool::GetNameServerList()
{
    std::vector<std::string> nameservers;
    auto content = litebus::os::Read("/etc/resolv.conf");
    if (content.IsNone()) {
        return nameservers;
    }

    auto lines = litebus::strings::Split(content.Get(), "\n");
    size_t nameserverLen = 2;
    std::regex e("^nameserver\\s+([0-9.]+)\\s*$");
    for (auto &line : lines) {
        if (!std::regex_match(line, e)) {
            continue;
        }

        auto words = Field(line, ' ');
        if (words.size() != nameserverLen) {
            continue;
        }
        nameservers.push_back(words[1]);
    }

    return nameservers;
}

bool NetworkTool::Ping(const ProberConfig &config)
{
    std::string command = "ping";
    if (config.address.empty() || !std::regex_match(config.address, std::regex(IPV4_REGULAR_EXPRESSION))) {
        YRLOG_ERROR("config address {} is invalid.", config.address);
        return false;
    }
    // If the timeout period is too long, ping commands may be suspended for a long time, especially when the
    // network is unreachable.
    if (int32_t maxTimeout = 60; config.timeout <= 0 || config.timeout > maxTimeout) {
        YRLOG_ERROR("config timeout({}) is invalid.", config.timeout);
        return false;
    }
    if (int32_t maxInterval = 60; config.interval <= 0 || config.interval > maxInterval) {
        YRLOG_ERROR("config interval({}) is invalid.", config.interval);
        return false;
    }
    if (int32_t maxFailureThreshold = 3;
        config.failureThreshold <= 0 || config.failureThreshold > maxFailureThreshold) {
        YRLOG_ERROR("config failureThreshold({}) is invalid.", config.failureThreshold);
        return false;
    }

    command += " " + config.address;
    command += " -i " + std::to_string(config.interval);
    command += " -W " + std::to_string(config.timeout);
    command += " -D -c 1";

    for (int i = 0; i < config.failureThreshold; i++) {
        if (!CheckIllegalChars(command)) {
            return false;
        }
        if (auto res = std::system(command.c_str()); !res) {
            return true;
        }
    }

    YRLOG_ERROR("ping failed to {}.", config.address);
    return false;
}

bool NetworkTool::Probe(const std::vector<ProberConfig> &configs)
{
    for (const auto &config : configs) {
        if (config.protocol == ICMP_PROTOCOL && Ping(config)) {
            YRLOG_DEBUG("ping {} success.", config.address);
            return true;
        }
        YRLOG_ERROR("probe {} failed.", config.address);
    }
    return false;
}

std::vector<NetworkConfig> NetworkTool::ParseNetworkConfig(const std::string &str)
{
    nlohmann::json parser;
    std::vector<NetworkConfig> networkConfigs;
    try {
        parser = nlohmann::json::parse(str);
    } catch (std::exception &error) {
        YRLOG_WARN("parse network configs {} failed, error: {}", str, error.what());
        return networkConfigs;
    } catch (...) {
        YRLOG_WARN("parse network configs {} failed, error: {}", str);
        return networkConfigs;
    }

    for (auto &j : parser) {
        NetworkConfig networkConfig;
        if (j.find(ROUTE_CONFIG) != j.end() && j.at(ROUTE_CONFIG).is_object()) {
            networkConfig.routeConfig = ParseRouteConfig(j.at(ROUTE_CONFIG));
        }
        if (j.find(TUNNEL_CONFIG) != j.end() && j.at(TUNNEL_CONFIG).is_object()) {
            networkConfig.tunnelConfig = ParseTunnelConfig(j.at(TUNNEL_CONFIG));
        }

        if (j.find(FIREWALL_CONFIG) != j.end() && j.at(FIREWALL_CONFIG).is_object()) {
            networkConfig.firewallConfig = ParseFirewallConfig(j.at(FIREWALL_CONFIG));
        }
        networkConfigs.push_back(networkConfig);
    }

    return networkConfigs;
}

RouteConfig NetworkTool::ParseRouteConfig(const nlohmann::json &j)
{
    RouteConfig routeConfig;
    if (j.find("gateway") != j.end() && j.at("gateway").is_string()) {
        routeConfig.gateway = j.at("gateway");
    }
    if (j.find("cidr") != j.end() && j.at("cidr").is_string() && !j.at("cidr").empty()) {
        routeConfig.cidr = j.at("cidr");
    } else {
        routeConfig.cidr = "default";
    }
    return routeConfig;
}

TunnelConfig NetworkTool::ParseTunnelConfig(const nlohmann::json &j)
{
    TunnelConfig tunnelConfig;
    if (j.find("tunnelName") != j.end() && j.at("tunnelName").is_string()) {
        tunnelConfig.tunnelName = j.at("tunnelName");
    }
    if (j.find("remoteIP") != j.end() && j.at("remoteIP").is_string()) {
        tunnelConfig.remoteIP = j.at("remoteIP");
    }
    if (j.find("mode") != j.end() && j.at("mode").is_string()) {
        tunnelConfig.mode = j.at("mode");
    }
    return tunnelConfig;
}

FirewallConfig NetworkTool::ParseFirewallConfig(const nlohmann::json &j)
{
    FirewallConfig firewallConfig;
    if (j.find("chain") != j.end() && j.at("chain").is_string()) {
        firewallConfig.chain = j.at("chain");
    }
    if (j.find("table") != j.end() && j.at("table").is_string()) {
        firewallConfig.table = j.at("table");
    }
    if (j.find("operation") != j.end() && j.at("operation").is_string()) {
        firewallConfig.operation = j.at("operation");
    }
    if (j.find("target") != j.end() && j.at("target").is_string()) {
        firewallConfig.target = j.at("target");
    }
    if (j.find("args") != j.end() && j.at("args").is_string()) {
        firewallConfig.args = j.at("args");
    }
    return firewallConfig;
}

std::vector<ProberConfig> NetworkTool::ParseProberConfig(const std::string &str)
{
    nlohmann::json parser;
    std::vector<ProberConfig> proberConfigs;
    try {
        parser = nlohmann::json::parse(str);
    } catch (std::exception &error) {
        YRLOG_WARN("parse prober configs {} failed, error: {}", str, error.what());
        return proberConfigs;
    } catch (...) {
        YRLOG_WARN("parse prober configs {} failed, error: {}", str);
        return proberConfigs;
    }

    for (auto &j : parser) {
        ProberConfig proberConfig;
        if (j.find("protocol") != j.end() && j.at("protocol").is_string()) {
            proberConfig.protocol = j.at("protocol");
        }
        if (j.find("address") != j.end() && j.at("address").is_string()) {
            proberConfig.address = j.at("address");
        }
        if (j.find("interval") != j.end() && j.at("interval").is_number()) {
            proberConfig.interval = j.at("interval").get<int32_t>();
        }
        if (j.find("timeout") != j.end() && j.at("timeout").is_number()) {
            proberConfig.timeout = j.at("timeout").get<int32_t>();
        }
        if (j.find("failureThreshold") != j.end() && j.at("failureThreshold").is_number()) {
            proberConfig.failureThreshold = j.at("failureThreshold").get<int32_t>();
        }
        proberConfigs.push_back(proberConfig);
    }

    return proberConfigs;
}

bool NetworkTool::IsIpsetExists(const std::string &ipsetName)
{
    std::string command = "ipset list " + ipsetName;
    auto result = ExecuteCommand(command);
    if (!result.error.empty()) {
        YRLOG_ERROR("ipset({}) is not exist, error message: {}", ipsetName, result.error);
        return false;
    }
    return (result.output.find("Name:") != std::string::npos);
}

}  // namespace functionsystem::function_agent