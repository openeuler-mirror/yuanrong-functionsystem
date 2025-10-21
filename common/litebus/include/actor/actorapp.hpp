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

#ifndef __ACTOR_APP_HPP__
#define __ACTOR_APP_HPP__

#include "actor/actor.hpp"
#include "actor/buslog.hpp"

namespace litebus {

class MessageLocal : public MessageBase {
public:
    MessageLocal(const AID &from, const AID &to, const std::string &name, void *aPtr)
        : MessageBase(from, to, name, "LocalMsg", Type::KLOCAL), ptr(aPtr)
    {
    }
    ~MessageLocal() override = default;

    void *ptr;
};

class AppActor : public ActorBase {
public:
    using APPBehavior = std::function<void(std::unique_ptr<MessageBase>)>;

    AppActor(const std::string &name) : ActorBase(name)
    {
    }
    ~AppActor() override = default;

    using ActorBase::Send;

    inline int Send(const AID &to, std::unique_ptr<MessageBase> msg)
    {
        return ActorBase::Send(to, std::move(msg));
    }
    // send T message to the actor
    template <typename M>
    int Send(const std::string &to, const std::string &msgName, std::unique_ptr<M> msg)
    {
        std::unique_ptr<MessageLocal> localMsg(new (std::nothrow) MessageLocal(GetAID(), to, msgName, msg.release()));
        BUS_OOM_EXIT(localMsg);
        return Send(to, std::move(localMsg));
    }

    // regist the message handle
    template <typename T, typename M>
    void Receive(const std::string &msgName, void (T::*method)(const AID &, std::unique_ptr<M>))
    {
        APPBehavior behavior = std::bind(&BehaviorBase<T, M>, static_cast<T *>(this), method, std::placeholders::_1);

        if (appBehaviors.find(msgName) != appBehaviors.end()) {
            BUSLOG_ERROR("ACTOR msgName conflict, a={}, msg={}", GetAID().Name(), msgName);
            BUS_EXIT("msgName conflicts.");
            return;
        }

        (void)appBehaviors.emplace(msgName, behavior);
        return;
    }

    template <typename T, typename M>
    static void BehaviorBase(T *t, void (T::*method)(const AID &, std::unique_ptr<M>),
                             const std::unique_ptr<MessageBase> &msg)
    {
        auto local = dynamic_cast<MessageLocal *>(msg.get());
        if (local == nullptr) {
            return;
        }
        (t->*method)(msg->From(), std::move(std::unique_ptr<M>(static_cast<M *>(local->ptr))));
    }

protected:
    // KLOCALMsg handler
    void HandleLocalMsg(std::unique_ptr<MessageBase> msg) override
    {
        auto it = appBehaviors.find(msg->Name());
        if (it != appBehaviors.end()) {
            it->second(std::move(msg));
        } else {
            BUSLOG_ERROR("ACTOR can not finds handler. a={},msg={},hdlno={}", GetAID().Name(), msg->Name(),
                         appBehaviors.size());
        }
    }

private:
    std::map<std::string, APPBehavior> appBehaviors;
};

}    // namespace litebus

#endif
