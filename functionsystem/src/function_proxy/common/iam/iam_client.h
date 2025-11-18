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

#ifndef FUNCTION_PROXY_COMMON_IAM_IAM_CLIENT_H
#define FUNCTION_PROXY_COMMON_IAM_IAM_CLIENT_H

#include <string>

#include "async/future.hpp"
#include "common/status/status.h"
#include "common/utils/aksk_content.h"
#include "common/utils/token_transfer.h"
#include "common/aksk/aksk_util.h"
#include "httpd/http.hpp"

namespace functionsystem::function_proxy {
class IAMClient {
public:
    IAMClient() = default;
    virtual ~IAMClient() = default;

    static std::shared_ptr<IAMClient> CreateIAMClient(const std::string &iamAddress);

    virtual litebus::Future<std::shared_ptr<TokenSalt>> RequireEncryptToken(const std::string &tenantID);

    virtual litebus::Future<Status> VerifyToken(const std::string &token);

    virtual litebus::Future<Status> AbandonToken(const std::string &tenantID);

    virtual litebus::Future<std::shared_ptr<EncAKSKContent>> RequireEncryptCredential(const std::string &tenantID);

    virtual litebus::Future<std::shared_ptr<EncAKSKContent>> VerifyCredential(const std::string &accessKey);

    virtual litebus::Future<Status> AbandonCredential(const std::string &tenantID);

    virtual void SetIAMAddress(const std::string &iamAddress);

    virtual void SetAuthKey();

    virtual litebus::Future<litebus::http::Response> LaunchRequest(const litebus::http::Request &request);

private:
    void SetHttpRequest(litebus::http::Request &request, const std::map<std::string, std::string> &headers) const;

    litebus::Future<litebus::http::Response> CallApi(const std::string &path, const std::string &method,
                                                     const std::map<std::string, std::string> &headers);

    std::string iamAddress_;

    bool useAKSK_{false};

    KeyForAKSK authKey_;
};
}
#endif  // FUNCTION_PROXY_COMMON_IAM_IAM_CLIENT_H