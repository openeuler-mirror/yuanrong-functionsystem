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

#include "file_monitor.h"

#include <async/async.hpp>

#include "async/future.hpp"
#include "common/status/status.h"

namespace functionsystem {

litebus::Future<bool> FileMonitor::Start()
{
    std::string name = "FileMonitor_" + litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    actor_ = std::make_shared<FileMonitorActor>(name);
    litebus::Spawn(actor_);
    return litebus::Async(actor_->GetAID(), &FileMonitorActor::Start);
}

void FileMonitor::Stop()
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &FileMonitorActor::Stop);
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
    actor_ = nullptr;
}

litebus::Future<bool> FileMonitor::RemoveWatch(const std::string &path)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &FileMonitorActor::RemoveWatch, path);
}

litebus::Future<bool> FileMonitor::AddWatch(const std::string &path, const functionsystem::EventHandler handler)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &FileMonitorActor::AddWatch, path, handler);
}
}  // namespace functionsystem