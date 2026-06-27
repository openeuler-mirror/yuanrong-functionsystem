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

#include "meta_store_explorer.h"

#include "httpd/http_connect.hpp"
#include "utils/os_utils.hpp"
#include "common/logs/logging.h"

namespace functionsystem {
litebus::Future<std::string> MetaStoreDefaultExplorer::Explore()
{
    return address_;
}

bool MetaStoreDefaultExplorer::IsNeedExplore()
{
    return false;
}

void MetaStoreDefaultExplorer::UpdateAddress(const std::string &address)
{
    address_ = address;
}

litebus::Future<std::string> MetaStoreHttpExplorer::Explore()
{
    litebus::http::Request request;
    request.method = "GET";

    size_t schemeIndex = address_.find("://");
    std::string prefix = "";
    if (schemeIndex == std::string::npos) {
        prefix = "http://";
    }

    litebus::Try<litebus::http::URL> httpUrl = litebus::http::URL::Decode(prefix + address_ + "/metastore/explore");
    if (httpUrl.IsError()) {
        YRLOG_ERROR("failed to decode meta-store explorer server url.");
        return litebus::Status(litebus::Status::KERROR);
    }

    request.url = httpUrl.Get();
    if (useAkSk_) {
        auto authHeaders = SignHttpRequest(SignRequest(request.method, "/metastore/explore", {}, {}, ""), authKey_);
        for (const auto &kvp : authHeaders) {
            request.headers[kvp.first] = kvp.second;
        }
    }

    return litebus::http::LaunchRequest(request).Then(
        [](const litebus::Future<litebus::http::Response> &respFuture) -> litebus::Future<std::string> {
            if (respFuture.IsError()) {
                YRLOG_ERROR(
                    "error({}), calling api meta-store explorer, please ensure that the meta-store explorer server is"
                    "reachable.",
                    respFuture.GetErrorCode());
                return litebus::Status(litebus::Status::KERROR);
            }
            auto resp = respFuture.Get();
            int code = resp.retCode;
            if (code >= litebus::http::ResponseCode::BAD_REQUEST
                || code == litebus::http::HttpErrorCode::CONNECTION_REFUSED
                || code == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT) {
                YRLOG_ERROR("error({}) calling api meta-store explorer, please ensure that meta-store is available.",
                            code);
                return litebus::Status(litebus::Status::KERROR);
            }
            YRLOG_INFO("success to explore meta-store on {}, code: {}", resp.body, fmt::underlying(resp.retCode));
            return resp.body;
        });
}

void MetaStoreHttpExplorer::SetAuthKey()
{
    auto enableAKSK = litebus::os::GetEnv(litebus::os::LITEBUS_AKSK_ENABLED);
    if (enableAKSK.IsSome() && enableAKSK.Get() == "1") {
        auto tmpAk = litebus::os::GetEnv(litebus::os::LITEBUS_ACCESS_KEY);
        auto tmpSK = litebus::os::GetEnv(litebus::os::LITEBUS_SECRET_KEY);
        auto tmpDK = litebus::os::GetEnv(LITEBUS_DATA_KEY);
        if (tmpAk.IsSome() && tmpSK.IsSome() && tmpDK.IsSome()) {
            useAkSk_ = true;
            authKey_ = KeyForAKSK(tmpAk.Get(), SensitiveValue(tmpSK.Get()), SensitiveValue(tmpDK.Get()));
        } else {
            YRLOG_WARN("failed to obtain the secret key when 2fa enabled");
        }
    }
}

bool MetaStoreHttpExplorer::IsNeedExplore()
{
    return true;
}

void MetaStoreHttpExplorer::UpdateAddress(const std::string &address)
{
    address_ = address;
}

}  // namespace functionsystem