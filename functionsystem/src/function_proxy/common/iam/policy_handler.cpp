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

#include "policy_handler.h"
#include "function_proxy/common/constants.h"

namespace functionsystem::function_proxy {
namespace {
const std::string POLICY_SYMBOL_SAME_TENANT = "=";
const std::string POLICY_SYMBOL_ALL_TENANT = "*";
const std::string EXTERNAL_TENANT_GROUP = "external";

Status CheckAuthorizeParamValid(const AuthorizeParam &authorizeParam)
{
    if (authorizeParam.callerTenantID.empty()) {
        return Status(StatusCode::FAILED,
                      "CheckAuthorizeParamValid failed, caller tenantID in authorizeParam is empty.");
    }
    if (authorizeParam.calleeTenantID.empty()) {
        return Status(StatusCode::FAILED,
                      "CheckAuthorizeParamValid failed, callee tenantID in authorizeParam is empty");
    }
    if (authorizeParam.callMethod.empty()) {
        return Status(StatusCode::FAILED, "CheckAuthorizeParamValid failed, callMethod in authorizeParam is empty");
    }
    return Status::OK();
}

std::string GetFuncGroupFromTenantID(const std::string &tenantID,
                                     const std::vector<PolicyContent::TenantFuncGroup> &funcGroups)
{
    for (const auto &iterFuncGroup : funcGroups) {
        if (iterFuncGroup.tenantID == tenantID) {
            return iterFuncGroup.funcGroup;
        }
    }
    return EXTERNAL_TENANT_GROUP;
}

Status IsWhiteListSatisfied(const std::unordered_map<std::string, std::unordered_set<std::string>> &funcWhiteList,
                            const AuthorizeParam &authorizeParam)
{
    auto iterFunc = funcWhiteList.find(authorizeParam.funcName);
    if (iterFunc->second.find(authorizeParam.callerTenantID) != iterFunc->second.end()) {
        return Status::OK();
    } else {
        return Status(StatusCode::FAILED, "tenantID: " + authorizeParam.callerTenantID +
                                          " not in the whitelist of function: " + authorizeParam.funcName);
    }
}

Status IsFuncInList(const std::unordered_set<std::string> &funcRuleList,
                    const AuthorizeParam &authorizeParam,
                    const std::unordered_map<std::string, std::unordered_set<std::string>> &funcWhiteList)
{
    if (funcRuleList.find(WHITE_LIST) != funcRuleList.end() &&
        funcWhiteList.find(authorizeParam.funcName) != funcWhiteList.end()) {
        return IsWhiteListSatisfied(funcWhiteList, authorizeParam);
    }
    // "*" means all functions are allowed to call
    if (funcRuleList.find(POLICY_SYMBOL_ALL_TENANT) != funcRuleList.end()) {
        return Status::OK();
    }
    // "=" means function call is allowed only when caller and callee has the same tenantID
    if (funcRuleList.find(POLICY_SYMBOL_SAME_TENANT) != funcRuleList.end()) {
        if (authorizeParam.callerTenantID == authorizeParam.calleeTenantID) {
            return Status::OK();
        } else {
            return Status(StatusCode::FAILED, "caller and callee not same, callerTenantID: " +
                authorizeParam.callerTenantID +", calleeTenantID: " + authorizeParam.calleeTenantID);
        }
    }
    // function call is allowed if function name is in the group
    if (funcRuleList.find(authorizeParam.funcName) != funcRuleList.end()) {
        return Status::OK();
    }
    return Status(StatusCode::FAILED, "no allow rule satisfied.");
}

Status IsAllowRuleSatisfied(const std::unordered_map<std::string, PolicyContent::CallerRule> &policyAllowRuleContent,
                            const AuthorizeParam &authorizeParam,
                            const std::unordered_map<std::string, std::unordered_set<std::string>> &funcWhiteList)
{
    // Step 1. get call method
    auto iterCallRuleMap = policyAllowRuleContent.find(authorizeParam.callMethod);
    if (iterCallRuleMap == policyAllowRuleContent.end()) {
        return Status(StatusCode::FAILED, authorizeParam.callMethod +" allow rules not found.");
    }
    // step 2. get caller group
    auto iterSingleRuleMap = iterCallRuleMap->second.find(authorizeParam.callerGroup);
    if (iterSingleRuleMap == iterCallRuleMap->second.end()) {
        return Status(StatusCode::FAILED,
                      "caller group allow rules not found, callerGroup: " + authorizeParam.callerGroup);
    }
    // step 3. get callee group
    auto iterCalleeGroup = iterSingleRuleMap->second.find(authorizeParam.calleeGroup);
    if (iterCalleeGroup == iterSingleRuleMap->second.end()) {
        return Status(StatusCode::FAILED,
                      "callee group allow rules not found, calleeGroup: " + authorizeParam.calleeGroup);
    }
    // step 4. get allowed functions
    if (auto status(IsFuncInList(iterCalleeGroup->second, authorizeParam, funcWhiteList)); status.IsError()) {
        return Status(StatusCode::FAILED, "allow rule not satisfied, err: " + status.ToString());
    }
    return Status::OK();
}
}

Status PolicyHandler::HandleAuthorizeRequest(AuthorizeParam &authorizeParam,
                                             const std::shared_ptr<PolicyContent> &policyContent)
{
    Status status = CheckAuthorizeParamValid(authorizeParam);
    if (!status.IsOk()) {
        return status;
    }
    ASSERT_IF_NULL(policyContent);
    // step 1. Get func group from tenantID
    authorizeParam.callerGroup = GetFuncGroupFromTenantID(authorizeParam.callerTenantID, policyContent->funcGroups_);
    authorizeParam.calleeGroup = GetFuncGroupFromTenantID(authorizeParam.calleeTenantID, policyContent->funcGroups_);
    // step 2. Check deny rules.
    if (policyContent->tenantBlackList_.find(authorizeParam.callerTenantID) != policyContent->tenantBlackList_.end()) {
        return Status(StatusCode::FAILED, "authorize failed, caller tenantID in blacklist, caller tenantID: " +
                      authorizeParam.callerTenantID);
    }
    // step 3. Check allow rules.
    if (policyContent->policyAllowRuleContent_.empty()) {
        return Status(StatusCode::FAILED, "authorize failed, allow rules are empty.");
    }
    status = IsAllowRuleSatisfied(policyContent->policyAllowRuleContent_, authorizeParam,
                                  policyContent->funcWhiteList_);
    if (!status.IsOk()) {
        return Status(StatusCode::FAILED, "Unauthorized call in allow rules, err: " + status.ToString());
    }
    return Status::OK();
}
} // namespace functionsystem::function_proxy