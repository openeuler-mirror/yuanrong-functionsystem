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

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <securec.h>
#include <csignal>
#include <unistd.h>

#include <arpa/inet.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "actor/aid.hpp"
#include "actor/buslog.hpp"
#include "actor/msg.hpp"

#include "tcp_socket.hpp"

namespace litebus {

constexpr int EAGAIN_RETRY = 2;

int TCPSocketOperate::Pending(Connection *)
{
    return 0;
}

int TCPSocketOperate::RecvPeek(Connection *connection, char *recvBuf, uint32_t recvLen)
{
    if (connection == nullptr) {
        return -1;
    }

    return recv(connection->fd, recvBuf, recvLen, MSG_PEEK);
}

int TCPSocketOperate::Recv(Connection *connection, char *recvBuf, uint32_t totRecvLen, uint32_t &recvLen)
{
    if (connection == nullptr) {
        return -1;
    }

    int retval;
    char *curRecvBuf = recvBuf;
    int fd = connection->fd;

    recvLen = 0;
    while (recvLen != totRecvLen) {
        retval = recv(fd, curRecvBuf, totRecvLen - recvLen, 0);
        if (retval > 0) {
            recvLen += static_cast<unsigned int>(retval);
            if (recvLen == totRecvLen) {
                return totRecvLen;
            }

            curRecvBuf = static_cast<char *>(curRecvBuf) + retval;
        } else if (retval < 0) {
            if (EAGAIN == errno) {
                BUSLOG_DEBUG("recv msg EAGAIN, fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, totRecvLen, recvLen,
                             errno);
                return recvLen;
            } else if (ECONNRESET == errno || ECONNABORTED == errno || ENOTCONN == errno || EPIPE == errno) {
                BUSLOG_DEBUG("recv msg failed, fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, totRecvLen, recvLen,
                             errno);
                connection->errCode = errno;
                return -1;
            } else {
                BUSLOG_DEBUG("recv msg failed, fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, totRecvLen, recvLen,
                             errno);
                return recvLen;
            }
        } else {
            // errno is not EAGAIN
            BUSLOG_DEBUG("tcp transport got EOF, fd:{},recvlen:{},hasrecvlen:{}", fd, totRecvLen, recvLen);
            connection->errCode = errno;
            return -1;
        }
    }

    return recvLen;
}

int TCPSocketOperate::Recvmsg(Connection *connection, struct msghdr *recvMsg, uint32_t recvLen)
{
    unsigned int i;
    uint32_t totalRecvLen = recvLen;
    int retval;
    unsigned int tmpLen;
    int fd = connection->fd;

    if (totalRecvLen == 0) {
        return 0;
    }

    while (totalRecvLen) {
        retval = recvmsg(fd, recvMsg, 0);
        if (retval <= 0) {
            int recvRet = TraceRecvmsgErr(retval, fd, recvLen, recvLen - totalRecvLen);
            if (recvRet == -1) {
                connection->errCode = errno;
            }
            return recvRet;
        }

        totalRecvLen -= static_cast<unsigned int>(retval);
        if (totalRecvLen == 0) {
            recvMsg->msg_iovlen = 0;
            break;
        }

        tmpLen = 0;
        for (i = 0; i < recvMsg->msg_iovlen; i++) {
            if (recvMsg->msg_iov[i].iov_len + tmpLen > static_cast<size_t>(retval)) {
                recvMsg->msg_iov[i].iov_len -= (static_cast<unsigned int>(retval) - tmpLen);
                recvMsg->msg_iov[i].iov_base =
                    static_cast<char *>(recvMsg->msg_iov[i].iov_base) + static_cast<unsigned int>(retval) - tmpLen;
                recvMsg->msg_iov = &recvMsg->msg_iov[i];
                recvMsg->msg_iovlen -= i;
                break;
            }
            tmpLen += recvMsg->msg_iov[i].iov_len;
        }

        int recvRet = TraceRecvmsgErr(retval, fd, recvLen, recvLen - totalRecvLen);
        if (recvRet == -1) {
            connection->errCode = errno;
        }
        return recvRet;
    }

    return recvLen;
}

int TCPSocketOperate::Sendmsg(Connection *connection, struct msghdr *sendMsg, uint32_t &sendLen)
{
    int retval = 0;
    unsigned int tmpBytes;
    int eagainCount = EAGAIN_RETRY;
    unsigned int i;
    uint32_t totalLen = sendLen;
    int fd = connection->fd;

    while (sendLen != 0) {
        retval = sendmsg(fd, sendMsg, MSG_NOSIGNAL);
        if (retval < 0) {
            if (errno != EAGAIN) {
                BUSLOG_DEBUG("send msg failed, fd:{},errno:{}", fd, errno);
                connection->errCode = errno;
                return -1;
            } else if (eagainCount-- == 0) {
                BUSLOG_DEBUG("send msg failed, fd:{},errno:{}", fd, errno);
                return 0;
            }
            continue;
        }

        sendLen -= static_cast<uint32_t>(retval);
        if (sendLen == 0) {
            sendMsg->msg_iovlen = 0;
            break;
        }
        tmpBytes = 0;
        for (i = 0; i < sendMsg->msg_iovlen; i++) {
            if (sendMsg->msg_iov[i].iov_len + tmpBytes >= static_cast<size_t>(retval)) {
                sendMsg->msg_iov[i].iov_len -= (static_cast<unsigned int>(retval) - tmpBytes);
                sendMsg->msg_iov[i].iov_base =
                    static_cast<char *>(sendMsg->msg_iov[i].iov_base) + (unsigned)(int)retval - tmpBytes;
                sendMsg->msg_iov = &sendMsg->msg_iov[i];
                sendMsg->msg_iovlen -= i;
                break;
            }
            tmpBytes += sendMsg->msg_iov[i].iov_len;
        }
        eagainCount = EAGAIN_RETRY;
    }

    return totalLen - sendLen;
}

void TCPSocketOperate::Close(Connection *connection)
{
    (void)close(connection->fd);
    connection->fd = -1;
}

// accept new conn event handle
void TCPSocketOperate::NewConnEventHandler(int, uint32_t, void *context)
{
    Connection *conn = static_cast<Connection *>(context);
    conn->connState = ConnectionState::CONNECTED;
    return;
}

void TCPSocketOperate::ConnEstablishedEventHandler(int, uint32_t, void *context)
{
    Connection *conn = static_cast<Connection *>(context);
    conn->connState = ConnectionState::CONNECTED;
    return;
}

int TCPSocketOperate::TraceRecvmsgErr(const int &recvRet, const int &fd, const uint32_t &recvLen,
                                      const uint32_t &hasrecvlen) const
{
    if (recvRet == 0) {
        BUSLOG_DEBUG("tcp transport got EOF, fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, recvLen, hasrecvlen, errno);
        return -1;
    }

    if (ECONNRESET == errno || ECONNABORTED == errno || ENOTCONN == errno || EPIPE == errno) {
        BUSLOG_DEBUG("recv msg failed]fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, recvLen, hasrecvlen, errno);
        return -1;
    }
    // call recv fun again with other errno
    BUSLOG_DEBUG("recv msg EAGAIN, fd:{},recvlen:{},hasrecvlen:{},errno:{}", fd, recvLen, hasrecvlen, errno);
    return hasrecvlen;
}
}    // namespace litebus
