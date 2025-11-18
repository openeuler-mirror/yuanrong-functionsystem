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

#ifndef COMMON_MODULE_DRIVER_H
#define COMMON_MODULE_DRIVER_H

#include "common/status/status.h"

namespace functionsystem {
class ModuleDriver {
public:
    ModuleDriver() = default;
    virtual ~ModuleDriver() = default;

    virtual Status Start() = 0;
    virtual Status Sync()
    {
        return Status::OK();
    }
    virtual Status Recover()
    {
        return Status::OK();
    }
    // ToReady called after module Synced && Recovered
    virtual void ToReady()
    {
    }
    virtual Status Stop() = 0;
    virtual void Await() = 0;
};

inline Status StartModule(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            return Status(FAILED, "driver is nullptr.");
        }
        if (auto status = driver->Start(); status.IsError()) {
            return status;
        }
    }
    return Status::OK();
}

inline Status SyncModule(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            return Status(FAILED, "driver is nullptr.");
        }
        if (auto status = driver->Sync(); status.IsError()) {
            return status;
        }
    }
    return Status::OK();
}

inline Status RecoverModule(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            return Status(FAILED, "driver is nullptr.");
        }
        if (auto status = driver->Recover(); status.IsError()) {
            return status;
        }
    }
    return Status::OK();
}

inline void ModuleIsReady(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            continue;
        }
        driver->ToReady();
    }
}

inline Status StopModule(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            continue;
        }
        if (auto status = driver->Stop(); status.IsError()) {
            return status;
        }
    }
    return Status::OK();
}

inline void AwaitModule(const std::vector<std::shared_ptr<ModuleDriver>> &drivers)
{
    for (auto driver : drivers) {
        if (driver == nullptr) {
            continue;
        }
        driver->Await();
    }
}
}  // namespace functionsystem
#endif  // COMMON_MODULE_DRIVER_H
