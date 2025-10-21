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

#include <arpa/inet.h>
#include <netdb.h>
#include <securec.h>
#include <csignal>
#include <system_error>

#include "actor/buslog.hpp"
#include "actor/iomgr.hpp"

#include "socket_operate.hpp"

namespace litebus {
constexpr int IP_LEN_MAX = 128;

int SocketOperate::SetSocketKeepAlive(int fd, int keepalive, int keepidle, int keepinterval, int keepcount)
{
    int optionVal;
    int ret;

    optionVal = keepalive;
    ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optionVal, sizeof(optionVal));
    if (ret < 0) {
        BUSLOG_ERROR("setsockopt SO_KEEPALIVE fail, fd:{},errno:{}", fd, errno);
        return -1;
    }

    // Send first probe after `interval' seconds.
    optionVal = keepidle;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &optionVal, sizeof(optionVal));
    if (ret < 0) {
        BUSLOG_ERROR("setsockopt TCP_KEEPIDLE fail, fd:{},errno:{}", fd, errno);
        return -1;
    }

    // Send next probes after the specified interval.
    optionVal = keepinterval;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &optionVal, sizeof(optionVal));
    if (ret < 0) {
        BUSLOG_ERROR("setsockopt TCP_KEEPINTVL fail, fd:{},errno:{}", fd, errno);
        return -1;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    optionVal = keepcount;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &optionVal, sizeof(optionVal));
    if (ret < 0) {
        BUSLOG_ERROR("setsockopt TCP_KEEPCNT fail, fd:{},errno:{}", fd, errno);

        return -1;
    }

    return 0;
}

int SocketOperate::SetSocket(int fd)
{
    int optionVal = 1;
    int ret;

    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optionVal, sizeof(optionVal));
    if (ret) {
        BUSLOG_ERROR("setsockopt SO_REUSEADDR fail, fd:{},errno:{}", fd, errno);
        return -1;
    }

    ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optionVal, sizeof(optionVal));
    if (ret) {
        BUSLOG_ERROR("setsockopt TCP_NODELAY fail, fd:{},errno:{}", fd, errno);
        return -1;
    }
    ret = SetSocketKeepAlive(fd, SOCKET_KEEPALIVE, SOCKET_KEEPIDLE, SOCKET_KEEPINTERVAL, SOCKET_KEEPCOUNT);
    if (ret) {
        BUSLOG_WARN("setsockopt keep alive fail, fd:{}", fd);
    }
    return 0;
}

int SocketOperate::CreateSocket(sa_family_t family)
{
    int ret;
    int fd;

    // create server socket
    fd = ::socket(family,
                  static_cast<int>(SOCK_STREAM) | static_cast<int>(SOCK_NONBLOCK) | static_cast<int>(SOCK_CLOEXEC), 0);
    if (fd < 0) {
        BUSLOG_WARN("create socket fail:{}", errno);
        return -1;
    }

    ret = SetSocket(fd);
    if (ret < 0) {
        (void)close(fd);
        return -1;
    }

    return fd;
}

// protocol://ip:port
// protocol://[ip]:port
uint16_t SocketOperate::GetPort(const std::string &url)
{
    uint16_t port;

    size_t index = url.rfind(URL_IP_PORT_SEPARATOR);
    if (index == std::string::npos) {
        BUSLOG_ERROR("not found ':' from {}", url);
        return 0;
    }

    try {
        auto ulPort = std::stoul(url.substr(index + URL_IP_PORT_SEPARATOR.length()));
        if (ulPort > UINT16_MAX) {
            BUSLOG_ERROR("port({}) out of range [0, {}]", ulPort, UINT16_MAX);
            return 0;
        }
        port = static_cast<uint16_t>(ulPort);
    } catch (...) {
        BUSLOG_ERROR("not found port from {}", url);
        return 0;
    }

    return port;
}

