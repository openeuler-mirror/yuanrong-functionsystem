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

#include "actor/buslog.hpp"
#include "openssl/err.h"
#include "openssl/rand.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#include <cstdio>

#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "litebus.hpp"

#include "openssl_wrapper.hpp"
#include "tcp/tcp_socket.hpp"
#include "ssl_socket.hpp"

using std::string;

namespace litebus {

constexpr int SSL_DO_HANDSHAKE_OK = 1;

constexpr int SSL_CHECK_BUF_LEN = 6;

constexpr int SSL_CHECK_LEN_MIN = 2;

constexpr int SSL2_CHECK_FIRST_BYTE = 0x80;
constexpr int SSL2_CHECK_FISRT_INDEX = 0;
constexpr int SSL2_CHECK_HELLO_INDEX = 2;

constexpr int SSL3_CHECK_HANDSHAKE_INDEX = 0;

constexpr int SSL3_CHECK_VERSION_INDEX = 1;

constexpr int SSL3_CHECK_HELLO_INDEX = 5;

int SSLSocketOperate::Pending(Connection *connection)
{
    return SSL_pending(connection->ssl);
}

int SSLSocketOperate::RecvPeek(Connection *connection, char *recvBuf, uint32_t recvLen)
{
    return SSL_peek(connection->ssl, recvBuf, recvLen);
}

int RecvWithError(const SSL *ssl, Connection *connection, int retval, const uint32_t totRecvLen, const uint32_t recvLen)
{
    if (retval < 0 && errno == EAGAIN) {
        return recvLen;
    }
    int err = SSL_get_error(ssl, retval);
    switch (err) {
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return recvLen;
        default:
            connection->errCode = err;
            BUSLOG_DEBUG("recv fail, fd:{},msglen:{},recvlen:{},retval:{},sslerr:{},errno:{}", SSL_get_fd(ssl),
                         totRecvLen, recvLen, retval, err, errno);
            return -1;
    }
}

int SSLSocketOperate::Recv(Connection *connection, char *recvBuf, uint32_t totRecvLen, uint32_t &recvLen)
{
    int retval;
    char *curRecvBuf = recvBuf;
    SSL *ssl = connection->ssl;
    recvLen = 0;
    while (recvLen != totRecvLen) {
        retval = SSL_read(ssl, curRecvBuf, totRecvLen - recvLen);
        if (retval > 0) {
            recvLen += static_cast<unsigned int>(retval);
            if (recvLen == totRecvLen) {
                return totRecvLen;
            }

            curRecvBuf = static_cast<char *>(curRecvBuf) + retval;
        } else {
            return RecvWithError(ssl, connection, retval, totRecvLen, recvLen);
        }
    }

    return recvLen;
}

int SSLSocketOperate::Recvmsg(Connection *connection, struct msghdr *recvMsg, uint32_t totRecvLen)
{
    int retval;
    uint32_t recvLen;
    SSL *ssl = connection->ssl;

    recvLen = 0;
    while (recvLen != totRecvLen) {
        retval = SSL_read(ssl, recvMsg->msg_iov[0].iov_base, recvMsg->msg_iov[0].iov_len);
        if (retval > 0) {
            recvLen += static_cast<unsigned int>(retval);
            if (recvLen == totRecvLen) {
                recvMsg->msg_iovlen = 0;
                break;
            }

            if (recvMsg->msg_iov[0].iov_len > static_cast<size_t>(retval)) {
                recvMsg->msg_iov[0].iov_len -= static_cast<unsigned int>(retval);
                recvMsg->msg_iov[0].iov_base = static_cast<char *>(recvMsg->msg_iov[0].iov_base) + retval;
            } else {
                recvMsg->msg_iov = &recvMsg->msg_iov[1];
                recvMsg->msg_iovlen -= 1;
            }
        } else {
            return RecvWithError(ssl, connection, retval, totRecvLen, recvLen);
        }
    }

    return recvLen;
}

int SendWithError(const SSL *ssl, int retval, const uint32_t msglen, const uint32_t sendlen)
{
    if (retval < 0 && errno == EAGAIN) {
        return 0;
    }
    int err = SSL_get_error(ssl, retval);
    switch (err) {
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return 0;
        default:
            BUSLOG_DEBUG("send fail, fd:{},msglen:{},sendlen:{},retval:{},sslerrno:{},errno:{}",
                         ssl == nullptr ? -1 : SSL_get_fd(ssl), msglen, sendlen, retval, err, errno);
            return -1;
    }
}

int SSLSocketOperate::SslSend(SSL *ssl, void *buf, uint32_t &len)
{
    uint32_t msglen = len;
    int retval;
    char *pTmpBuf = static_cast<char *>(buf);
    while (len) {
        retval = SSL_write(ssl, static_cast<const char *>(pTmpBuf), len);
        if (retval > 0) {
            len -= static_cast<unsigned int>(retval);
            pTmpBuf = pTmpBuf + retval;
        } else {
            return SendWithError(ssl, retval, msglen, len);
        }
    }

    return 0;
}

int SSLSocketOperate::Sendmsg(Connection *connection, struct msghdr *sendMsg, uint32_t &sendLen)
{
    uint32_t totalLen = sendLen;
    int retval;
    SSL *ssl = connection->ssl;

    while (sendLen) {
        retval = SSL_write(ssl, sendMsg->msg_iov[0].iov_base, sendMsg->msg_iov[0].iov_len);
        if (retval > 0) {
            sendLen -= static_cast<unsigned int>(retval);

            if (sendLen == 0) {
                sendMsg->msg_iovlen = 0;
                break;
            }

            if (sendMsg->msg_iov[0].iov_len > static_cast<size_t>(retval)) {
                sendMsg->msg_iov[0].iov_len -= static_cast<unsigned int>(retval);
                sendMsg->msg_iov[0].iov_base = static_cast<char *>(sendMsg->msg_iov[0].iov_base) + retval;
            } else {
                sendMsg->msg_iov = &sendMsg->msg_iov[1];
                sendMsg->msg_iovlen -= 1;
            }
        } else {
            return SendWithError(ssl, retval, totalLen, sendLen);
        }
    }

    return totalLen - sendLen;
}

void SSLSocketOperate::Close(Connection *connection)
{
    if (connection->ssl != nullptr) {
        (void)SSL_clear(connection->ssl);
        SSL_free(connection->ssl);
        connection->ssl = nullptr;
    }

    (void)close(connection->fd);

    connection->fd = -1;
}

void SSLSocketOperate::ConnHandShake(int fd, Connection *conn) const
{
    if (conn->connState == ConnectionState::CONNECTED) {
        return;
    }

    SSL *ssl = static_cast<SSL *>(conn->ssl);
    int retval = SSL_do_handshake(ssl);
    if (retval == SSL_DO_HANDSHAKE_OK) {
        BUSLOG_DEBUG("SSL Server HandShake SUCC, fd:{}", fd);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#ifdef SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS
        if (ssl->s3) {
            ssl->s3->flags |= SSL3_FLAGS_NO_RENEGOTIATE_CIPHERS;
        }
#endif
#endif
        (void)conn->recvEvloop->ModifyFdEvent(
            fd, static_cast<unsigned int>(EPOLLIN) | static_cast<unsigned int>(EPOLLHUP) |
                    static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLRDHUP));
        conn->connState = ConnectionState::CONNECTED;
        return;
    }

