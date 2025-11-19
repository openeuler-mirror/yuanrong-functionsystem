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

#ifndef COMMON_HTTP_HTTP_SERVER_H
#define COMMON_HTTP_HTTP_SERVER_H

#include <httpd/http_actor.hpp>

#include "api_router_register.h"

namespace functionsystem {

class HttpServer : public litebus::http::HttpActor {
public:
    /**
     * HttpServer constructor.
     *
     * @param name HttpServer actor name.
     */
    explicit HttpServer(const std::string &name);

    /**
     * Register url and handler pairs.
     *
     * @param router Router contains url and handler pairs.
     * @return Result status.
     */
    Status RegisterRoute(const std::shared_ptr<ApiRouterRegister> &router);

    ~HttpServer() override = default;

protected:
    void Init() override;

private:
    litebus::Future<HttpResponse> HandleRequest(const HttpRequest &request);

    std::string GetEndpoint(const std::string &url) const;

    std::shared_ptr<HandlerMap> handlerMap_{ nullptr };
};

}  // namespace functionsystem

#endif  // COMMON_HTTP_HTTP_SERVER_H
