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

#include "posix_auth_interceptor.h"

#include "common/aksk/aksk_util.h"

namespace functionsystem {
litebus::Future<bool> PosixAuthInterceptor::Verify(const std::shared_ptr<runtime_rpc::StreamingMessage> &message)
{
    ASSERT_IF_NULL(message);
    if (!internalIam_->IsIAMEnabled()) {
        return true;
    }

    auto accessKey = message->metadata().find("access_key");
    if (accessKey == message->metadata().end() || accessKey->second.empty()) {
        YRLOG_ERROR("failed to verify message({}), failed to find access_key in meta-data, instance({}), runtime({})",
                    message->messageid(), instanceID_, runtimeID_);
        return false;
    }

    return internalIam_->RequireCredentialByAK(accessKey->second)
        .Then(
            [message, instanceID(instanceID_), runtimeID(runtimeID_)](const std::shared_ptr<AKSKContent> &akSkContent) {
                if (akSkContent == nullptr || akSkContent->IsValid().IsError()) {
                    YRLOG_ERROR("failed to verify message({}), failed to get cred from iam, instance({}), runtime({})",
                                message->messageid(), instanceID, runtimeID);
                    return false;
                }
                return VerifyStreamingMessage(akSkContent->accessKey, akSkContent->secretKey, message);
            });
}

litebus::Future<bool> PosixAuthInterceptor::Sign(const std::shared_ptr<runtime_rpc::StreamingMessage> &message)
{
    if (!internalIam_->IsIAMEnabled()) {
        return true;
    }

    if (accessKey_.empty()) {
        YRLOG_ERROR("failed to sign message({}), failed to find access_key in meta-data, instance({}), runtime({})",
                    message->messageid(), instanceID_, runtimeID_);
        return false;
    }

    return internalIam_->RequireCredentialByAK(accessKey_)
        .Then(
            [message, instanceID(instanceID_), runtimeID(runtimeID_)](const std::shared_ptr<AKSKContent> &akSkContent) {
                if (akSkContent == nullptr || akSkContent->IsValid().IsError()) {
                    YRLOG_ERROR("failed to sign message({}), failed to get cred from iam, instance({}), runtime({})",
                                message->messageid(), instanceID, runtimeID);
                    return false;
                }

                if (!SignStreamingMessage(akSkContent->accessKey, akSkContent->secretKey, message)) {
                    YRLOG_ERROR("failed to sign message({}), instance({}), runtime({})", message->messageid(),
                                instanceID, runtimeID);
                    return false;
                }
                return true;
            });
}
}  // namespace functionsystem