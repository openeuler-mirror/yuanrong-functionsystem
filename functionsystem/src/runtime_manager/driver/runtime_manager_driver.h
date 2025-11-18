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

#ifndef RUNTIME_MANAGER_DRIVER_RUNTIME_MANAGER_DRIVER_H
#define RUNTIME_MANAGER_DRIVER_RUNTIME_MANAGER_DRIVER_H

#include "common/utils/module_driver.h"
#include "common/http/http_server.h"
#include "runtime_manager/config/flags.h"
#include "runtime_manager/manager/runtime_manager.h"

namespace functionsystem::runtime_manager {
class RuntimeManagerDriver : public ModuleDriver {
public:
    explicit RuntimeManagerDriver(const Flags &flags, const std::string &componentName);

    ~RuntimeManagerDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

private:
    Flags flags_;
    std::shared_ptr<RuntimeManager> actor_;
    std::shared_ptr<HttpServer> httpServer_;
    std::shared_ptr<DefaultHealthyRouter> apiRouteRegister_;
    std::string componentName_;
};
}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_DRIVER_RUNTIME_MANAGER_DRIVER_H
