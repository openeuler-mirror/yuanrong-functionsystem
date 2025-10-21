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

#ifndef ACTOR_THREAD_H
#define ACTOR_THREAD_H

#include <condition_variable>
#include <list>
#include <thread>

#include "actor/actor.hpp"

namespace litebus {

class ActorThread {
public:
    ActorThread();
    ~ActorThread();
    void Finalize();
    void AddThread(int threadCount);
    void EnqueReadyActor(const std::shared_ptr<ActorBase> &actor);

private:
    void Run();
    void DequeReadyActor(std::shared_ptr<ActorBase> &actor);

    std::list<std::shared_ptr<ActorBase>> readyActors;
    std::mutex readyActorMutex;
    std::condition_variable conditionVar;

    std::list<std::unique_ptr<std::thread>> workers;
    std::string threadName;
};

};    // end of namespace litebus
#endif
