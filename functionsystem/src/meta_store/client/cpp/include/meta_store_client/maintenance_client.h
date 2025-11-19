/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#ifndef FUNCTIONSYSTEM_META_STORE_MAINTENANCE_CLIENT_H
#define FUNCTIONSYSTEM_META_STORE_MAINTENANCE_CLIENT_H

#include <async/future.hpp>
#include "meta_store_struct.h"
namespace functionsystem::meta_store {
class MaintenanceClient {
public:
   virtual ~MaintenanceClient() = default;
   virtual litebus::Future<StatusResponse> HealthCheck() = 0;
   virtual litebus::Future<bool> IsConnected() = 0;
   virtual void BindReconnectedCallBack(const std::function<void(const std::string &)> &callback) = 0;
protected:
    MaintenanceClient() = default;
};
} // namespace functionsystem::meta_store

#endif // FUNCTIONSYSTEM_META_STORE_MAINTENANCE_CLIENT_H
