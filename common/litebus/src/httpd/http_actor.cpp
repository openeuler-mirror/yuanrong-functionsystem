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

#include "actor/actor.hpp"
#include "actor/buslog.hpp"
#include "async/future.hpp"
#include "httpd/http.hpp"
#include "utils/string_utils.hpp"
#include "httpd/http_actor.hpp"

using namespace std;

namespace litebus {
namespace http {

void HttpActor::Done(const std::string &name, const Request &request, HttpMessage *httpMessage)
{
    // if user add route for '/', accept all names
    auto iter = httphandles.find("/");
    if (iter != httphandles.end()) {
        Future<Response> response = httphandles[iter->first](request);
        httpMessage->GetResponsePromise()->Associate(response);
        return;
    } else {
        BUSLOG_WARN("Can not find this handle, name: {}", name);
        httpMessage->GetResponsePromise()->Associate(NotFound());
    }
    return;
}

void HttpActor::AddRoute(const std::string &name, HttpActor::HttpRequestHandler &&handler)
{
    BUSLOG_INFO("Add endpoint, name:{}", name);
    httphandles[name] = handler;
}

string HttpActor::GetHttpFunctionName(const string &path) const
{
    string name = path;
    name = "/" + litebus::strings::Trim(name, litebus::strings::PREFIX, "/");
    vector<string> tokens = litebus::strings::Tokenize(name, "/");
    // remove header(actor name) of request.url.path
    if (!tokens.empty()) {
        name = litebus::strings::Remove(name, "/" + tokens[0], litebus::strings::PREFIX);
    }

    // set default handle name
    if (name.empty()) {
        BUSLOG_INFO("Set default handle name as '/'.");
        name = "/";
    }
    return name;
}

void HttpActor::HandleHttp(std::unique_ptr<MessageBase> message)
{
    HttpMessage *httpMessage = nullptr;
    httpMessage = dynamic_cast<HttpMessage *>(message.get());
    if (httpMessage == nullptr) {
        BUSLOG_WARN("Can't transform to HttpMessage.");
        return;
    }
    const Request &request = httpMessage->GetRequest();
    string name = GetHttpFunctionName(request.url.path);
    BUSLOG_DEBUG("handle name, size={},name={},urlfrom={}", name.size(), name, request.url.path);
    // as '////a/b', we will check '/a/b'
    name = "/" + litebus::strings::Trim(name, litebus::strings::PREFIX, "/");
    // as '/a/b/////', we will check '/a/b/', if find '/a/b', returns failed
    if (name.back() == '/') {
        name = litebus::strings::Trim(name, litebus::strings::SUFFIX, "/");
        auto iter = httphandles.find(name);
        if (iter != httphandles.end()) {
            Done(name, request, httpMessage);
            return;
        }
    }

    // as '/a/b/c', if we want '/a/b', do as belows to meet the best match
    while (name != "/" && !name.empty()) {
        if (name.back() == '/') {
            name = litebus::strings::Trim(name, litebus::strings::SUFFIX, "/");
        }

        auto iter = httphandles.find(name);
        if (iter != httphandles.end()) {
            Future<Response> response = httphandles[iter->first](request);
            httpMessage->GetResponsePromise()->Associate(response);
            return;
        }

        vector<string> tokens = litebus::strings::Tokenize(name, "/");
        if (tokens.empty()) {
            // name is '/'
            break;
        } else {
            name = litebus::strings::Remove(name, "/" + tokens[tokens.size() - 1], litebus::strings::SUFFIX);
        }
    }
    Done(name, request, httpMessage);
    return;
}

}    // namespace http
}    // namespace litebus
