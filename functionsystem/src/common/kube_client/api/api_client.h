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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CLIENT_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CLIENT_H

#include <functional>
#include <memory>
#include <vector>

#include "api_configuration.h"
#include "api_exception.h"
#include "async/future.hpp"
#include "common/kube_client/model/common/ihttp_body.h"
#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/pod/v1_pod.h"
#include "common/kube_client/model/pod/v1_pod_list.h"
#include "common/status/status.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"

#if defined(_WIN32) || defined(_WIN64)
#undef U
#endif

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;
enum class EventType : int { EVENT_TYPE_ADD, EVENT_TYPE_MODIFIED, EVENT_TYPE_DELETE, EVENT_TYPE_ERROR, EVENT_TYPE_MAX };
using WatchHandler = std::function<void(const EventType &, const std::shared_ptr<ModelBase> &)>;

class WatchHandlerManager {
public:
    bool RegisterWatchHandler(const std::string &modelName, const EventType &eventType, const WatchHandler &handler);
    WatchHandler GetWatchHandlerFunc(const std::string &modelName, const EventType &eventType) const;
    static EventType GetEventTypeByStr(const std::string &typeName);

private:
    std::unordered_map<std::string, std::unordered_map<EventType, WatchHandler>> m_watchHandler;
};

class ApiClient {
public:
    explicit ApiClient(std::shared_ptr<const ApiConfiguration> configuration = nullptr);
    virtual ~ApiClient();
    using ResponseHandlerType = std::function<void(litebus::http::ResponseCode, const litebus::http::HeaderMap &)>;

    const ResponseHandlerType &GetResponseHandler() const;
    void SetResponseHandler(const ResponseHandlerType &responseHandler);

    [[maybe_unused]] std::shared_ptr<const ApiConfiguration> GetConfiguration() const;
    [[maybe_unused]] void SetConfiguration(const std::shared_ptr<const ApiConfiguration> &configuration);

    static std::string ParameterToString(bool value);

    static std::map<std::string, std::string> GetLocalQueryParams(const litebus::Option<std::string> &rContinue,
                                                                  const litebus::Option<uint32_t> &limit,
                                                                  const litebus::Option<bool> &watch);

    litebus::Future<std::string> CallApi(const std::string &path, const std::string &method,
                                         const std::map<std::string, std::string> &queryParams,
                                         const std::shared_ptr<IHttpBody> &postBody) const;
    litebus::Future<std::string> CallApi(const litebus::http::Request &request,
                                         const litebus::http::ResponseCallback &callback = nullptr) const;

    Status SetHttpRequest(litebus::http::Request &request, const std::string &path, const std::string &method,
                          const std::map<std::string, std::string> &queryParams,
                          const std::shared_ptr<IHttpBody> &postBody) const;

    std::vector<nlohmann::json> ParseResponseToJson(const std::string &key, const litebus::http::Response *response);
    void HandleResponse(const std::string &key, const litebus::http::Response *response);
    bool RegisterWatchHandler(const std::string &modelName, const EventType &eventType, const WatchHandler &handler)
    {
        return m_watchManager.RegisterWatchHandler(modelName, eventType, handler);
    }
    WatchHandler GetWatchHandlerFunc(const std::string &modelName, const EventType &eventType)
    {
        return m_watchManager.GetWatchHandlerFunc(modelName, eventType);
    }

protected:
    WatchHandlerManager m_watchManager;
    ResponseHandlerType m_responseHandler;
    std::shared_ptr<const ApiConfiguration> m_configuration;
    std::map<std::string, std::string> dataReceivedCacheMap_;

    void SetSslEnv();
    std::vector<nlohmann::json> ParseJsonMuchPossibleFromData(std::string &data) const;
};
}  // namespace api
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_API_API_CLIENT_H */
