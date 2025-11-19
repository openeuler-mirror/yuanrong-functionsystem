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

#ifndef FUNCTION_PROXY_COMMON_IAM_POLICY_CONTENT_H
#define FUNCTION_PROXY_COMMON_IAM_POLICY_CONTENT_H

#include "common/status/status.h"

namespace functionsystem::function_proxy {
const std::string CALL_METHOD_CREATE = "create";
const std::string CALL_METHOD_INVOKE = "invoke";
const std::string CALL_METHOD_KILL = "kill";

class PolicyContent {
public:
    PolicyContent() = default;
    ~PolicyContent() = default;

    Status UpdatePolicyContent(const std::string &policyStr);
    /**
     * TenantFuncGroup: tenantID and group of functions, eg:
     * "system_func": {
     *     "tenant_0": [ "faas-scheduler" ]
     * }
     */
    struct TenantFuncGroup {
        std::string funcGroup;
        std::string tenantID;
        std::unordered_set<std::string> funcList;
    };
    /**
     * SingleRule: satisfied functions in a callee group
     * "external_func": [ "*" ]
     */
    using SingleRule = std::unordered_map<std::string, std::unordered_set<std::string>>;
    /**
     * CallerRule: an integrated rule, a caller and its SingleRules
     * "system_func": {
     *     "external_func" : [ "*" ],
     *     "system_func" : [ "*" ]
     * }
     */
    using CallerRule = std::unordered_map<std::string, SingleRule>;
    /**
     * funcGroups_: collections of TenantFuncGroup, eg:
     * "system_func": {
     *     "tenant_0": [ "faas-scheduler" ]
     * },
     * "external_func": {}
     */
    std::vector<TenantFuncGroup> funcGroups_;
    /**
     * funcGroupSet_: all defined function groups in funcGroups_
     */
    std::unordered_set<std::string> funcGroupSet_;
    /**
     * policyAllowRuleContent_: rules belong to the call method, eg:
     * "Create": {
     *     "caller_group1": {
     *         "callee_group1" : [ "functions" ],
     *         "callee_group2" : [ "functions" ]
     *     }
     *     ...
     * },
     * "Invoke": {
     * ...
     * }
     */
    std::unordered_map<std::string, CallerRule> policyAllowRuleContent_;
    /**
     * tenantBlackList_: policy deny rule, tenants in tenantBlackList_ are not allow to call
     */
    std::unordered_set<std::string> tenantBlackList_;
    /**
     * funcWhiteList_: special scenarios in SingleRule, tenants in the func whitelist are allow to call the func, eg:
     * "func1": ["tenant_1"]
     */
    std::unordered_map<std::string, std::unordered_set<std::string>> funcWhiteList_;
    std::mutex policyContentMutex_;
};
} // namespace functionsystem::function_proxy

#endif // FUNCTION_PROXY_COMMON_IAM_POLICY_CONTENT_H