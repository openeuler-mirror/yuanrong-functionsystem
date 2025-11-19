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

#ifndef COMMON_UTILS_CHECK_LINK_H
#define COMMON_UTILS_CHECK_LINK_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "common/logs/logging.h"

namespace functionsystem {
[[maybe_unused]] static bool WaitConnect(int sockfd, const std::string &ip, uint16_t port)
{
    // use select ot set timeout
    fd_set writeFds;
    FD_ZERO(&writeFds);
    FD_SET(sockfd, &writeFds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;  // 500000: connect timeout

    int selectResult = select(sockfd + 1, nullptr, &writeFds, nullptr, &timeout);
    if (selectResult == 0) {
        YRLOG_ERROR("Connection({}:{}) timeout.", ip, port);
        return false;
    } else if (selectResult < 0) {
        YRLOG_ERROR("Error in select({}:{}): {}", ip, port, errno);
        return false;
    }

    // checkout connect result
    int socketError;
    socklen_t socketErrorSize = sizeof(socketError);
    (void)getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorSize);
    if (socketError != 0) {
        YRLOG_ERROR("Error in connection({}:{}): {}", ip, port, socketError);
        return false;
    }
    return true;
}

[[maybe_unused]] static bool CheckIPandPort(const std::string &ip, uint16_t port)
{
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    (void)inet_pton(AF_INET, ip.c_str(), &(serverAddr.sin_addr));

    int sockfd = socket(AF_INET, static_cast<int>(SOCK_STREAM), 0);
    if (sockfd < 0) {
        YRLOG_ERROR("Error creating socket for({}:{}): {}", ip, port, errno);
        return false;
    }

    // set socket no block
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        YRLOG_ERROR("Error calling fcntl for({}:{}): {}", ip, port, errno);
        (void)close(sockfd);
        return false;
    }
    (void)fcntl(sockfd, F_SETFL, static_cast<uint32_t>(flags) | O_NONBLOCK);

    // connect server
    int connectResult = connect(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
    if (connectResult < 0 && errno != EINPROGRESS) {
        YRLOG_ERROR("Error connecting to server({}:{}): {}", ip, port, errno);
        (void)close(sockfd);
        return false;
    }

    if (!WaitConnect(sockfd, ip, port)) {
        (void)close(sockfd);
        return false;
    }

    YRLOG_DEBUG("Connection to {}:{} is available", ip, port);
    (void)close(sockfd);
    return true;
}

}  // namespace functionsystem

#endif