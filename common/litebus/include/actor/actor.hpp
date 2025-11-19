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

#ifndef __ACTOR_HPP__
#define __ACTOR_HPP__

#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "actor/buslog.hpp"
#include "actor/msg.hpp"
#include "ssl/sensitive_value.hpp"

namespace litebus {

class ActorBase;
class ActorMgr;
class ActorPolicy;

using ActorReference = std::shared_ptr<ActorBase>;

// should be at least greater than 1
constexpr uint32_t MAX_ACTOR_RECORD_SIZE = 3;

class ActorBase {
public:
    inline const AID &GetAID() const
    {
        return id;
    }

    inline void AddMsgRecord(const std::string &msgName)
    {
        recordNextPoint++;
        uint32_t startPoint = recordNextPoint % MAX_ACTOR_RECORD_SIZE;
        msgRecords[startPoint] = msgName;
    }

    inline void PrintMsgRecord()
    {
        uint32_t startPoint = recordNextPoint % MAX_ACTOR_RECORD_SIZE;
        for (uint32_t i = 0; i < MAX_ACTOR_RECORD_SIZE; i++) {
            BUSLOG_INFO("Actor message dump, actor:{},msg:{}", id.Name(), msgRecords[startPoint]);
            startPoint = (startPoint + MAX_ACTOR_RECORD_SIZE - 1) % MAX_ACTOR_RECORD_SIZE;
        }
    }

    ActorBase(const std::string &name);
    virtual ~ActorBase();

    // send  MessageBase message to the  actor.
    int Send(const AID &to, std::unique_ptr<MessageBase> msg);

    // send string message to the actor
    int Send(const AID &to, std::string &&name, std::string &&msg, bool remoteLink = false,
             bool isExactNotRemote = false);

    // get output buffer size for flow control
    uint64_t GetOutBufSize(const AID &to) const;

    // get input buffer size for flow control
    uint64_t GetInBufSize(const AID &to) const;

    // set record send/receive message package size
    int AddRuleUdp(const std::string &peer, int recordNum) const;

    // delete the send/recieve message package size
    void DelRuleUdp(const std::string &peer, bool outputLog) const;

protected:
    using ActorFunction = std::function<void(std::unique_ptr<MessageBase> &msg)>;

    // install KMSG handler . This method will be called before the actor start to run.
    virtual void Init()
    {
    }

    // This method will be called before the actor start to terminate.
    virtual void Finalize()
    {
    }

    // KHTTPMsg handler
    virtual void HandleHttp(std::unique_ptr<MessageBase>)
    {
        BUSLOG_ERROR("ACTOR HandleHttp() is not implemented, a={}", id.Name());
    }

    // KLOCALMsg handler
    virtual void HandleLocalMsg(std::unique_ptr<MessageBase>)
    {
        BUSLOG_ERROR("ACTOR HandleLocalMsg() is not implemented. a={}", id.Name());
    }

    // The link is closed.
    virtual void Exited(const AID &)
    {
        BUSLOG_ERROR("ACTOR Exited() is not implemented. a={}", id.Name());
    }

    // Filter the KMSG
    virtual bool Filter(const std::unique_ptr<MessageBase> &)
    {
        return false;
    }

    // register the message handle
    void Receive(const std::string &msgName, ActorFunction &&func);

    // register the message handle
    virtual void Signature(const std::unique_ptr<MessageBase> &msg);

    virtual bool SignatureVerification(const std::unique_ptr<MessageBase> &msg);

    // register the message handle. It will be discarded.
    template <typename T>
    void Receive(const std::string &msgName, void (T::*method)(litebus::AID, std::string &&, std::string &&))
    {
        ActorFunction func = std::bind(&BehaviorBase1<T>, static_cast<T *>(this), method, std::placeholders::_1);
        Receive(msgName, std::move(func));
    }

    // register the message handle
    template <typename T>
    void Receive(const std::string &msgName, void (T::*method)(const litebus::AID &, std::string &&, std::string &&))
    {
        ActorFunction func = std::bind(&BehaviorBase<T>, static_cast<T *>(this), method, std::placeholders::_1);
        Receive(msgName, std::move(func));
        return;
    }

    // register the message handle, for kmsg-udp message
    template <typename T>
    void ReceiveUdp(const std::string &msgName, void (T::*method)(const litebus::AID &, std::string &&, std::string &&))
    {
        ActorFunction func = std::bind(&BehaviorBaseForUdp<T>, static_cast<T *>(this), method, std::placeholders::_1);
        Receive(msgName, std::move(func));
        return;
    }

    // Link the remote actor
    int Link(const AID &to) const;

    // Unlink the remote actor
    int UnLink(const AID &to) const;

    // Reconnect to the remote actor
    int Reconnect(const AID &to) const;

    void Terminate();
    void Await();

private:
    friend class ActorMgr;
    friend class ActorThread;

    // KMSG Msg Handler
    virtual void HandlekMsg(std::unique_ptr<MessageBase> &msg);

    template <typename T>
    static void BehaviorBase(T *t, void (T::*method)(const litebus::AID &, std::string &&, std::string &&),
                             std::unique_ptr<MessageBase> &msg)
    {
        if (msg->type != MessageBase::Type::KMSG) {
            BUSLOG_ERROR("Drop non-tcp message, from:{},to:{},name:{}", std::string(msg->from), std::string(msg->to),
                         msg->name);
            return;
        }
        (t->*method)(msg->from, std::move(msg->name), std::move(msg->body));
    }

    // regist the message handle. It will be discarded.
    template <typename T>
    static void BehaviorBase1(T *t, void (T::*method)(litebus::AID, std::string &&, std::string &&),
                              std::unique_ptr<MessageBase> &msg)
    {
        if (msg->type != MessageBase::Type::KMSG) {
            BUSLOG_ERROR("Drop non-tcp message. from:{},to:{},name:{}", std::string(msg->from), std::string(msg->to),
                         msg->name);
            return;
        }
        (t->*method)(msg->from, std::move(msg->name), std::move(msg->body));
    }

    // regist the udp message handle. Use this closure function to drop non-udp messages
    template <typename T>
    static void BehaviorBaseForUdp(T *t, void (T::*method)(const litebus::AID &, std::string &&, std::string &&),
                                   std::unique_ptr<MessageBase> &msg)
    {
        if (msg->type != MessageBase::Type::KUDP) {
            BUSLOG_ERROR("Drop non-udp message. from:{},to:{},name:{}", std::string(msg->from), std::string(msg->to),
                         msg->name);
            return;
        }
        (t->*method)(msg->from, std::move(msg->name), std::move(msg->body));
    }

    void Run();
    void Quit();
    int EnqueMessage(std::unique_ptr<MessageBase> msg);

    void Spawn(std::shared_ptr<ActorBase> &actor, std::unique_ptr<ActorPolicy> actorThread);
    void SetRunningStatus(bool start);

    std::string GenSignature(const std::unique_ptr<MessageBase> &msg, const std::string &timestamp) const;

    std::unique_ptr<ActorPolicy> actorThread;

    AID id;

    std::shared_ptr<std::string> accessKey_;
    std::shared_ptr<SensitiveValue> secretKey_;

    std::map<std::string, ActorFunction> actionFunctions;
    std::mutex waiterLock;

    std::string msgRecords[MAX_ACTOR_RECORD_SIZE];
    uint32_t recordNextPoint = 0;
};

}  // namespace litebus

#endif
