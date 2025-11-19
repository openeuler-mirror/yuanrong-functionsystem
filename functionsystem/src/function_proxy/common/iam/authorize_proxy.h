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

#ifndef FUNCTION_PROXY_COMMON_IAM_AUTHORIZE_PROXY_H
#define FUNCTION_PROXY_COMMON_IAM_AUTHORIZE_PROXY_H

#include "policy_handler.h"

namespace functionsystem::function_proxy {
const std::string DEFAULT_POLICY_PATH = "/home/sn/app_secret/policy";

class AuthorizeProxy {
public:
    explicit AuthorizeProxy(const std::string &policyPath = DEFAULT_POLICY_PATH);
    ~AuthorizeProxy() = default;

    Status Authorize(AuthorizeParam &authorizeParam);
    Status UpdatePolicy();

private:
    std::string policyPath_;
    std::shared_ptr<PolicyHandler> policyHandler_{ nullptr };
    std::shared_ptr<PolicyContent> policyContent_{ nullptr };
};
} // namespace functionsystem::function_proxy
#endif  // FUNCTION_PROXY_COMMON_IAM_AUTHORIZE_PROXY_H