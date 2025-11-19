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

#ifndef COMMON_META_STORE_CLIENT_UTILS_META_STORE_EXPLORER_H
#define COMMON_META_STORE_CLIENT_UTILS_META_STORE_EXPLORER_H

#include "httpd/http.hpp"

namespace functionsystem {

class MetaStoreExplorer {
public:
    explicit MetaStoreExplorer(const std::string &address) : address_(address)
    {
    }

    virtual ~MetaStoreExplorer() = default;

    virtual litebus::Future<std::string> Explore() = 0;

    virtual bool IsNeedExplore() = 0;

    virtual void UpdateAddress(const std::string &address) = 0;

protected:
    std::string address_;
};

class MetaStoreDefaultExplorer : public MetaStoreExplorer {
public:
    explicit MetaStoreDefaultExplorer(const std::string &address) : MetaStoreExplorer(address)
    {
    }

    litebus::Future<std::string> Explore() override;
    bool IsNeedExplore() override;
    void UpdateAddress(const std::string &address) override;
};
}  // namespace functionsystem

#endif  // COMMON_META_STORE_CLIENT_UTILS_META_STORE_EXPLORER_H
