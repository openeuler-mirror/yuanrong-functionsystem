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

#ifndef __HTTP_ACTOR_HPP__
#define __HTTP_ACTOR_HPP__

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "actor/actor.hpp"
#include "http.hpp"

namespace litebus {
namespace http {

class HttpActor : public litebus::ActorBase {
public:
    typedef std::function<litebus::Future<http::Response>(const http::Request &)> HttpRequestHandler;

    explicit HttpActor(const std::string &name) : litebus::ActorBase(name)
    {
    }

    ~HttpActor() override
    {
    }

    void AddRoute(const std::string &name, HttpRequestHandler &&handler);

    template <typename T>
    void AddRoute(const std::string &name, Future<Response> (T::*method)(const Request &))
    {
        HttpRequestHandler handler = std::bind(method, dynamic_cast<T *>(this), std::placeholders::_1);

        AddRoute(name, std::move(handler));
    }

protected:
    void HandleHttp(std::unique_ptr<MessageBase> msg) override;

private:
    void Done(const std::string &name, const Request &request, HttpMessage *httpMessage);
    std::string GetHttpFunctionName(const std::string &path) const;
    std::map<std::string, HttpRequestHandler> httphandles;
};

}    // namespace http
}    // namespace litebus
#endif
