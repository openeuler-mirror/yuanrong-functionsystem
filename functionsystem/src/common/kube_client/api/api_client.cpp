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

#include "api_client.h"

#include <iomanip>
#include <limits>
#include <sstream>

#include "common/kube_client/constants.h"
#include "common/logs/logging.h"
#include "common/status/status.h"
#include "ssl/ssl_env.hpp"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

const std::string CONTINUE = "continue";
const std::string LIMIT = "limit";
const uint16_t HTTP_CODE_CLIENT_ERROR = 400;
const uint16_t HTTP_CODE_CLIENT_SUCCESS = 200;
const std::string JSON_ARRAY_DELIM = "\n";

template <typename T>
std::string ToString(const T value)
{
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<T>::digits10) << std::fixed << value;
    return out.str();
}

bool WatchHandlerManager::RegisterWatchHandler(const std::string &modelName, const EventType &eventType,
                                               const WatchHandler &handler)
{
    if (eventType >= EventType::EVENT_TYPE_MAX) {
        YRLOG_WARN("eventType is invalid, eventType:{}", static_cast<int>(eventType));
        return false;
    }
    m_watchHandler[modelName][eventType] = handler;
    return true;
}

WatchHandler WatchHandlerManager::GetWatchHandlerFunc(const std::string &modelName, const EventType &eventType) const
{
    if (eventType >= EventType::EVENT_TYPE_MAX) {
        YRLOG_WARN("eventType is invalid, eventType:{}", static_cast<int>(eventType));
        return nullptr;
    }
    auto objWatch = m_watchHandler.find(modelName);
    if (objWatch == m_watchHandler.end()) {
        YRLOG_WARN("Can not find objWatch, modelName:{}", modelName);
        return nullptr;
    }
    auto objEventWatch = objWatch->second.find(eventType);
    if (objEventWatch == objWatch->second.end()) {
        YRLOG_WARN("Can not find objEventWatch, modelName:{},eventType:{} ", modelName, static_cast<int>(eventType));
        return nullptr;
    }
    return objEventWatch->second;
}

EventType WatchHandlerManager::GetEventTypeByStr(const std::string &typeName)
{
    static std::map<std::string, EventType> watchEventMap = { { "ADDED", EventType::EVENT_TYPE_ADD },
                                                              { "MODIFIED", EventType::EVENT_TYPE_MODIFIED },
                                                              { "DELETED", EventType::EVENT_TYPE_DELETE },
                                                              { "ERROR", EventType::EVENT_TYPE_ERROR } };
    auto iter = watchEventMap.find(typeName);
    return (iter == watchEventMap.end()) ? EventType::EVENT_TYPE_MAX : iter->second;
}

ApiClient::ApiClient(std::shared_ptr<const ApiConfiguration> configuration) : m_configuration(configuration)
{
    SetSslEnv();
}
ApiClient::~ApiClient()
{
}

const ApiClient::ResponseHandlerType &ApiClient::GetResponseHandler() const
{
    return m_responseHandler;
}

void ApiClient::SetResponseHandler(const ResponseHandlerType &responseHandler)
{
    m_responseHandler = responseHandler;
}

std::shared_ptr<const ApiConfiguration> ApiClient::GetConfiguration() const
{
    return m_configuration;
}
void ApiClient::SetConfiguration(const std::shared_ptr<const ApiConfiguration> &configuration)
{
    m_configuration = configuration;
}

std::string ApiClient::ParameterToString(bool value)
{
    std::stringstream valueAsStringStream;
    valueAsStringStream << std::boolalpha << value;
    return std::string(valueAsStringStream.str());
}

std::map<std::string, std::string> ApiClient::GetLocalQueryParams(const litebus::Option<std::string> &rContinue,
                                                                  const litebus::Option<uint32_t> &limit,
                                                                  const litebus::Option<bool> &watch)
{
    std::map<std::string, std::string> localVarQueryParams;
    if (watch.IsSome()) {
        localVarQueryParams[std::string("watch")] = ParameterToString(watch.Get());
    }
    if (rContinue.IsSome()) {
        localVarQueryParams[CONTINUE] = rContinue.Get();
    }
    if (limit.IsSome()) {
        localVarQueryParams[LIMIT] = std::to_string(limit.Get());
    }
    return localVarQueryParams;
}

