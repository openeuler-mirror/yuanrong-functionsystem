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

#ifndef HTTP_IOMGR_H
#define HTTP_IOMGR_H

#include <memory>
#include <string>
#include <thread>

#include "httpd/http.hpp"

#include "actor/aid.hpp"
#include "actor/iomgr.hpp"
#include "actor/msg.hpp"
#include "async/option.hpp"

#include "tcp/tcpmgr.hpp"

namespace litebus {
class Connection;    // forward declaration

namespace iomgrUtil {
int RecvHttpReq(Connection *connection, IOMgr::MsgHandler msgHandler);
int RecvHttpReq(Connection *connection, IOMgr::MsgHandler msgHandler, uint32_t size);
int RecvHttpRsp(Connection *conn, IOMgr::MsgHandler msgHandler);
}    // namespace iomgrUtil

class HttpIOMgr {
public:
    static bool CheckHttpCon(int conSeq);

    static void EnableHttp();

    static void ErrorCallback(void *context);

    static void SetEventCb(Connection *connection);

    static void HandleRequest(litebus::http::Request *request, Connection *connection, IOMgr::MsgHandler msgHandler);

private:
    static void DeleteHttpProxy(int conSeq);
    static void HandleKMsgRequest(litebus::http::Request *request, Connection *connection,
                                  IOMgr::MsgHandler msgHandler);

    static void HandleDefaultRequest(litebus::http::Request *request, Connection *connection,
                                     IOMgr::MsgHandler msgHandler);

    static litebus::AID ParseHttpUrl(litebus::http::Request *request);
    static void ParseKMsgUrl(const std::string &urlPath, std::string &kmsgActorName, std::string &kmsgTypeName);
    static int AllocateConnId();
};
}    // namespace litebus
#endif
