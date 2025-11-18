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

#ifndef FUNCTION_PROXY_COMMON_IAM_POLICY_HANDLER_H
#define FUNCTION_PROXY_COMMON_IAM_POLICY_HANDLER_H

#include "policy_content.h"
#include "common/status/status.h"

namespace functionsystem::function_proxy {
struct AuthorizeParam {
    std::string callerTenantID;
    std::string calleeTenantID;
    std::string callMethod;
    std::string funcName;

    std::string callerGroup{ "" };
    std::string calleeGroup{ "" };
};

class PolicyHandler {
public:
    PolicyHandler() = default;
    ~PolicyHandler() = default;

    Status HandleAuthorizeRequest(AuthorizeParam &authorizeParam, const std::shared_ptr<PolicyContent> &policyContent);
};
} // namespace functionsystem::function_proxy

#endif // FUNCTION_PROXY_COMMON_IAM_POLICY_HANDLER_H