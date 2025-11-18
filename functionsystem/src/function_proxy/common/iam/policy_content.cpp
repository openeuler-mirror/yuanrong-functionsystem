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

#include "policy_content.h"

#include "function_proxy/common/constants.h"
#include "nlohmann/json.hpp"

namespace functionsystem::function_proxy {
namespace {
const std::string TENANT_GROUP = "tenant_group";
const std::string POLICY = "policy";
const std::string ALLOW_POLICY = "allow";
const std::string DENY_POLICY = "deny";
const std::string DENY_POLICY_TENANT_LIST = "tenant_list";

const std::unordered_set<std::string> CALL_METHOD_SET = { CALL_METHOD_CREATE, CALL_METHOD_INVOKE, CALL_METHOD_KILL };

Status CheckPolicyFormatValid(const std::string &policyStr, nlohmann::json &policyJson)
{
    try {
        policyJson = nlohmann::json::parse(policyStr);
    } catch (nlohmann::json::parse_error &e) {
        return Status(StatusCode::FAILED,
                      "not a valid json, reason: " + std::string(e.what()) + ", id: " + std::to_string(e.id));
    }
    if (policyJson.find(TENANT_GROUP) == policyJson.end()) {
        return Status(StatusCode::FAILED, "TENANT_GROUP not found in policy.");
    }
    if (policyJson.find(WHITE_LIST) == policyJson.end()) {
        return Status(StatusCode::FAILED, "whitelist not found in policy.");
    }
    if (policyJson.find(POLICY) == policyJson.end()) {
        return Status(StatusCode::FAILED, "policy not found in policy.");
    }
    auto policy = policyJson.at(POLICY);
    if (policy.find(ALLOW_POLICY) == policy.end()) {
        return Status(StatusCode::FAILED, "allow rules not found in policy.");
    }
    if (policy.find(DENY_POLICY) == policy.end()) {
        return Status(StatusCode::FAILED, "deny rules not found in policy.");
    }
    return Status::OK();
}

void ParseTenantFuncGroup(const nlohmann::json &policyJson,
                          std::vector<PolicyContent::TenantFuncGroup> &tenantFuncGroupVec,
                          std::unordered_set<std::string> &funcGroupSet)
{
    for (const auto &iterGroup : policyJson.items()) {
        if (iterGroup.key().empty()) {
            continue;
        }
        (void)funcGroupSet.insert(iterGroup.key());
        for (const auto &iterTenant : iterGroup.value().items()) {
            if (iterTenant.key().empty()) {
                continue;
            }
            PolicyContent::TenantFuncGroup tenantFunc = { .funcGroup = iterGroup.key(),
                                                          .tenantID = iterTenant.key(),
                                                          .funcList = {} };
            if (!iterTenant.value().empty()) {
                tenantFunc.funcList = iterTenant.value().get<std::unordered_set<std::string>>();
            }
            (void)tenantFuncGroupVec.emplace_back(tenantFunc);
        }
    }
}

Status ParseWhiteList(const nlohmann::json &whiteListJson,
                      std::unordered_map<std::string, std::unordered_set<std::string>> &funcWhiteList)
{
    for (const auto &iterFuncName : whiteListJson.items()) {
        if (!iterFuncName.key().empty()) {
            if (!iterFuncName.value().empty()) {
                (void)funcWhiteList.insert({ iterFuncName.key(),
                                             iterFuncName.value().get<std::unordered_set<std::string>>() });
            }
        } else {
            return Status(StatusCode::FAILED, "func name in whitelist is empty.");
        }
    }
    return Status::OK();
}

Status IsRuleMethodValid(const nlohmann::json &ruleJson)
{
    for (const auto &iterMethod : ruleJson.items()) {
        if (!iterMethod.key().empty()) {
            if (CALL_METHOD_SET.find(iterMethod.key()) == CALL_METHOD_SET.end()) {
                return Status(StatusCode::FAILED, "call method is not allowed: " + iterMethod.key());
            }
        } else {
            return Status(StatusCode::FAILED, "call method is empty.");
        }
    }
    return Status::OK();
}

Status GetCalleeRuleGroup(const nlohmann::json &funcGroupJson,
                          PolicyContent::SingleRule &singleRule,
                          const std::unordered_set<std::string> &funcGroupSet)
{
    for (const auto &iterCalleeGroup : funcGroupJson.items()) {
        if (!iterCalleeGroup.key().empty()) {
            if (funcGroupSet.find(iterCalleeGroup.key()) == funcGroupSet.end()) {
                return Status(StatusCode::FAILED,
                              "function callee group in policy rule not exist in TENANT_GROUP, err group: " +
                              iterCalleeGroup.key());
            }
            if (!iterCalleeGroup.value().empty()) {
                (void)singleRule.insert({ iterCalleeGroup.key(),
                                          iterCalleeGroup.value().get<std::unordered_set<std::string>>() });
            }
        } else {
            return Status(StatusCode::FAILED, "func callee group is empty.");
        }
    }
    return Status::OK();
}

Status ParseCallerRule(const nlohmann::json &groupRule,
                       const std::string &method,
                       std::unordered_map<std::string, PolicyContent::CallerRule> &policyRuleContent,
                       const std::unordered_set<std::string> &funcGroupSet)
{
    PolicyContent::CallerRule callerRule;
    for (const auto &iterCallerGroup : groupRule.items()) {
        if (!iterCallerGroup.key().empty()) {
            if (funcGroupSet.find(iterCallerGroup.key()) == funcGroupSet.end()) {
                return Status(StatusCode::FAILED,
                              "parse func group rule failed, function caller group in policy rule not exist "
                              "in TENANT_GROUP, err group: " + iterCallerGroup.key());
            }
            PolicyContent::SingleRule singleRule;
            if (auto status(
                    GetCalleeRuleGroup(iterCallerGroup.value(), singleRule, funcGroupSet)); status.IsError()) {
                return Status(StatusCode::FAILED, "parse func group rule failed, err: " + status.ToString());
            }
            (void)callerRule.insert({ iterCallerGroup.key(), singleRule });
        } else {
            return Status(StatusCode::FAILED, "parse func group rule failed, err: caller group is empty.");
        }
    }
    (void)policyRuleContent.insert({ method, callerRule });
    return Status::OK();
}

Status ParseAllowRuleContent(const nlohmann::json &ruleJson,
                             std::unordered_map<std::string, PolicyContent::CallerRule> &policyRuleContent,
                             const std::unordered_set<std::string> &funcGroupSet)
{
    if (auto status(IsRuleMethodValid(ruleJson)); status.IsError()) {
        return Status(StatusCode::FAILED, "call method is invalid, err: " + status.ToString());
    }
    if (ruleJson.find(CALL_METHOD_INVOKE) != ruleJson.end()) {
        if (auto status(
                ParseCallerRule(ruleJson.at(CALL_METHOD_INVOKE), CALL_METHOD_INVOKE, policyRuleContent, funcGroupSet));
            status.IsError()) {
            return Status(StatusCode::FAILED, "parse allow invoke rule failed, err: " + status.ToString());
        }
    }
    if (ruleJson.find(CALL_METHOD_CREATE) != ruleJson.end()) {
        if (auto status(ParseCallerRule(
            ruleJson.at(CALL_METHOD_CREATE), CALL_METHOD_CREATE, policyRuleContent, funcGroupSet));
            status.IsError()) {
            return Status(StatusCode::FAILED, "parse allow create rule failed, err: " + status.ToString());
        }
    }
    if (ruleJson.find(CALL_METHOD_KILL) != ruleJson.end()) {
        if (auto status(ParseCallerRule(
            ruleJson.at(CALL_METHOD_KILL), CALL_METHOD_KILL, policyRuleContent, funcGroupSet)); status.IsError()) {
            return Status(StatusCode::FAILED, "parse allow kill rule failed, err: " + status.ToString());
        }
    }
    return Status::OK();
}
}

Status PolicyContent::UpdatePolicyContent(const std::string &policyStr)
{
    if (policyStr.empty()) {
        return Status(StatusCode::FAILED, "empty policy content");
    }

    nlohmann::json policyJson;
    if (auto status(CheckPolicyFormatValid(policyStr, policyJson)); status.IsError()) {
        return Status(StatusCode::FAILED, "policy content format is invalid, err: " + status.ToString());
    }

    // step 1. parse tenant function groups
    std::vector<TenantFuncGroup> funcGroups;
    std::unordered_set<std::string> funcGroupSet;
    ParseTenantFuncGroup(policyJson.at(TENANT_GROUP), funcGroups, funcGroupSet);
    // step 2. parse whitelist
    std::unordered_map<std::string, std::unordered_set<std::string>> funcWhiteList;
    if (auto status(ParseWhiteList(policyJson.at(WHITE_LIST), funcWhiteList)); status.IsError()) {
        return Status(StatusCode::FAILED, "parse whitelist failed, err: " + status.ToString());
    }
    // step 3. parse allow rules
    std::unordered_map<std::string, CallerRule> policyAllowRuleContent;
    if (auto status(ParseAllowRuleContent(
        policyJson.at(POLICY).at(ALLOW_POLICY), policyAllowRuleContent, funcGroupSet)); status.IsError()) {
        return Status(StatusCode::FAILED, "parse allow rule content failed, err: " + status.ToString());
    }
    // step 4. parse deny rules
    std::unordered_set<std::string> tenantBlackList;
    auto denyPolicyJson = policyJson.at(POLICY).at(DENY_POLICY);
    if (denyPolicyJson.find(DENY_POLICY_TENANT_LIST) != denyPolicyJson.end()) {
        tenantBlackList = denyPolicyJson.at(DENY_POLICY_TENANT_LIST).get<std::unordered_set<std::string>>();
    }
    {
        std::lock_guard<std::mutex> lock(policyContentMutex_);
        funcGroups_ = std::move(funcGroups);
        funcGroupSet_ = std::move(funcGroupSet);
        policyAllowRuleContent_ = std::move(policyAllowRuleContent);
        tenantBlackList_ = std::move(tenantBlackList);
        funcWhiteList_ = std::move(funcWhiteList);
    }
    return Status::OK();
}
} // namespace functionsystem::function_proxy