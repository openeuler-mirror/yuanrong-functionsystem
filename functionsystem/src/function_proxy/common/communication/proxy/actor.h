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
#ifndef PROXY_ACTOR_H
#define PROXY_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <async/option.hpp>
#include <litebus.hpp>
#include <memory>

#include "common/proto/pb/posix_pb.h"

namespace functionsystem::proxy {
class Actor : public litebus::ActorBase {
public:
    explicit Actor(const std::string &name) : ActorBase(name) {};
    ~Actor() override = default;

protected:
    // litebus virtual functions
    void Init() override
    {
    }
    void Finalize() override
    {
    }
};
}  // namespace functionsystem::proxy
#endif