Status ApiClient::SetHttpRequest(litebus::http::Request &request, const std::string &path, const std::string &method,
                                 const std::map<std::string, std::string> &queryParams,
                                 const std::shared_ptr<IHttpBody> &postBody) const
{
    ASSERT_IF_NULL(m_configuration);
    litebus::Try<litebus::http::URL> httpUrl = litebus::http::URL::Decode(m_configuration->GetBaseUrl() + path);
    if (httpUrl.IsError()) {
        YRLOG_ERROR("failed to decode url, {}, {}", m_configuration->GetBaseUrl(), path);
        return Status(StatusCode::FAILED);
    }
    request.url = httpUrl.Get();
    YRLOG_DEBUG("url is {}, {}", request.url.path, method);
    for (const auto &kvp : queryParams) {
        request.url.query[kvp.first] = kvp.second;
        if (kvp.first == "watch" && kvp.second == "true") {
            request.keepAlive = true;
        }
    }
    request.method = method;
    request.headers["Accept"] = CONTENT_TYPE_APPLICATION_JSON;
    if (postBody != nullptr) {
        std::stringstream data;
        postBody->WriteTo(data);
        auto bodyString = data.str();
        request.body = bodyString;
        if (request.method == "PATCH") {
            request.headers["Content-Type"] = "application/json-patch+json";
        } else {
            request.headers["Content-Type"] = CONTENT_TYPE_APPLICATION_JSON;
        }
    }
    if (request.headers.find("User-Agent") == request.headers.end() && !m_configuration->GetUserAgent().empty()) {
        request.headers["User-Agent"] = m_configuration->GetUserAgent();
    }
    auto authorization = m_configuration->GetApiKey("Authorization");
    if (!authorization.empty()) {
        request.headers["Authorization"] = "Bearer " + authorization;
    }
    request.credential = m_configuration->GetSslConfig().credential;
    return Status::OK();
}

litebus::Future<std::string> ApiClient::CallApi(const std::string &path, const std::string &method,
                                                const std::map<std::string, std::string> &queryParams,
                                                const std::shared_ptr<IHttpBody> &postBody) const
{
    litebus::http::Request request;
    auto status = SetHttpRequest(request, path, method, queryParams, postBody);
    if (status.IsError()) {
        litebus::Promise<std::string> promise;
        promise.SetFailed(StatusCode::FAILED);
        return promise.GetFuture();
    }
    if (auto watch = queryParams.find("watch"); watch != queryParams.end() && watch->second == "true") {
        YRLOG_INFO("call api {} watch", path);
        auto callback = std::bind(&ApiClient::HandleResponse, *this, request.url.path, std::placeholders::_1);
        return CallApi(request, callback);
    }
    return CallApi(request);
}

litebus::Future<std::string> ApiClient::CallApi(const litebus::http::Request &request,
                                                const litebus::http::ResponseCallback &callback) const
{
    litebus::Future<litebus::http::Response> future;
    if (callback != nullptr) {
        future = litebus::http::LaunchRequest(request, callback);
    } else {
        future = litebus::http::LaunchRequest(request);
    }
    litebus::Promise<std::string> promise;
    future.OnComplete([=](const litebus::Future<litebus::http::Response> &localVarResponseFuture) {
        if (localVarResponseFuture.IsError()) {
            YRLOG_WARN("error calling api: url is {}, method is {}", request.url.path, request.method);
            promise.SetFailed(localVarResponseFuture.GetErrorCode());
            return;
        }
        auto localVarResponse = localVarResponseFuture.Get();
        if (GetResponseHandler()) {
            GetResponseHandler()(localVarResponse.retCode, localVarResponse.headers);
        }

        int code = localVarResponse.retCode;
        if (code >= litebus::http::ResponseCode::INTERNAL_SERVER_ERROR ||
            code == litebus::http::HttpErrorCode::CONNECTION_REFUSED ||
            code == litebus::http::HttpErrorCode::CONNECTION_TIMEOUT ||
            code == litebus::http::ResponseCode::NOT_FOUND || code == litebus::http::ResponseCode::TOO_MANY_REQUESTS) {
            YRLOG_WARN("error calling api: url is {}, method is {}, code is: {}, body is {}", request.url.path,
                       request.method, fmt::underlying(localVarResponse.retCode), localVarResponse.body);
            promise.SetFailed(localVarResponse.retCode);
            return;
        }

        if (localVarResponse.retCode >= HTTP_CODE_CLIENT_ERROR || localVarResponse.retCode < HTTP_CODE_CLIENT_SUCCESS) {
            YRLOG_WARN("error calling api: {}, body is {}",
                       litebus::http::Response::GetStatusDescribe(localVarResponse.retCode), localVarResponse.body);
            promise.SetFailed(localVarResponse.retCode);
            return;
        }
        if (localVarResponse.headers.find(std::string("Content-Type")) != localVarResponse.headers.end()) {
            auto headers = localVarResponse.headers;
            std::string localVarContentType = headers[std::string("Content-Type")];
            if (localVarContentType.find(CONTENT_TYPE_APPLICATION_JSON) == std::string::npos) {
                YRLOG_WARN("error calling api: unexpected response type: {}, body is {}", localVarContentType,
                           localVarResponse.body);
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }
        }
        promise.SetValue(localVarResponse.body);
    });
    return promise.GetFuture();
}

