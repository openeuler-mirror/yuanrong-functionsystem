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

#ifndef __LITEBUS_HTTPCLIENT_H__
#define __LITEBUS_HTTPCLIENT_H__

#include <map>
#include <string>

#include "actor/msg.hpp"
#include "evloop/evloop.hpp"
#include "httpd/http.hpp"
#include "iomgr/linkmgr.hpp"

namespace litebus {
namespace http {

namespace clientUtil {
void CloseConnection(int conSeq);
}
class HttpClient {
public:
    explicit HttpClient();
    virtual ~HttpClient();

    // Create http connection for persistent request
    Future<int> Connect(const litebus::http::URL &url,
                        const litebus::Option<std::string> &credencial = litebus::None());

    Future<bool> LaunchRequest(const litebus::http::Request &request, const int &conSequence);

    // Disconnect http connection
    Future<bool> Disconnect(int conSeq);

    // Start event pool
    bool Initialize();

    static HttpClient *GetInstance()
    {
        static HttpClient single = HttpClient();
        return &single;
    }

    static Future<int> RegisterResponseCallBack(int conSeq, const ResponseCallback &callback);

private:
    static int RecvRsp(Connection *connection, uint32_t size);
    static void RecvMsg(struct bufferevent *bev, void *ctx);
    static void EventCallBack(void *context);
    static void WriteCallBack(void *context);
    static void ReadCallBack(void *context);

    void WriteMsgToBuf(MessageBase *msg, const Request &request) const;
    int AllocateConnId() const;
    Connection *CreateHttpConnection(const std::string &fromUrl, const std::string &toUrl, const std::string urlScheme,
                                     const litebus::Option<std::string> &credencial, litebus::EvLoop *connLoop);

    litebus::EvLoop *gEvloop = nullptr;

    // The key is connection sequence number, and the value is connection object, only use for
    // keep-alive request
    static std::map<int, Connection *> g_connections;
    friend void clientUtil::CloseConnection(int conSeq);
};

}    // namespace http
}    // namespace litebus

#endif
