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

#include "port_manager.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include "common/logs/logging.h"

namespace functionsystem::runtime_manager {
const static int MAX_PORT_NUM = 65535;

void PortManager::InitPortResource(int initialPort, int portNum)
{
    YRLOG_INFO("Init port resource, initial port: {}, portNum: {}", initialPort, portNum);
    portMap_.clear();
    while (portNum > 0) {
        if (portNum > MAX_PORT_NUM) {
            YRLOG_ERROR("exceed port number limit. number is {}", portNum);
            return;
        }
        RuntimeInfo info;
        info.port = initialPort;
        portMap_[initialPort] = info;
        initialPort++;
        portNum--;
    }
}

std::string PortManager::RequestPort(const std::string &runtimeID)
{
    YRLOG_INFO("runtimeID: {}, request port", runtimeID);
    if (portMap_.size() == 0) {
        YRLOG_ERROR("PortManager port map is empty, request port failed");
        return "";
    }
    std::string port;
    for (auto &iter : portMap_) {
        if (!iter.second.used) {
            if (CheckPortInUse(iter.first)) {
                YRLOG_INFO("port: {} is inuse, continue", iter.first);
                continue;
            }
            iter.second.used = true;
            iter.second.runtimeID = runtimeID;
            iter.second.port = iter.first;
            port = std::to_string(iter.first);
            break;
        }
    }
    return port;
}

bool PortManager::CheckPortInUse(int port) const
{
    int socketFd;
    struct sockaddr_in sin;
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd == -1) {
        return true;
    }
    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(static_cast<unsigned short>(port));
    inet_pton(AF_INET, "0.0.0.0", &sin.sin_addr);

    if (bind(socketFd, (struct sockaddr *)(&sin), sizeof(struct sockaddr)) < 0) {
        close(socketFd);
        return true;
    }
    close(socketFd);
    return false;
}

PortManager::PortManager()
{
    InitPortResource(initialPort_, poolSize_);
}

PortManager::~PortManager()
{
    portMap_.clear();
}

std::string PortManager::GetPort(const std::string &runtimeID) const
{
    for (const auto &iter : portMap_) {
        std::string id = iter.second.runtimeID;
        if (id == runtimeID && iter.second.used) {
            return std::to_string(iter.second.port);
        }
    }
    return "";
}

int PortManager::ReleasePort(const std::string &runtimeID)
{
    for (auto &iter : portMap_) {
        std::string id = iter.second.runtimeID;
        if (id == runtimeID) {
            iter.second.runtimeID = "";
            iter.second.used = false;
            iter.second.port = 0;
            iter.second.grpcPort = 0;
            YRLOG_INFO("port manager release port: {}, runtimeID: {}", iter.first, id);
            return 0;
        }
    }
    YRLOG_ERROR("port manager has not record this runtime resource, id: {}", runtimeID);
    return -1;
}

void PortManager::Clear()
{
    portMap_.clear();
}
}  // namespace functionsystem::runtime_manager
