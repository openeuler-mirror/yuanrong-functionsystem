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

#include "function_proxy/common/iam/iam_client.h"

#include "common/logs/logging.h"
#include "httpd/http_connect.hpp"
#include "ssl/ssl_env.hpp"
#include "utils/os_utils.hpp"

namespace functionsystem::function_proxy {
namespace {
const std::string AUTH_TOKEN_PATH = "/iam-server/v1/token/auth"; // path for verify token
const std::string REQUIRE_ENCRYPT_TOKEN_PATH = "/iam-server/v1/token/require"; // path for require encrypt token
const std::string ABANDON_TOKEN_PATH = "/iam-server/v1/token/abandon"; // path for abandon token
const std::string REQUIRE_CREDENTIAL_PATH = "/iam-server/v1/credential/require";
const std::string AUTH_CREDENTIAL_PATH = "/iam-server/v1/credential/auth";
const std::string ABANDON_CREDENTIAL_PATH = "/iam-server/v1/credential/abandon";

const std::string HEADER_TOKEN_KEY = "X-Auth"; // key for token
const std::string HEADER_TENANT_SALT_KEY = "X-Salt"; // salt for tenant
const std::string HEADER_TENANT_ID_KEY = "X-Tenant-ID"; // key for tenantID
const std::string HEADER_EXPIRED_TIME_SPAN = "X-Expired-Time-Span";

const uint16_t HTTP_CODE_CLIENT_ERROR = 400;
const uint16_t HTTP_CODE_CLIENT_SUCCESS = 200;

Status GetStatusFromResponse(const litebus::http::Response &localVarResponse)
{
    int code = localVarResponse.retCode;
    if (code >= litebus::http::ResponseCode::INTERNAL_SERVER_ERROR
        || code == litebus::http::HttpErrorCode::CONNECTION_REFUSED
        || code == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT) {
        YRLOG_ERROR("err calling api: {}, body is {}", code, localVarResponse.body);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, &"require token, errcode is "[code]);
    }
    if (code >= HTTP_CODE_CLIENT_ERROR || code < HTTP_CODE_CLIENT_SUCCESS) {
        YRLOG_ERROR("err calling api: {}, body is {}", code, localVarResponse.body);
        return Status(StatusCode::FAILED, litebus::http::Response::GetStatusDescribe(localVarResponse.retCode));
    }
    return Status::OK();
}

std::string GetHeaderValueFromResponse(const litebus::http::Response &localVarResponse, const std::string &headerKey)
{
    auto iter = localVarResponse.headers.find(headerKey);
    if (iter == localVarResponse.headers.end()) {
        YRLOG_ERROR("failed to get header:{} from response", headerKey);
        return "";
    } else if (iter->second.empty()) {
        YRLOG_ERROR("header:{} is empty from response", headerKey);
        return "";
    } else {
        return iter->second;
    }
}

std::shared_ptr<EncAKSKContent> ParseCredentialContentFromResponse(const litebus::http::Response &resp)
{
    auto status = GetStatusFromResponse(resp);
    if (status.IsError()) {
        auto errCredContent = std::make_shared<EncAKSKContent>();
        errCredContent->status = status;
        return errCredContent;
    }
    return TransToEncAKSKContentFromJson(resp.body);
}
}  // namespace

void IAMClient::SetIAMAddress(const std::string &iamAddress)
{
    iamAddress_ = iamAddress;
}

void IAMClient::SetAuthKey()
{
    auto enableAKSK = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enableAKSK.IsSome() && enableAKSK.Get() == "1") {
        auto tmpAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
        auto tmpSK = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
        auto tmpDK = litebus::os::GetEnv(LITEBUS_DATA_KEY);
        if (tmpAk.IsSome() && tmpSK.IsSome() && tmpDK.IsSome()) {
            useAKSK_ = true;
            authKey_ = KeyForAKSK(tmpAk.Get(), SensitiveValue(tmpSK.Get()), SensitiveValue(tmpDK.Get()));
        } else {
            YRLOG_WARN("Failed to obtain the secret key when 2fa enabled");
        }
    }
}

