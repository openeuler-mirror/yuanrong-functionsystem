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

#ifndef LITEBUS_MESSAGE_HPP
#define LITEBUS_MESSAGE_HPP

#include "actor/aid.hpp"
#include "ssl/sensitive_value.hpp"

namespace litebus {
class ActorBase;
class MessageBase {
public:
    enum class Type : char {
        KMSG = 1,
        KUDP,
        KHTTP,
        KASYNC,
        KLOCAL,
        KEXIT,
        KTERMINATE,
    };

    MessageBase(Type eType = Type::KMSG) : from(), name(), type(eType)
    {
    }

    explicit MessageBase(const std::string &sName, Type eType = Type::KMSG) : from(), name(sName), type(eType)
    {
    }

    explicit MessageBase(const AID &aFrom, const AID &aTo, Type eType = Type::KMSG)
        : from(aFrom), to(aTo), name(), body(), type(eType)
    {
    }

    explicit MessageBase(const AID &aFrom, const AID &aTo, const std::string &sName, Type eType = Type::KMSG)
        : from(aFrom), to(aTo), name(sName), body(), type(eType)
    {
    }

    explicit MessageBase(const AID &aFrom, const AID &aTo, const std::string &sName, std::string &&sBody,
                         Type eType = Type::KMSG)
        : from(aFrom), to(aTo), name(sName), body(std::move(sBody)), type(eType)
    {
    }

    virtual ~MessageBase()
    {
    }

    inline std::string &Name()
    {
        return name;
    }

    inline void SetName(const std::string &aName)
    {
        this->name = aName;
    }

    inline AID &From()
    {
        return from;
    }

    inline std::string &Body()
    {
        return body;
    }

    inline void SetFrom(const AID &aFrom)
    {
        from = aFrom;
    }

    inline AID &To()
    {
        return to;
    }

    inline void SetTo(const AID &aTo)
    {
        to = aTo;
    }

    inline Type GetType() const
    {
        return type;
    }

    inline void SetType(Type eType)
    {
        type = eType;
    }

    virtual void Run(ActorBase *)
    {
    }

    friend class ActorBase;
    friend class TCPMgr;
    AID from;
    AID to;            // from user
    std::string name;  // from user
    std::string body;  // from user
    Type type;

    std::shared_ptr<std::string> accessKey;
    std::shared_ptr<SensitiveValue> secretKey;

    std::string peer;
    std::string timestamp;
    std::string signature{ "0" };    // to(url@name), name, body
};

}    // namespace litebus

#endif    // LITEBUS_MESSAGE_HPP