    int err = SSL_get_error(ssl, retval);
    if (err == SSL_ERROR_WANT_WRITE) {
        BUSLOG_DEBUG("SSL HandShake SSL_ERROR_WANT_WRITE");
        (void)conn->recvEvloop->ModifyFdEvent(
            fd, static_cast<unsigned int>(EPOLLOUT) | static_cast<unsigned int>(EPOLLHUP) |
                    static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLRDHUP));
    } else if (err == SSL_ERROR_WANT_READ) {
        BUSLOG_DEBUG("SSL HandShake SSL_ERROR_WANT_READ");
        (void)conn->recvEvloop->ModifyFdEvent(
            fd, static_cast<unsigned int>(EPOLLIN) | static_cast<unsigned int>(EPOLLHUP) |
                    static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLRDHUP));
    } else {
        if (LOG_CHECK_EVERY_N()) {
            BUSLOG_INFO("SSL HandShake, retval:{},error:{},errno:{},fd:{},to:{}", retval, err, errno, fd, conn->to);
        } else {
            BUSLOG_DEBUG("SSL HandShake, retval:{},error:{},errno:{},fd:{},to:{}", retval, err, errno, fd, conn->to);
        }

        conn->errCode = err;
        conn->connState = ConnectionState::DISCONNECTING;
    }
}

