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

#include "http_server.h"

#include <async/async.hpp>

#include "common/logs/logging.h"

using namespace std;

namespace functionsystem {

HttpServer::HttpServer(const string &name) : litebus::http::HttpActor(name)
{
    handlerMap_ = make_shared<HandlerMap>();
}

void HttpServer::Init()
{
    YRLOG_INFO("init http server");
    if (handlerMap_ == nullptr) {
        YRLOG_ERROR("null handler map in http server");
        return;
    }
    for (auto &it : *handlerMap_) {
        if (it.first.empty()) {
            YRLOG_WARN("try to add empty url");
            continue;
        }
        AddRoute(it.first, &HttpServer::HandleRequest);
    }
}

Status HttpServer::RegisterRoute(const std::shared_ptr<ApiRouterRegister> &router)
{
    RETURN_STATUS_IF_NULL(router, FA_HTTP_REGISTER_HANDLER_NULL_ERROR, "failed to register route, null router");
    auto handlers = router->GetHandlers();
    if (handlers == nullptr) {
        YRLOG_ERROR("null handlers from router");
        return Status(FA_HTTP_REGISTER_HANDLER_NULL_ERROR);
    }
    for (auto &it : *handlers) {
        if (it.first.empty() || it.second == nullptr) {
            YRLOG_WARN("try to add empty url or nullptr handler");
            continue;
        }
        auto result = handlerMap_->emplace(it.first, it.second);
        if (!result.second) {
            YRLOG_WARN("register repeat url: {}", it.first);
            return Status(FA_HTTP_REGISTER_REPEAT_URL_ERROR);
        }
    }
    return Status::OK();
}

litebus::Future<HttpResponse> HttpServer::HandleRequest(const HttpRequest &request)
{
    auto handlerIterator = handlerMap_->find(GetEndpoint(request.url.path));
    if (handlerIterator == handlerMap_->end() || handlerIterator->second == nullptr) {
        return HttpResponse(litebus::http::ResponseCode::NOT_FOUND, "Can not find the handler",
                            litebus::http::ResponseBodyType::TEXT);
    }
    return handlerIterator->second(request);
}

string HttpServer::GetEndpoint(const string &url) const
{
    string header = "/";
    header += ActorBase::GetAID().Name();
    return url.substr(header.length());
}

}  // namespace functionsystem