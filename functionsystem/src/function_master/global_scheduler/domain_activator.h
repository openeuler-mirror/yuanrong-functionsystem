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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_DOMAIN_ACTIVATOR_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_DOMAIN_ACTIVATOR_H

#include "common/status/status.h"
#include "domain_scheduler/include/domain_scheduler_launcher.h"

namespace functionsystem::global_scheduler {

class DomainActivator {
public:
    explicit DomainActivator(std::shared_ptr<domain_scheduler::DomainSchedulerLauncher> launcher);

    virtual ~DomainActivator();

    virtual Status StartDomainSched();
    virtual Status StopDomainSched() noexcept;

private:
    std::shared_ptr<domain_scheduler::DomainSchedulerLauncher> launcher_ = nullptr;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_DOMAIN_ACTIVATOR_H
