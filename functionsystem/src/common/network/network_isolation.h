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

#ifndef FUNCTIONSYSTEM_NETWORK_ISOLATION_H
#define FUNCTIONSYSTEM_NETWORK_ISOLATION_H

#include <memory>
#include <vector>
#include <string>

#include "common/utils/exec_utils.h"

namespace functionsystem {

// Rule is the struct of rule,
// for ipset ipv4 rule like "10.0.0.1", Rule is string
// for iptales ipv6 rule like "iptables -A INPUT -s 2001:db8::1 -j DROP", Rule can be a struct
//     IptablesRule { chain, sourceAddress, target }
// for taskgroup of sandbox isolation rule like "taskGroup1: enth89, eth90;", Rule can be a struct
//     TaskGroupRule { taskGroupName, interfaces }
template <typename Rule>
class NetworkIsolation {
public:
    NetworkIsolation() = default;
    virtual ~NetworkIsolation() = default;

    virtual std::vector<Rule> GetAllRules() = 0;
    virtual int AddRule(Rule rule) = 0;
    virtual int RemoveRule(Rule rule) = 0;
    virtual int RemoveAllRules() = 0;
};  // class NetworkIsolation

// IpsetIpv4NetworkIsolation utilizes the rule in form of IPv4 string type.
// a rule like "10.0.0.1" to be added to a specific ipset: "ipset add podip-whitelist 10.0.0.1"
class IpsetIpv4NetworkIsolation : public NetworkIsolation<std::string> {
public:
    IpsetIpv4NetworkIsolation() = default;
    explicit IpsetIpv4NetworkIsolation(std::string ipsetName) : ipsetName_(ipsetName)
    {
        commandRunner_ = std::make_shared<CommandRunner>();
    }
    ~IpsetIpv4NetworkIsolation() override
    {}

    /**
     * @brief Retrieve all rules
     * @return Returning 0 indicates success, while non-zero indicates failure
     */
    std::vector<std::string> GetAllRules() override;

    /**
     * @brief Create new rule
     * @param ip IPv4 string
     * @return Returning 0 indicates success, while non-zero indicates failure
     */
    int AddRule(std::string ip) override;

    /**
     * @brief Create multiple rules
     * @param ips IPv4 strings
     * @return Returning 0 indicates success, while non-zero indicates failure
     */
    int AddRules(std::vector<std::string> ips);

    /**
     * @brief Delete rule
     * @param ip IPv4 string
     * @return Returning 0 indicates success, while non-zero indicates failure
     */
    int RemoveRule(std::string ip) override;

    /**
     * @brief Remove all rules
     * @return Returning 0 indicates success, while non-zero indicates failure
     */
    int RemoveAllRules() override;

    /**
     * @brief Determine if ipset is present.
     * @return Returning true indicates existence, while false indicates non-existence
     */
    bool IsIpsetExist();

    std::string GetIpsetName() const
    {
        return ipsetName_;
    }

    void SetIpsetName(const std::string &ipsetName)
    {
        ipsetName_ = ipsetName;
    }

    // for test
    [[maybe_unused]] void SetCommandRunner(const std::shared_ptr<CommandRunner> &commandRunner)
    {
        commandRunner_ = commandRunner;
    }

private:
    std::string ipsetName_;
    std::shared_ptr<CommandRunner> commandRunner_;
};  // class IpsetIpv4NetworkIsolation

}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_NETWORK_ISOLATION_H