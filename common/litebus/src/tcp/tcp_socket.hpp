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

#ifndef LITEBUS_TCP_SOCKET_H
#define LITEBUS_TCP_SOCKET_H

#include <string>

#include "actor/iomgr.hpp"
#include "evloop/evloop.hpp"
#include "iomgr/evbufmgr.hpp"
#include "iomgr/linkmgr.hpp"
#include "iomgr/socket_operate.hpp"

namespace litebus {

class TCPSocketOperate : public SocketOperate {
public:
    int Pending(Connection *connection) override;
    int RecvPeek(Connection *connection, char *recvBuf, uint32_t recvLen) override;
    int Recv(Connection *connection, char *recvBuf, uint32_t totRecvLen, uint32_t &recvLen) override;
    int Recvmsg(Connection *connection, struct msghdr *recvMsg, uint32_t recvLen) override;
    int Sendmsg(Connection *connection, struct msghdr *sendMsg, uint32_t &sendLen) override;
    void Close(Connection *connection) override;

    void NewConnEventHandler(int, uint32_t, void *context) override;
    void ConnEstablishedEventHandler(int, uint32_t, void *context) override;

private:
    int TraceRecvmsgErr(const int &recvRet, const int &fd, const uint32_t &recvLen, const uint32_t &hasrecvlen) const;
};

};    // namespace litebus

#endif
