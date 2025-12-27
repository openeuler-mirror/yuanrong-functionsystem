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

#ifndef FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_CLIENT_H
#define FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_CLIENT_H

#include "data_obj_actor.h"
#include "common/utils/actor_driver.h"

namespace functionsystem {

class DataObjClient : public ActorDriver {
public:
    explicit DataObjClient(const std::shared_ptr<DataObjActor> &dataObjActor)
        : ActorDriver(dataObjActor), dataObjActor_(dataObjActor)
    {
    }

    ~DataObjClient() override = default;

    virtual litebus::Future<std::vector<std::string>> GetObjNodeList(const std::string &tenantId,
                                                                     const std::vector<std::string> &objIDs);

    virtual void InitDistributedCacheClient();

private:
    std::shared_ptr<DataObjActor> dataObjActor_;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_DATA_OBJ_CLIENT_DATA_OBJ_CLIENT_H
