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

#ifndef FUNCTION_MASTER_META_STORE_KV_SERVICE_ACCESSOR_ACTOR_H
#define FUNCTION_MASTER_META_STORE_KV_SERVICE_ACCESSOR_ACTOR_H

#include <unordered_set>

#include "actor/actor.hpp"
#include "async/future.hpp"

namespace functionsystem::meta_store {
class KvServiceAccessorActor : public litebus::ActorBase {
public:
    explicit KvServiceAccessorActor(const litebus::AID &kvServiceActor);

    KvServiceAccessorActor(const litebus::AID &kvServiceActor, const std::string &namePrefix);

    ~KvServiceAccessorActor() override = default;

protected:
    void Init() override;
    void Finalize() override;

private:
    void AsyncPut(const litebus::AID &from, std::string &&, std::string &&msg);
    void AsyncDelete(const litebus::AID &from, std::string &&, std::string &&msg);
    void AsyncGet(const litebus::AID &from, std::string &&, std::string &&msg);
    void AsyncTxn(const litebus::AID &from, std::string &&, std::string &&msg);
    void AsyncWatch(const litebus::AID &from, std::string &&, std::string &&msg);
    void AsyncGetAndWatch(const litebus::AID &from, std::string &&, std::string &&msg);

private:
    bool InsertRequestSet(const std::string &id);
    void RemoveRequestSet(const std::string &id);

private:
    litebus::AID kvServiceActor_;
    std::unordered_set<std::string> requestSet_;
    litebus::Future<bool> isRecoverReady_;
};
}  // namespace functionsystem::meta_store

#endif  // FUNCTION_MASTER_META_STORE_KV_SERVICE_ACCESSOR_ACTOR_H