std::string SocketOperate::GetIP(const std::string &url)
{
    size_t index1 = url.find("[");
    if (index1 == std::string::npos) {
        index1 = url.find(URL_PROTOCOL_IP_SEPARATOR);
        if (index1 == std::string::npos) {
            index1 = 0;
        } else {
            index1 = index1 + URL_PROTOCOL_IP_SEPARATOR.length();
        }
    } else {
        index1 = index1 + 1;
    }

    size_t index2 = url.find("]");
    if (index2 == std::string::npos) {
        index2 = url.rfind(URL_IP_PORT_SEPARATOR);
        if (index2 == std::string::npos) {
            BUSLOG_ERROR("not found ':' from {}", url);
            return "";
        }
    }

    if (index1 > index2) {
        BUSLOG_ERROR("parse ip failed from {}", url);
        return "";
    }

    std::string ip = url.substr(index1, index2 - index1);
    IOSockaddr addr;

    int result = inet_pton(AF_INET, ip.c_str(), &addr.saIn.sin_addr);
    if (result <= 0) {
        result = inet_pton(AF_INET6, ip.c_str(), &addr.saIn6.sin6_addr);
        if (result <= 0) {
            if (GetIPFromHostname(ip, 0, addr)) {
                return ip;
            }
            BUSLOG_ERROR("parse ip failed, result:{},url:{}", result, url);
            return "";
        }
    }

    return ip;
}

bool SocketOperate::GetIPFromHostname(const std::string &hostName, const uint16_t port, litebus::IOSockaddr &addr)
{
    struct addrinfo hints;
    (void)memset_s(&hints, sizeof hints, 0, sizeof hints);
    struct addrinfo *res;
    int result;
    if ((result = getaddrinfo(hostName.c_str(), NULL, &hints, &res)) != 0) {
        BUSLOG_WARN("parse hostname failed, result:{},hostname:{}", result, hostName);
        return false;
    }
    if (res->ai_family == AF_INET) {    // IPv4
        struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(res->ai_addr);
        addr.saIn.sin_addr = ipv4->sin_addr;
        addr.saIn.sin_family = AF_INET;
        addr.saIn.sin_port = htons(port);
    } else if (res->ai_family == AF_INET6) {    // IPv6
        struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>(res->ai_addr);
        addr.saIn6.sin6_addr = ipv6->sin6_addr;
        addr.saIn6.sin6_family = AF_INET6;
        addr.saIn6.sin6_port = htons(port);
    } else {
        BUSLOG_WARN("parse hostname failed, invalid family:{},hostname:{}", res->ai_family, hostName);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);
    return true;
}

bool SocketOperate::GetSockAddr(const std::string &url, IOSockaddr &addr)
{
    std::string ip;
    uint16_t port;

    // 1. dest is valid 2. destsz equals to count and both are valid.
    // memset_s will always executes successfully.
    (void)memset_s(&addr, sizeof(addr), 0, sizeof(addr));

    size_t index1 = url.find(URL_PROTOCOL_IP_SEPARATOR);
    if (index1 == std::string::npos) {
        index1 = 0;
    } else {
        index1 = index1 + URL_PROTOCOL_IP_SEPARATOR.length();
    }

    size_t index2 = url.rfind(':');
    if (index2 == std::string::npos) {
        BUSLOG_ERROR("Couldn't find the charater colon.");
        return false;
    }

    ip = url.substr(index1, index2 - index1);
    if (ip.empty()) {
        BUSLOG_ERROR("Couldn't find ip in url:{}", url);
        return false;
    }

    try {
        auto ulPort = std::stoul(url.substr(index2 + URL_IP_PORT_SEPARATOR.length()));
        if (ulPort > UINT16_MAX) {
            BUSLOG_ERROR("port({}) out of range [0, {}]", ulPort, UINT16_MAX);
            return false;
        }
        port = static_cast<uint16_t>(ulPort);
    } catch (const std::exception &e) {
        BUSLOG_ERROR("Couldn't find port in url:{}, error: {}", url, e.what());
        return false;
    }

    int result = inet_pton(AF_INET, ip.c_str(), &addr.saIn.sin_addr);
    if (result > 0) {
        addr.saIn.sin_family = AF_INET;
        addr.saIn.sin_port = htons(port);
        return true;
    }

    result = inet_pton(AF_INET6, ip.c_str(), &addr.saIn6.sin6_addr);
    if (result > 0) {
        addr.saIn6.sin6_family = AF_INET6;
        addr.saIn6.sin6_port = htons(port);
        return true;
    }

    if (GetIPFromHostname(ip, port, addr)) {
        return true;
    }

    BUSLOG_ERROR("parse ip failed,result:{},url:{}", result, url);

    return false;
}

