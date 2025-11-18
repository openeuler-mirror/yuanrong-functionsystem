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

#include "domain_activator.h"

#include <async/uuid_generator.hpp>

#include "common/logs/logging.h"

namespace functionsystem::global_scheduler {

DomainActivator::DomainActivator(std::shared_ptr<domain_scheduler::DomainSchedulerLauncher> launcher)
    : launcher_(std::move(launcher))
{
}

DomainActivator::~DomainActivator()
{
}

Status DomainActivator::StartDomainSched()
{
    YRLOG_INFO("domain activator start to create domain scheduler");
    if (launcher_ == nullptr) {
        YRLOG_ERROR("failed to create domain scheduler, launcher is null");
        return Status(StatusCode::FAILED);
    }
    return launcher_->Start();
}

Status DomainActivator::StopDomainSched() noexcept
{
    if (launcher_ == nullptr) {
        return Status(StatusCode::FAILED);
    }
    Status status = launcher_->Stop();
    launcher_->Await();
    return status;
}
}  // namespace functionsystem::global_scheduler