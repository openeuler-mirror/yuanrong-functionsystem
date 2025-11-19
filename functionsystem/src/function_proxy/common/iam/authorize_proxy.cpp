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

#include "authorize_proxy.h"
#include "common/utils/files.h"

namespace functionsystem::function_proxy {
AuthorizeProxy::AuthorizeProxy(const std::string &policyPath) : policyPath_(policyPath)
{
    policyHandler_ = std::make_shared<PolicyHandler>();
    policyContent_ = std::make_shared<PolicyContent>();
}

Status AuthorizeProxy::Authorize(AuthorizeParam &authorizeParam)
{
    ASSERT_IF_NULL(policyHandler_);
    ASSERT_IF_NULL(policyContent_);
    if (auto status(policyHandler_->HandleAuthorizeRequest(authorizeParam, policyContent_)); status.IsError()) {
        YRLOG_ERROR("authorize failed, err: {}", status.ToString());
        return Status(StatusCode::FAILED, "authorize failed, err: " + status.ToString());
    }
    YRLOG_DEBUG("authorize success, callerTenantID: {}, calleeTenantID: {}, callMethod: {}, funcName: {}",
                authorizeParam.callerTenantID, authorizeParam.calleeTenantID,
                authorizeParam.callMethod, authorizeParam.funcName);
    return Status::OK();
}

Status AuthorizeProxy::UpdatePolicy()
{
    if (!FileExists(policyPath_)) {
        YRLOG_ERROR("policy file not exist, skip authorize");
        return Status(StatusCode::FAILED, "policy file not exist.");
    }
    std::string policyStr = Read(policyPath_);
    ASSERT_IF_NULL(policyContent_);
    if (auto status(policyContent_->UpdatePolicyContent(policyStr)); status.IsError()) {
        YRLOG_DEBUG("update policy failed, err: {}", status.ToString());
        return Status(StatusCode::FAILED, "update policy failed, err: " + status.ToString());
    }
    return Status::OK();
}
} // namespace functionsystem::function_proxy