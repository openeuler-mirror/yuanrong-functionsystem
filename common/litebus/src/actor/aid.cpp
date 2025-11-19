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

#include "actor/aid.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <securec.h>

namespace litebus {

constexpr int PORTMINNUMBER = 0;
constexpr int PORTMAXNUMBER = 65535;
constexpr int PROCOLLEN = 3;    // strlen("://");

void AID::SetUnfixUrl()
{
    size_t index = url.find("://");
    if (index != std::string::npos) {
        if (url.substr(0, index) == BUS_TCP) {
            url = url.substr(index + PROCOLLEN);
        }
    }
}

AID::AID(const char *tmpName)
{
    std::string sName = tmpName;
    size_t index = sName.find("@");
    if (index == std::string::npos) {
        name = sName;
        url = "";
    } else {
        name = sName.substr(0, index);
        url = sName.substr(index + 1);
        SetUnfixUrl();
    }
}

AID::AID(const std::string &tmpName)
{
    size_t index = tmpName.find("@");
    if (index == std::string::npos) {
        name = tmpName;
        url = "";
    } else {
        name = tmpName.substr(0, index);
        url = tmpName.substr(index + 1);
        SetUnfixUrl();
    }
}

bool IsValidHost(const std::string &host)
{
    struct addrinfo hints;
    (void)memset_s(&hints, sizeof hints, 0, sizeof hints);
    struct addrinfo *res;
    int result = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (result != 0) {
        return false;
    }
    freeaddrinfo(res);
    return true;
}

bool AID::OK() const
{
    unsigned char buf[sizeof(struct in6_addr)];
    bool ipOK = inet_pton(AF_INET, GetIp().c_str(), buf) > 0 || inet_pton(AF_INET6, GetIp().c_str(), buf) > 0;
    bool hostOK = IsValidHost(GetIp());
    std::string proto = GetProtocol();
#ifdef UDP_ENABLED
    bool protoOK = (proto == BUS_TCP) || (proto == BUS_UDP);
#else
    bool protoOK = (proto == BUS_TCP);
#endif
    int port = GetPort();
    bool portOK = port > PORTMINNUMBER && port < PORTMAXNUMBER;
    return (ipOK || hostOK) && protoOK && portOK && name != "";
}
AID &AID::operator=(const AID &id)
{
    if (&id != this) {
        name = id.name;
        url = id.url;
    }
    return *this;
}

void AID::SetProtocol(const std::string &protocol)
{
    size_t index = url.find("://");
    if (index != std::string::npos) {
        if (protocol == BUS_TCP) {
            url = url.substr(index + PROCOLLEN);
        } else {
            url = protocol + url.substr(index);
        }
        return;
    }
    if (protocol != BUS_TCP) {
        url = protocol + "://" + url;
    }
}

std::string AID::GetProtocol() const
{
    size_t index = url.find("://");
    if (index != std::string::npos) {
        return url.substr(0, index);
    } else {
        return "tcp";
    }
}

std::string AID::GetIp() const
{
    if (url.empty()) {
        return url;
    }

    size_t index1 = url.find("://");
    if (index1 == std::string::npos) {
        index1 = 0;
    } else {
        index1 = index1 + PROCOLLEN;
    }
    size_t index2 = url.rfind(':');
    if ((index2 == std::string::npos) || (index2 < index1)) {
        BUSLOG_INFO("wrong url:{}", url);
        return url;
    } else {
        return url.substr(index1, index2 - index1);
    }
}

uint16_t AID::GetPort() const
{
    if (url.empty()) {
        return 0;
    }

    size_t index = url.rfind(':');
    uint16_t port = 0;
    try {
        auto ulPort = std::stoul(url.substr(index + 1));
        if (ulPort > UINT16_MAX) {
            BUSLOG_ERROR("port({}) out of range [0, {}]", ulPort, UINT16_MAX);
            return port;
        }
        port = static_cast<uint16_t>(ulPort);
    } catch (const std::exception &e) {
        BUSLOG_ERROR("actor {} has wrong url:{}, error: {}", name, url, e.what());
    }
    return port;
}

};    // end of namespace litebus