std::vector<nlohmann::json> ApiClient::ParseJsonMuchPossibleFromData(std::string &data) const
{
    std::string pattern = JSON_ARRAY_DELIM;
    std::string::size_type pos = 0;
    std::vector<nlohmann::json> result;
    std::string::size_type size = data.size();
    std::string::size_type index = 0;

    while (index < size) {
        pos = data.find(pattern, index);
        if (pos == std::string::npos) {
            break;
        }
        std::string s = data.substr(index, pos - index);
        nlohmann::json bodyJson;
        try {
            bodyJson = nlohmann::json::parse(s);
            if (bodyJson.empty()) {
                YRLOG_ERROR("bodyJson is empty");
                index = pos + pattern.size();
                continue;
            }
        } catch (const std::exception &exc) {
            YRLOG_ERROR("error json::parse expression: {}", exc.what());
            index = pos + pattern.size();
            continue;
        }
        result.push_back(std::move(bodyJson));
        index = pos + pattern.size();
    }

    data = data.substr(index, size - index);

    return result;
}

std::vector<nlohmann::json> ApiClient::ParseResponseToJson(const std::string &key,
                                                           const litebus::http::Response *response)
{
    if (response->body.empty()) {
        YRLOG_ERROR("response body is empty");
        return {};
    }
    if (dataReceivedCacheMap_.find(key) == dataReceivedCacheMap_.end()) {
        YRLOG_DEBUG("GetHandleResponse new dataReceivedCache, key:{}", key);
        dataReceivedCacheMap_[key] = "";
    }
    dataReceivedCacheMap_[key] += response->body;
    return ParseJsonMuchPossibleFromData(dataReceivedCacheMap_[key]);
}

void ApiClient::HandleResponse(const std::string &key, const litebus::http::Response *response)
{
    std::vector<nlohmann::json> bodyJsonVec = ParseResponseToJson(key, response);
    for (const auto &bodyJson : bodyJsonVec) {
        if (!bodyJson.contains("type")) {
            YRLOG_ERROR("error body json");
            return;
        }
        std::string type = bodyJson["type"].get<std::string>();
        nlohmann::json objectValue = bodyJson["object"];
        auto version = objectValue["apiVersion"].get<std::string>();
        auto kind = objectValue["kind"].get<std::string>();
        auto modelName = ToModelName(kind);
        auto newObj = ModelFactory::GetInstance().CreateModel(modelName);
        if (newObj == nullptr) {
            YRLOG_WARN("newObj is nullptr");
            return;
        }
        newObj->FromJson(objectValue);

        EventType eventType = WatchHandlerManager::GetEventTypeByStr(type);
        auto watchHandler = m_watchManager.GetWatchHandlerFunc(modelName, eventType);
        if (watchHandler == nullptr) {
            YRLOG_WARN("watchHandler is nullptr");
            return;
        }
        watchHandler(eventType, newObj);
    }
}

void ApiClient::SetSslEnv()
{
    // 场景一：只配置 3
    // 场景二：只配置 2 + 3
    // 场景三：配置 1 + 2 + 3
    // 场景四：配置 1（加密） + 2 + 3
    ASSERT_IF_NULL(m_configuration);
    auto sslConfig = m_configuration->GetSslConfig();
    auto ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_ENABLED", "1");
    if (ret != 0) {
        YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
    }

    if (!sslConfig.clientKeyFile.empty()) {  // 加密个人证书的密码
        ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_KEY_FILE",
                                      sslConfig.clientKeyFile.c_str());
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
    }

    if (!sslConfig.clientCertFile.empty()) {  // 个人证书
        ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_CERT_FILE",
                                      sslConfig.clientCertFile.c_str());
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
    }

    if (!sslConfig.caCertFile.empty()) {
        // 握手时服务端回 hello 时携带服务端公钥证书，客户端基于CA校验服务端公钥证书合法性
        ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_CA_DIR", "/");
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
        ret =
            LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_CA_FILE", sslConfig.caCertFile.c_str());
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
    }

    if (!sslConfig.isSkipTlsVerify) {
        ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_VERIFY_CERT", "1");
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
    } else {
        ret = LitebusSetMultiSSLEnvsC(sslConfig.credential.c_str(), "LITEBUS_SSL_VERIFY_CERT", "0");
        if (ret != 0) {
            YRLOG_WARN("failed to LitebusSetMultiSSLEnvsC, ret: {}", ret);
        }
    }
}

}  // namespace api
}  // namespace functionsystem::kube_client