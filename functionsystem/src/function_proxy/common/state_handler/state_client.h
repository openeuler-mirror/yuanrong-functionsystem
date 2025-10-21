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

#ifndef BUSPROXY_BUSINESS_STATE_ACTOR_STATE_CLIENT_H
#define BUSPROXY_BUSINESS_STATE_ACTOR_STATE_CLIENT_H

#include <memory>

#include "function_proxy/common/distribute_cache_client/distributed_cache_client.h"

namespace functionsystem {

class StateClient {
public:
    explicit StateClient(const std::shared_ptr<DistributedCacheClient> &distributedCacheClient);
    virtual ~StateClient() = default;

    virtual Status Init();

    virtual Status Set(const std::string &instanceId, const std::string &state);

    virtual Status Get(const std::string &instanceId, std::string &state);

    virtual Status Del(const std::string &instanceId);

private:
    std::shared_ptr<DistributedCacheClient> distributedCacheClient_;
    bool isInited_;
};

}  // namespace functionsystem

#endif  // BUSPROXY_BUSINESS_STATE_ACTOR_STATE_CLIENT_H
