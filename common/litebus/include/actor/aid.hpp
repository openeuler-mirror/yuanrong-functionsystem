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

#ifndef __AID_HPP__
#define __AID_HPP__

#include <string>

#include "actor/buslog.hpp"

namespace litebus {

constexpr auto BUS_TCP = "tcp";
constexpr auto BUS_UDP = "udp";

class AID {
public:
    AID() : name(), url()
    {
    }

    ~AID()
    {
    }

    AID(const char *name);
    AID(const std::string &name);

    AID(const std::string &tmpName, const std::string &sUrl) : name(tmpName), url(sUrl)
    {
        SetUnfixUrl();
    }

    AID(const AID &id) : name(id.name), url(id.url), ak(id.ak)
    {
        SetUnfixUrl();
    }

    // Overloading of Assignment Operator
    AID &operator=(const AID &id);

    inline void SetUrl(const std::string &tmpUrl)
    {
        url = tmpUrl;
        SetUnfixUrl();
    }

    inline void SetName(const std::string &tmpName)
    {
        name = tmpName;
    }

    inline void SetAk(const std::string &tmpAk)
    {
        ak = tmpAk;
    }

    inline const std::string &Name() const
    {
        return name;
    }

    inline const std::string &Url() const
    {
        return url;
    }

    void SetProtocol(const std::string &protocol);
    bool OK() const;

    std::string GetProtocol() const;
    std::string GetIp() const;
    uint16_t GetPort() const;
    inline std::string UnfixUrl() const
    {
        return GetIp() + ":" + std::to_string(GetPort());
    }
    inline operator std::string() const
    {
        return name + "@" + url;
    }
    inline std::string GetAK() const
    {
        return ak;
    }
    inline std::string HashString() const
    {
        return name + "@" + UnfixUrl();
    }

private:
    void SetUnfixUrl();

    friend class Actor;

    // actor's name
    std::string name;

    /**
    tcp://ip:port
    udp://ip:port
    ip:port  (tcp)
     **/
    std::string url;

    std::string ak;
};

inline std::ostream &operator<<(std::ostream &os, const AID &aid)
{
    os << aid.Name() << "@" << aid.Url();
    return os;
}

inline bool operator==(const AID &aid1, const AID &aid2)
{
    if (aid1.GetProtocol() == BUS_TCP && aid2.GetProtocol() == BUS_TCP) {
        // NOTE : By default, http has no protocol filed, so we use 'UnfixUrl' to compare aids here
        return ((aid1.Name() == aid2.Name()) && (aid1.UnfixUrl() == aid2.UnfixUrl()));
    } else {
        return ((aid1.Name() == aid2.Name()) && (aid1.Url() == aid2.Url()));
    }
}
inline bool operator!=(const AID &aid1, const AID &aid2)
{
    return !(aid1 == aid2);
}

inline bool operator>(const AID &aid1, const AID &aid2)
{
    return aid1.HashString() > aid2.HashString();
}
inline bool operator<(const AID &aid1, const AID &aid2)
{
    return aid1.HashString() < aid2.HashString();
}

};    // end of namespace litebus

// custom specialization of std::hash can be injected in namespace std
namespace std {
template <>
struct hash<litebus::AID> {
    using ArgumentType = litebus::AID;
    using ResultType = std::size_t;
    ResultType operator()(ArgumentType const &s) const noexcept
    {
        return (std::hash<std::string>{}(s.HashString()));
    }
};
}    // namespace std

#endif
