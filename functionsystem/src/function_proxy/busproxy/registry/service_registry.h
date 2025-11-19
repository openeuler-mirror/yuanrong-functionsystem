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

#ifndef BUSPROXY_SERVICE_REGISTRY_H
#define BUSPROXY_SERVICE_REGISTRY_H

#include "common/metadata/metadata.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "busproxy/registry/constants.h"
#include "function_proxy/common/observer/observer_actor.h"

namespace functionsystem {

using namespace function_proxy;

class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;

    void Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info);
    void Init(std::shared_ptr<MetaStorageAccessor> accessor, const RegisterInfo &info, int ttl);
    Status Register();
    litebus::Future<Status> Stop();

private:
    RegisterInfo registerInfo_;
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    int ttl_ = DEFAULT_TTL;
};
} // namespace functionsystem

#endif // BUSPROXY_SERVICE_REGISTRY_H
