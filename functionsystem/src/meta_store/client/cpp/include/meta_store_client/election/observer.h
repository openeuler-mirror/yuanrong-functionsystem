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

#ifndef COMMON_META_STORE_ELECTION_CLIENT_OBSERVER_H
#define COMMON_META_STORE_ELECTION_CLIENT_OBSERVER_H

#include "meta_store_client/meta_store_struct.h"

namespace functionsystem {

class Observer {
public:
    Observer(std::string name, std::function<void(LeaderResponse)> callback, const std::string &etcdTablePrefix = "")
        : name_(std::move(name)), callback_(std::move(callback)), etcdTablePrefix_(etcdTablePrefix)
    {
    }

    virtual ~Observer() = default;

    virtual void Shutdown() noexcept = 0;

protected:
    std::string name_;
    std::function<void(LeaderResponse)> callback_;
    std::string etcdTablePrefix_;
};

}  // namespace functionsystem

#endif  // COMMON_META_STORE_ELECTION_CLIENT_OBSERVER_H
