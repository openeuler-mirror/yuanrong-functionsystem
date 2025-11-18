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

#ifndef FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_DRIVER_H
#define FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_DRIVER_H

#include "bootstrap_actor.h"
#include "common/file_monitor/file_monitor.h"
#include "common/utils/module_driver.h"

namespace functionsystem::system_function_loader {
struct SystemFunctionLoaderStartParam {
    std::shared_ptr<global_scheduler::GlobalSched> globalSched;
    uint32_t sysFuncRetryPeriod;
    std::string sysFuncCustomArgs;
    std::string masterAddress;
    std::shared_ptr<instance_manager::InstanceManager> instanceMgr;
    bool enableFrontendPool{ false };
};
const uint64_t KILL_SYS_FUNC_TIMEOUT = 1000000;  // ms
const std::string CONFIGFILE_WATCH_PATH = "/home/sn/function/config";

class BootstrapDriver : public ModuleDriver {
public:
    BootstrapDriver(const SystemFunctionLoaderStartParam &param, const std::shared_ptr<MetaStoreClient> &metaClient)
        : param_(param), metaClient_(metaClient)
    {
    }

    ~BootstrapDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

    // only for test
    void SetKillSysFuncTimeout(uint64_t timeout)
    {
        killSysFuncTimeout_ = timeout;
    }
private:
    Status Create();

    SystemFunctionLoaderStartParam param_;
    std::shared_ptr<MetaStoreClient> metaClient_;
    std::shared_ptr<BootstrapActor> bootstrapActor_ = nullptr;
    uint64_t killSysFuncTimeout_ = KILL_SYS_FUNC_TIMEOUT;
    std::shared_ptr<FileMonitor> fileMonitor_{ nullptr };
};  // class BootstrapDriver
}  // namespace functionsystem::system_function_loader

#endif  // FUNCTION_MASTER_SYSTEM_FUNCTION_LOADER_BOOTSTRAP_DRIVER_H
