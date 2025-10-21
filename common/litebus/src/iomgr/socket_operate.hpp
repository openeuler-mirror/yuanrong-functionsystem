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

#ifndef LITEBUS_SOCKET_H
#define LITEBUS_SOCKET_H

#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <string>

namespace litebus {
class Connection;
/**
 * IOSockaddr
 */
union IOSockaddr {
    struct sockaddr sa;
    struct sockaddr_in saIn;
    struct sockaddr_in6 saIn6;
    struct sockaddr_storage saStorage;
};

class SocketOperate {
public:
    virtual ~SocketOperate()
    {
    }
    static std::string GetIP(const std::string &url);
    static uint16_t GetPort(const std::string &url);
    static bool GetIPFromHostname(const std::string &hostName, const uint16_t port, IOSockaddr &addr);

    static uint16_t GetFdPort(int fd);
    static std::string GetFdPeer(int fd);
    static bool GetSockAddr(const std::string &url, IOSockaddr &addr);
    static int CreateSocket(sa_family_t family);
    static int SetSocket(int fd);
    static int SetSocketKeepAlive(int fd, int keepalive, int keepidle, int keepinterval, int keepcount);

    static int Connect(int fd, const struct sockaddr *sa, socklen_t saLen, uint16_t &boundPort);
    static int Listen(const std::string &url);
    static int Accept(int server);

    virtual int Pending(Connection *connection) = 0;
    virtual int RecvPeek(Connection *connection, char *recvBuf, uint32_t recvLen) = 0;
    virtual int Recv(Connection *connection, char *recvBuf, uint32_t totRecvLen, uint32_t &recvLen) = 0;
    virtual int Recvmsg(Connection *connection, struct msghdr *recvMsg, uint32_t recvLen) = 0;
    virtual int Sendmsg(Connection *connection, struct msghdr *sendMsg, uint32_t &sendLen) = 0;
    virtual void Close(Connection *connection) = 0;

    virtual void NewConnEventHandler(int fd, uint32_t events, void *context) = 0;
    virtual void ConnEstablishedEventHandler(int fd, uint32_t events, void *context) = 0;
};

};    // namespace litebus

#endif
