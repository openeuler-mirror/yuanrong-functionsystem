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

#include "http_pipeline_proxy.hpp"
#include <deque>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "actor/buslog.hpp"
#include "http_iomgr.hpp"
#include "httpd/http.hpp"

#include "actor/actormgr.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"
#include "evloop/evloop.hpp"
#include "iomgr/linkmgr.hpp"
#include "utils/os_utils.hpp"

using namespace std;
using std::placeholders::_1;

using namespace litebus;
using namespace http;

namespace litebus {
namespace http {

void HttpPipelineProxy::Process(const Request &request, const Future<Response> &responseFuture)
{
    DataItem *item = new (std::nothrow) DataItem(request, responseFuture);
    BUS_OOM_EXIT(item);
    dataItem.push_back(item);
    if (dataItem.size() == 1) {
        HandleNextCallback();
    }
}

void HttpPipelineProxy::HandleNextCallback()
{
    if (dataItem.size() > 0) {
        (void)dataItem.front()->responseFuture.OnComplete(
            Defer(GetAID(), &HttpPipelineProxy::ReceiveHttpResponseCallback, ::_1, dataItem.front()->request));
    }
}

void HttpPipelineProxy::ReceiveHttpResponseCallback(const Future<Response> &future, const Request &request)
{
    BUSLOG_DEBUG("Handle response from application layer.");
    if (dataItem.empty()) {
        BUS_EXIT("Pipeline is empty.");
    }

    DataItem *item = dataItem.front();

    if (future == item->responseFuture) {
        bool result = HandleResponse(future, request);
        dataItem.pop_front();
        delete item;
        item = nullptr;

        if (result) {
            HandleNextCallback();
        }
    } else {
        BUS_EXIT("Pipeline is error.");
    }
}

bool HttpPipelineProxy::HandleResponse(const Future<Response> &responseFuture, const Request &request) const
{
    Response response = responseFuture.Get();
    // encode response
    ostringstream output;
    output << "HTTP/1.1 " << static_cast<int>(response.retCode) << " " << Response::GetStatusDescribe(response.retCode)
           << "\r\n";

    auto iter = response.headers.begin();
    while (iter != response.headers.end()) {
        output << iter->first << ": " << iter->second << "\r\n";
        ++iter;
    }

    if (request.keepAlive) {
        if (response.headers.find("Connection") != response.headers.end()) {
            if (response.headers["Connection"] == "close") {
                output << "Connection: close\r\n";
            }
        } else {
            output << "Connection: Keep-Alive\r\n";
        }
    } else {
        output << "Connection: close\r\n";
    }

    // NOTE: Now we only send fixed-length message. Chunked message is not
    // supported.
    string body = response.body;
    if (body.size() > 0) {
        output << "Content-Length: " << body.size() << "\r\n";
        output << "\r\n";
        (void)output.write(body.data(), static_cast<std::streamsize>(body.size()));
    } else {
        output << "Content-Length: 0\r\n";
        output << "\r\n";
    }

    shared_ptr<TCPMgr> tcpmgr = dynamic_pointer_cast<TCPMgr>(ActorMgr::GetIOMgrRef(string("tcp")));

    if (tcpmgr != nullptr) {
        MessageBase *msg = new (std::nothrow) MessageBase(MessageBase::Type::KHTTP);
        BUS_OOM_EXIT(msg);
        msg->body = output.str();
        BUSLOG_DEBUG("Encode msg, request url,response code, body size, url:{},code:{},size:{}", request.url.path,
                     static_cast<std::underlying_type_t<ResponseCode>>(response.retCode), body.size());
        (void)tcpmgr->Send(msg, connection, conSeq);
    } else {
        BUSLOG_ERROR("tcp protocol is not exist.");
    }
    return true;
}

}    // namespace http
}    // namespace litebus