std::shared_ptr<IAMClient> IAMClient::CreateIAMClient(const std::string &iamAddress)
{
    auto client = std::make_shared<IAMClient>();
    client->SetIAMAddress(iamAddress);
    client->SetAuthKey();
    return client;
}

litebus::Future<std::shared_ptr<TokenSalt>> IAMClient::RequireEncryptToken(const std::string &tenantID)
{
    std::map<std::string, std::string> headers{ { HEADER_TENANT_ID_KEY, tenantID } };
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<TokenSalt>>>();
    auto future = CallApi(REQUIRE_ENCRYPT_TOKEN_PATH, std::string("GET"), headers);
    future.OnComplete([promise, tenantID](const litebus::Future<litebus::http::Response> &respFuture) {
        auto result = std::make_shared<TokenSalt>();
        if (respFuture.IsError()) {
            YRLOG_ERROR("{}|error({}) calling api require token, please ensure that the iam server is reachable.",
                        tenantID, respFuture.GetErrorCode());
            result->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "iam server not reachable");
            promise->SetValue(result);
            return;
        }
        auto localVarResponse = respFuture.Get();
        int code = localVarResponse.retCode;
        if (code >= litebus::http::ResponseCode::INTERNAL_SERVER_ERROR
            || code == litebus::http::HttpErrorCode::CONNECTION_REFUSED
            || code == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT) {
            YRLOG_ERROR("{}|error:{} require token, please ensure that iam server is available.", tenantID, code);
            result->status = Status(StatusCode::ERR_INNER_SYSTEM_ERROR, &"iam server not available:"[code]);
            promise->SetValue(result);
            return;
        }
        if (code >= HTTP_CODE_CLIENT_ERROR || code < HTTP_CODE_CLIENT_SUCCESS) {
            YRLOG_ERROR("{}|IAMClient require token err calling api: {}", tenantID, code);
            promise->SetValue(result);
            return;
        }
        result->token = GetHeaderValueFromResponse(localVarResponse, HEADER_TOKEN_KEY);
        result->salt = GetHeaderValueFromResponse(localVarResponse, HEADER_TENANT_SALT_KEY);
        std::string expireStr = GetHeaderValueFromResponse(localVarResponse, HEADER_EXPIRED_TIME_SPAN);
        if (!expireStr.empty()) {
            try {
                result->expiredTimeStamp = std::stoull(expireStr);
            } catch (std::exception &e) {
                YRLOG_WARN("{}|transform time stamp type failed, err:{}", tenantID, std::string(e.what()));
            }
        }
        promise->SetValue(result);
    });
    return promise->GetFuture();
}

litebus::Future<Status> IAMClient::VerifyToken(const std::string &token)
{
    std::map<std::string, std::string> headers{ { HEADER_TOKEN_KEY, token } };
    auto promise = std::make_shared<litebus::Promise<Status>>();
    auto future = CallApi(AUTH_TOKEN_PATH, std::string("GET"), headers);
    future.OnComplete([promise](const litebus::Future<litebus::http::Response> &respFuture) {
        if (respFuture.IsError()) {
            YRLOG_ERROR("error({}) calling api verify token, please ensure that the iam server is reachable.",
                        respFuture.GetErrorCode());
            promise->SetValue(Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "verify token error"));
            return;
        }
        promise->SetValue(GetStatusFromResponse(respFuture.Get()));
    });
    return promise->GetFuture();
}

litebus::Future<Status> IAMClient::AbandonToken(const std::string &tenantID)
{
    std::map<std::string, std::string> headers {{ HEADER_TENANT_ID_KEY, tenantID }};
    return CallApi(ABANDON_TOKEN_PATH, std::string("GET"), headers)
        .Then([](const litebus::http::Response &localVarResponse) {
            return GetStatusFromResponse(localVarResponse);
        });
}

