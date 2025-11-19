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

#ifndef FUNCTIONCORE_CPP_META_STORE_OPERATOR_CACHE_H
#define FUNCTIONCORE_CPP_META_STORE_OPERATOR_CACHE_H

#include "common/metadata/metadata.h"

namespace functionsystem {
class MetaStoreOperateCacher {
public:
    void AddPutEvent(const std::string &prefixKey, const std::string &key, const std::string &description);
    void AddDeleteEvent(const std::string &prefixKey, const std::string &key);

    void ErasePutEvent(const std::string &prefixKey, const std::string &key);
    void EraseDeleteEvent(const std::string &prefixKey, const std::string &key);

    bool IsCacheClear(const std::string &prefixKey);

    // for test
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> GetPutEventMap()
    {
        return putEventMap_;
    };
    std::unordered_map<std::string, std::set<std::string>> GetDeleteEventMap()
    {
        return deleteEventMap_;
    };

private:
    // prefix: /yr/route, key: /yr/route/business/yrk/tenant/0/function/, value: {"instanceID":"0fceXXX"}
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> putEventMap_;
    // prefix: /yr/route, key: /yr/route/business/yrk/tenant/0/function/
    std::unordered_map<std::string, std::set<std::string>> deleteEventMap_;
    std::mutex mutex_;
};

}  // namespace functionsystem
#endif  // FUNCTIONCORE_CPP_META_STORE_OPERATOR_CACHE_H
