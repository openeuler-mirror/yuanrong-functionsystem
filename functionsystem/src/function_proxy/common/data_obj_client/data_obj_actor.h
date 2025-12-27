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

#ifndef FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_ACTOR_H
#define FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_ACTOR_H

#include "async/async.hpp"
#include "function_proxy/common/distribute_cache_client/distributed_cache_client.h"
#include "common/utils/actor_driver.h"
#include "litebus.h"

namespace functionsystem {

class DataObjActor : public BasisActor {
public:
    explicit DataObjActor(const std::shared_ptr<DistributedCacheClient> &distributedCacheClient)
        : BasisActor("data_obj_actor"), distributedCacheClient_(distributedCacheClient){};

    ~DataObjActor() override = default;
    
    void Init() override
    {
    }

    std::vector<std::string> GetObjNodeList(const std::string &tenantId, const std::vector<std::string> &objIDs);

    void InitDistributedCacheClient();

private:
    std::shared_ptr<DistributedCacheClient> distributedCacheClient_;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_ACTOR_H
