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

#ifndef COMMON_ACTOR_DRIVER_H
#define COMMON_ACTOR_DRIVER_H
#include "async/async.hpp"
#include "common/status/status.h"
#include "common/logs/logging.h"
namespace functionsystem {
// Standardizes the actor startup process.
class BasisActor : public litebus::ActorBase {
public:
    explicit BasisActor(const std::string &name) : litebus::ActorBase(name)
    {
    }
    ~BasisActor() override = default;

    virtual litebus::Future<Status> Sync()
    {
        return Status::OK();
    }
    virtual litebus::Future<Status> Recover()
    {
        return Status::OK();
    }

    virtual void ToReady()
    {
        YRLOG_INFO("{} is ready", GetAID().Name());
        isReady_ = true;
    }

    virtual bool IsReady()
    {
        return isReady_;
    }

private:
    bool isReady_ {false};
};

class ActorDriver {
public:
    explicit ActorDriver(const std::shared_ptr<BasisActor> &actor) : actor_(actor)
    {
    }
    virtual ~ActorDriver()
    {
        Stop();
        Await();
    }

    virtual litebus::Future<Status> Sync()
    {
        if (actor_ == nullptr) {
            return Status::OK();
        }
        return litebus::Async(actor_->GetAID(), &BasisActor::Sync);
    }
    virtual litebus::Future<Status> Recover()
    {
        if (actor_ == nullptr) {
            return Status::OK();
        }
        return litebus::Async(actor_->GetAID(), &BasisActor::Recover);
    }

    virtual void ToReady()
    {
        if (actor_ == nullptr) {
            return;
        }
        litebus::Async(actor_->GetAID(), &BasisActor::ToReady);
    }

    virtual void Stop()
    {
        if (actor_ == nullptr) {
            return;
        }
        litebus::Terminate(actor_->GetAID());
    }

    virtual void Await()
    {
        if (actor_ == nullptr) {
            return;
        }
        litebus::Await(actor_->GetAID());
    }

    virtual std::string ActorName()
    {
        if (actor_ == nullptr) {
            return "";
        }
        return actor_->GetAID().Name();
    }
private:
    std::shared_ptr<BasisActor> actor_;
};

inline Status ActorSync(const std::vector<std::shared_ptr<ActorDriver>> &actorDrivers)
{
    for (auto actor : actorDrivers) {
        if (actor == nullptr) {
            return Status(StatusCode::FAILED, "nullptr of actor driver");
        }
        YRLOG_INFO("{} start to sync.", actor->ActorName());
        if (auto status = actor->Sync().Get(); status.IsError()) {
            YRLOG_ERROR("failed to sync {}. err: {}", actor->ActorName(), status.ToString());
            return status;
        }
    }
    return Status::OK();
}

inline Status ActorRecover(const std::vector<std::shared_ptr<ActorDriver>> &actorDrivers)
{
    for (auto actor : actorDrivers) {
        if (actor == nullptr) {
            return Status(StatusCode::FAILED, "nullptr of actor driver");
        }
        YRLOG_INFO("{} start to recover.", actor->ActorName());
        if (auto status = actor->Recover().Get(); status.IsError()) {
            YRLOG_ERROR("failed to recover {}. err: {}", actor->ActorName(), status.ToString());
            return status;
        }
    }
    return Status::OK();
}

inline void ActorReady(const std::vector<std::shared_ptr<ActorDriver>> &actorDrivers)
{
    for (auto actor : actorDrivers) {
        if (actor == nullptr) {
            continue;
        }
        actor->ToReady();
    }
}

inline void StopActor(const std::vector<std::shared_ptr<ActorDriver>> &actorDrivers)
{
    for (auto actor : actorDrivers) {
        if (actor == nullptr) {
            continue;
        }
        actor->Stop();
    }
}

inline void AwaitActor(const std::vector<std::shared_ptr<ActorDriver>> &actorDrivers)
{
    for (auto actor : actorDrivers) {
        if (actor == nullptr) {
            continue;
        }
        actor->Await();
    }
}
}  // namespace functionsystem
#endif  // COMMON_ACTOR_DRIVER_H
