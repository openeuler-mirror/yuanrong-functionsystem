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

#include "network_isolation.h"
#include "common/logs/logging.h"
#include "common/utils/exec_utils.h"

namespace functionsystem {

void HandleIpsetListMembers(const functionsystem::CommandExecResult &result, std::vector<std::string> &rules)
{
    std::istringstream iss(result.output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("Members:") != std::string::npos) {
            while (std::getline(iss, line)) {
                rules.push_back(line);
            }
            break;
        }
    }
}

std::vector<std::string> IpsetIpv4NetworkIsolation::GetAllRules()
{
    std::vector<std::string> rules;
    std::string command = "ipset list " + ipsetName_;
    YRLOG_DEBUG("GetAllRules command: {}", command);
    if (!CheckIllegalChars(command)) {
        YRLOG_ERROR("failed to check illegal chars of command");
        return rules;
    }
    if (auto result = commandRunner_->ExecuteCommandWrapper(command); result.error.empty()) {
        HandleIpsetListMembers(result, rules);
    } else {
        YRLOG_ERROR("error message: {}", result.error);
    }
    return rules;
}

int IpsetIpv4NetworkIsolation::AddRule(std::string ip)
{
    std::string command = "ipset -exist add " + ipsetName_ + " " + ip;
    YRLOG_DEBUG("AddRule command: {}", command);
    return commandRunner_->CheckAndRunCommandWrapper(command) ? 0 : -1;
}

int IpsetIpv4NetworkIsolation::AddRules(std::vector<std::string> ips)
{
    std::string tempFileName = "/tmp/ipset_rules-" + litebus::uuid_generator::UUID::GetRandomUUID().ToString() + ".txt";
    std::ofstream tempFile(tempFileName);

    for (const std::string &ip : ips) {
        tempFile << "add " << ipsetName_ << " -exist " << ip << std::endl;
    }
    tempFile.close();

    std::string command = "ipset -f " + tempFileName + " restore";
    YRLOG_DEBUG("AddRules command: {}", command);
    int ret = 0;
    if (!commandRunner_->CheckAndRunCommandWrapper(command)) {
        ret = -1;
    }

    std::remove(tempFileName.c_str());
    return ret;
}

int IpsetIpv4NetworkIsolation::RemoveRule(std::string ip)
{
    std::string command = "ipset del " + ipsetName_ + " " + ip;
    YRLOG_DEBUG("RemoveRule command: {}", command);
    return commandRunner_->CheckAndRunCommandWrapper(command) ? 0 : -1;
}

int IpsetIpv4NetworkIsolation::RemoveAllRules()
{
    std::string command = "ipset flush " + ipsetName_;
    YRLOG_DEBUG("RemoveAllRules command: {}", command);
    return commandRunner_->CheckAndRunCommandWrapper(command) ? 0 : -1;
}

bool IpsetIpv4NetworkIsolation::IsIpsetExist()
{
    const std::string command = "ipset list " + GetIpsetName();
    if (!CheckIllegalChars(command)) {
        YRLOG_ERROR("failed to check illegal chars of command");
        return false;
    }
    auto [output, error] = commandRunner_->ExecuteCommandWrapper(command);
    return error.empty() && output.find("Name:") != std::string::npos;
}
}  // namespace functionsystem