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

#ifndef ACTOR_POLICY_H
#define ACTOR_POLICY_H
#include "actor/actorpolicyinterface.hpp"

namespace litebus {

class ShardedThread : public ActorPolicy {
public:
    ShardedThread(const std::shared_ptr<ActorBase> &actor);
    ~ShardedThread() override;

protected:
    void Terminate(const ActorBase *actor) override;
    int EnqueMessage(std::unique_ptr<MessageBase> &msg) override;
    std::list<std::unique_ptr<MessageBase>> *GetMsgs() override;
    void Notify() override;

private:
    bool ready;
    bool terminated;
    std::shared_ptr<ActorBase> actor;
};

class SingleThread : public ActorPolicy {
public:
    SingleThread();
    ~SingleThread() override;

protected:
    void Terminate(const ActorBase *actor) override;
    int EnqueMessage(std::unique_ptr<MessageBase> &msg) override;
    std::list<std::unique_ptr<MessageBase>> *GetMsgs() override;
    void Notify() override;

private:
    std::condition_variable conditionVar;
};

};    // end of namespace litebus
#endif
