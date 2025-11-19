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

#ifndef COMMON_UTILS_ACTOR_WORKER_H
#define COMMON_UTILS_ACTOR_WORKER_H

#include "async/async.hpp"
#include "async/future.hpp"
#include "common/status/status.h"
#include "litebus.hpp"

namespace functionsystem {
class Worker : public litebus::ActorBase {
public:
    Worker() : litebus::ActorBase(litebus::uuid_generator::UUID::GetRandomUUID().ToString())
    {
    }
    ~Worker() = default;
    Status Work(const std::function<void()> &handler)
    {
        handler();
        return Status::OK();
    }
};

class ActorWorker {
public:
    ActorWorker()
    {
        worker_ = std::make_shared<Worker>();
        (void)litebus::Spawn(worker_);
    }

    ~ActorWorker()
    {
        if (worker_) {
            litebus::Terminate(worker_->GetAID());
            litebus::Await(worker_->GetAID());
        }
    }

    litebus::Future<Status> AsyncWork(std::function<void()> handler)
    {
        return litebus::Async(worker_->GetAID(), &Worker::Work, handler);
    }

    void Terminate()
    {
        litebus::Terminate(worker_->GetAID());
        worker_ = nullptr;
    }

private:
    std::shared_ptr<Worker> worker_;
};
}  // namespace functionsystem

#endif  // FUNCTIONSYSTEM_SRC_COMMON_UTILS_ACTOR_WORKER_H
