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
#include "domain_scheduler/include/domain_scheduler_launcher.h"

#include "domain_scheduler/startup/domain_scheduler_driver.h"

namespace functionsystem::domain_scheduler {

DomainSchedulerLauncher::DomainSchedulerLauncher(const DomainSchedulerParam &param)
{
    moduleDriver_ = std::make_shared<DomainSchedulerDriver>(param);
}

DomainSchedulerLauncher::DomainSchedulerLauncher(const std::shared_ptr<ModuleDriver> &moduleDriver)
{
    moduleDriver_ = moduleDriver;
}

DomainSchedulerLauncher::~DomainSchedulerLauncher()
{
}

Status DomainSchedulerLauncher::Start()
{
    return moduleDriver_->Start();
}

Status DomainSchedulerLauncher::Stop()
{
    return moduleDriver_->Stop();
}

void DomainSchedulerLauncher::Await()
{
    moduleDriver_->Await();
}
}  // namespace functionsystem::domain_scheduler