litebus::Future<std::shared_ptr<EncAKSKContent>> IAMClient::RequireEncryptCredential(const std::string &tenantID)
{
    std::map<std::string, std::string> headers{ { HEADER_TENANT_ID_KEY, tenantID } };
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<EncAKSKContent>>>();
    auto future = CallApi(REQUIRE_CREDENTIAL_PATH, std::string("GET"), headers);
    future.OnComplete([promise](const litebus::Future<litebus::http::Response> &respFuture) {
        if (respFuture.IsError()) {
            auto errCredContent = std::make_shared<EncAKSKContent>();
            errCredContent->status =
                Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "require credential by tenantId request error");
            promise->SetValue(errCredContent);
            YRLOG_ERROR("error({}) to request {}, please ensure that the iam server is reachable.",
                        respFuture.GetErrorCode(), REQUIRE_CREDENTIAL_PATH);
            return;
        }
        promise->SetValue(ParseCredentialContentFromResponse(respFuture.Get()));
    });
    return promise->GetFuture();
}

litebus::Future<std::shared_ptr<EncAKSKContent>> IAMClient::VerifyCredential(const std::string &auth)
{
    std::map<std::string, std::string> headers{ { HEADER_TOKEN_KEY, auth } };
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<EncAKSKContent>>>();
    auto future = CallApi(AUTH_CREDENTIAL_PATH, std::string("GET"), headers);
    future.OnComplete([promise](const litebus::Future<litebus::http::Response> &respFuture) {
        if (respFuture.IsError()) {
            auto errCredContent = std::make_shared<EncAKSKContent>();
            errCredContent->status =
                Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "require credential by access key request error");
            promise->SetValue(errCredContent);
            YRLOG_ERROR("error({}) to request {}, please ensure that the iam server is reachable.",
                        respFuture.GetErrorCode(), AUTH_CREDENTIAL_PATH);
            return;
        }
        promise->SetValue(ParseCredentialContentFromResponse(respFuture.Get()));
    });
    return promise->GetFuture();
}

litebus::Future<Status> IAMClient::AbandonCredential(const std::string &tenantID)
{
    return CallApi(ABANDON_TOKEN_PATH, std::string("GET"), { { HEADER_TENANT_ID_KEY, tenantID } })
        .Then([](const litebus::http::Response &localVarResponse) { return GetStatusFromResponse(localVarResponse); });
}

void IAMClient::SetHttpRequest(litebus::http::Request &request,
                               const std::map<std::string, std::string> &headers) const
{
    for (const auto &kvp : headers) {
        request.headers[kvp.first] = kvp.second;
    }
}

litebus::Future<litebus::http::Response> IAMClient::CallApi(const std::string &path, const std::string &method,
                                                            const std::map<std::string, std::string> &headers)
{
    litebus::http::Request request;
    request.method = method;
    SetHttpRequest(request, headers);
    litebus::Try<litebus::http::URL> httpUrl = litebus::http::URL::Decode(iamAddress_ + path);
    if (httpUrl.IsError()) {
        YRLOG_ERROR("failed to decode iam server url.");
        return litebus::http::Response(litebus::http::ResponseCode::BAD_REQUEST, "failed to decode url.");
    }
    request.url = httpUrl.Get();
    if (useAKSK_) {
        std::shared_ptr<std::map<std::string, std::string>> queries =
            std::make_shared<std::map<std::string, std::string>>();
        for (const auto &pair : httpUrl.Get().query) {
            queries->insert(pair);
        }
        auto authHeaders = SignHttpRequest(SignRequest(method, path, queries, headers, ""), authKey_);
        SetHttpRequest(request, authHeaders);
    }

    return LaunchRequest(request);
}

litebus::Future<litebus::http::Response> IAMClient::LaunchRequest(const litebus::http::Request &request)
{
    return litebus::http::LaunchRequest(request);
}
} // namespace functionsystem::function_proxy