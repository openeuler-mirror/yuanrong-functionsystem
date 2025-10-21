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

#ifndef FUNCTION_PROXY_COMMON_OBSERVER_TENANT_OBSERVER_H
#define FUNCTION_PROXY_COMMON_OBSERVER_TENANT_OBSERVER_H

#include "tenant_listener.h"

#include <memory>

namespace functionsystem {

class TenantObserver {
public:
    virtual ~TenantObserver()
    {
    }
    virtual void AttachTenantListener(const std::shared_ptr<TenantListener> &listener) = 0;
    virtual void DetachTenantListener(const std::shared_ptr<TenantListener> &listener) = 0;
    virtual void NotifyUpdateTenantInstance(const TenantEvent &event) = 0;
    virtual void NotifyDeleteTenantInstance(const TenantEvent &event) = 0;
}; // class TenantObserver

}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_OBSERVER_TENANT_OBSERVER_H
