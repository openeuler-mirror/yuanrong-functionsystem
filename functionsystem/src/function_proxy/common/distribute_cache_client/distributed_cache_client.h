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

#ifndef FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DISTRIBUTED_CACHE_CLIENT_H
#define FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DISTRIBUTED_CACHE_CLIENT_H

#include <string>
#include <vector>

#include "common/status/status.h"

namespace functionsystem {

struct ObjMetaInfo {
    std::vector<std::string> locations;
    uint64_t objSize;
};

class DistributedCacheClient {
public:
    virtual ~DistributedCacheClient() = default;
    virtual Status Init() = 0;
    virtual Status Set(const std::string &key, const std::string &val) = 0;
    virtual Status Get(const std::string &key, std::string &val) = 0;
    virtual Status Get(const std::vector<std::string> &keys, std::vector<std::string> &vals) = 0;
    virtual Status Del(const std::string &key) = 0;
    virtual Status Del(const std::vector<std::string> &keys, std::vector<std::string> &failedKeys) = 0;

    virtual Status GetHealthStatus() = 0;

    virtual Status ShutDown() = 0;
};

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_DISTRIBUTED_CACHE_CLIENT_DISTRIBUTED_CACHE_CLIENT_H
