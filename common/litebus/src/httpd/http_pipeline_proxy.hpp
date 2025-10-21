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

#ifndef HTTP_PIPELINE_PROXY_H
#define HTTP_PIPELINE_PROXY_H

#include <deque>
#include <string>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "httpd/http.hpp"
#include "iomgr/linkmgr.hpp"

namespace litebus {
namespace http {

class HttpPipelineProxy : public ActorBase {
public:
    explicit HttpPipelineProxy(const std::string &name, litebus::Connection *tempConnection, int tempConSeq)
        : ActorBase(name), connection(tempConnection), conSeq(tempConSeq)
    {
    }

    ~HttpPipelineProxy() override
    {
        if (connection != nullptr) {
            connection = nullptr;
        }
    }

    void Process(const Request &request, const Future<Response> &future);

    void ReceiveHttpResponseCallback(const Future<Response> &future, const Request &request);

protected:
    void Finalize() override
    {
        while (!dataItem.empty()) {
            DataItem *item = dataItem.front();
            dataItem.pop_front();
            delete item;
            item = nullptr;
        }
    }

private:
    void HandleNextCallback();

    bool HandleResponse(const Future<Response> &responseFuture, const Request &request) const;

    litebus::Connection *connection = nullptr;

    int conSeq;

    struct DataItem {
        DataItem(const Request &req, const Future<Response> &future) : request(req), responseFuture(future)
        {
        }
        const Request request;
        Future<Response> responseFuture;
    };

    std::deque<DataItem *> dataItem;

    HttpPipelineProxy(const HttpPipelineProxy &);
    HttpPipelineProxy &operator=(const HttpPipelineProxy &);
};
}    // namespace http
}    // namespace litebus
#endif