bool SSLSocketOperate::SSLCheck(int fd) const
{
    unsigned char data[SSL_CHECK_BUF_LEN] = { 0 };

    ssize_t peekSize = recv(fd, data, SSL_CHECK_BUF_LEN, MSG_PEEK);

    bool isSSL = false;

    bool isSSL2 = (data[SSL2_CHECK_FISRT_INDEX] & SSL2_CHECK_FIRST_BYTE) &&
                   data[SSL2_CHECK_HELLO_INDEX] == SSL2_MT_CLIENT_HELLO;

    bool isSSL3 = data[SSL3_CHECK_HANDSHAKE_INDEX] == SSL3_RT_HANDSHAKE &&
                  data[SSL3_CHECK_VERSION_INDEX] == SSL3_VERSION_MAJOR &&
                  data[SSL3_CHECK_HELLO_INDEX] == SSL3_MT_CLIENT_HELLO;

    if (peekSize >= SSL_CHECK_LEN_MIN && (isSSL2 || isSSL3)) {
        isSSL = true;
    }

    return isSSL;
}

// accept new conn event handle
void SSLSocketOperate::NewConnEventHandler(int fd, uint32_t events, void *context)
{
    uint32_t error = events & (static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLHUP) |
                               static_cast<unsigned int>(EPOLLRDHUP));
    Connection *conn = static_cast<Connection *>(context);
    SSL *ssl = static_cast<SSL *>(conn->ssl);

    if (error) {
        BUSLOG_DEBUG("epoll return with error]fd:{},error:{}", fd, error);
        conn->connState = ConnectionState::DISCONNECTING;
        return;
    }

    if (conn->ssl == nullptr) {
        ssl = SSL_new((openssl::SslCtx(false, "")));
        if (ssl == nullptr) {
            BUSLOG_ERROR("SSL_new fail,fd:{}", fd);
            conn->connState = ConnectionState::DISCONNECTING;
            return;
        }

        (void)SSL_set_fd(ssl, fd);
        SSL_set_accept_state(ssl);
        conn->ssl = ssl;
    }

    ConnHandShake(fd, conn);

    return;
}

void SSLSocketOperate::ConnEstablishedEventHandler(int fd, uint32_t events, void *context)
{
    uint32_t error = events & (static_cast<unsigned int>(EPOLLERR) | static_cast<unsigned int>(EPOLLHUP) |
                               static_cast<unsigned int>(EPOLLRDHUP));
    Connection *conn = static_cast<Connection *>(context);
    SSL *ssl = static_cast<SSL *>(conn->ssl);

    if (error) {
        BUSLOG_DEBUG("epoll return with error, fd:{},error:{}", fd, error);
        conn->connState = ConnectionState::DISCONNECTING;
        return;
    }

    if (ssl == nullptr) {
        ssl = SSL_new(openssl::SslCtx(true, conn->credencial));
        if (ssl == nullptr) {
            BUSLOG_ERROR("SSL_new fail,fd:{}", fd);
            conn->connState = ConnectionState::DISCONNECTING;
            return;
        }

        (void)SSL_set_fd(ssl, fd);
        SSL_set_connect_state(ssl);

        conn->ssl = ssl;
    }

    ConnHandShake(fd, conn);

    return;
}

}    // namespace litebus