uint16_t SocketOperate::GetFdPort(int fd)
{
    uint16_t port = 0;
    int retval;
    union IOSockaddr isa;
    socklen_t isaLen = sizeof(struct sockaddr_storage);

    retval = getsockname(fd, &isa.sa, &isaLen);
    if (retval) {
        BUSLOG_INFO("getsockname fail, fd:{},ret:{},errno:{}", fd, retval, errno);
        return port;
    }

    if (isa.sa.sa_family == AF_INET) {
        port = ntohs(isa.saIn.sin_port);
    } else if (isa.sa.sa_family == AF_INET6) {
        port = ntohs(isa.saIn6.sin6_port);
    } else {
        BUSLOG_INFO("getsockname unknown, fd:{},family:{}", fd, isa.sa.sa_family);
    }

    return port;
}

std::string SocketOperate::GetFdPeer(int fd)
{
    std::string peer;
    int retval;
    union IOSockaddr isa;
    socklen_t isaLen = sizeof(struct sockaddr_storage);

    retval = getpeername(fd, &isa.sa, &isaLen);
    if (retval < 0) {
        BUSLOG_INFO("getpeername fail, fd:{},ret:{},errno:{}", fd, retval, errno);
        return peer;
    }

    char ipdotdec[IP_LEN_MAX];
    if (isa.sa.sa_family == AF_INET) {
        (void)inet_ntop(AF_INET, static_cast<void *>(&isa.saIn.sin_addr), ipdotdec, IP_LEN_MAX);
        peer = std::string(ipdotdec) + ":" + std::to_string(ntohs(isa.saIn.sin_port));
        BUSLOG_DEBUG("getpeername for ipv4 after accept, fd:{},peer:{}", fd, peer);
    } else if (isa.sa.sa_family == AF_INET6) {
        (void)inet_ntop(AF_INET6, static_cast<void *>(&isa.saIn6.sin6_addr), ipdotdec, IP_LEN_MAX);
        peer = std::string(ipdotdec) + ":" + std::to_string(ntohs(isa.saIn6.sin6_port));
        BUSLOG_DEBUG("getpeername for ipv6 after accept, fd:{},peer:{}", fd, peer);
    } else {
        BUSLOG_INFO("getpeername unknown, fd:{},family:{}", fd, isa.sa.sa_family);
    }

    return peer;
}

int SocketOperate::Connect(int fd, const struct sockaddr *sa, socklen_t saLen, uint16_t &boundPort)
{
    int retval;

    retval = connect(fd, sa, saLen);
    if (retval != 0) {
        if (errno == EINPROGRESS) {
            BUSLOG_DEBUG("connect in progress,fd:{},ret:{},errno:{}", fd, retval, errno);
            /* set iomux for write event */
        } else {
            BUSLOG_ERROR("tcp connect fail,fd:{},ret:{},errno:{}", fd, retval, errno);
            return retval;
        }
    } else {
        BUSLOG_DEBUG("connect, fd:{},ret:{},errno:{}", fd, retval, errno);
        /* handle in ev_handler */
    }

    // to get local port
    boundPort = GetFdPort(fd);
    if (boundPort == 0) {
        return BUS_ERROR;
    }

    BUSLOG_DEBUG("connect ok, fd:{},localport:{}", fd, boundPort);
    return BUS_OK;
}

int SocketOperate::Listen(const std::string &url)
{
    int listenFd;

    IOSockaddr addr;

    if (!GetSockAddr(url, addr)) {
        return -1;
    }

    // create server socket
    listenFd = CreateSocket(addr.sa.sa_family);
    if (listenFd < 0) {
        BUSLOG_ERROR("create socket fail, url:{}", url);
        return -1;
    }

    // bind
    if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(IOSockaddr))) {
        BUSLOG_ERROR("bind fail, fd:{},errno:{},url:{}", listenFd, errno, url);
        (void)close(listenFd);
        return -1;
    }

    // listen
    if (::listen(listenFd, SOCKET_LISTEN_BACKLOG)) {
        BUSLOG_ERROR("listen fail, fd:{},errno:{},url:{}", listenFd, errno, url);
        (void)close(listenFd);
        return -1;
    }

    return listenFd;
}

int SocketOperate::Accept(int server)
{
    struct sockaddr storage;
    socklen_t length = sizeof(storage);

    // accept connection
    auto acceptFd = ::accept4(server, &storage, &length,
                              static_cast<int>(SOCK_NONBLOCK) | static_cast<int>(SOCK_CLOEXEC));
    if (acceptFd < 0) {
        BUSLOG_ERROR("accept fail,errno:{},server:{}", errno, server);
        return acceptFd;
    }
    (void)SetSocket(acceptFd);

    return acceptFd;
}

}    // namespace litebus
