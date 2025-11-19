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

#include "log_manager.h"

#include "async/async.hpp"

namespace functionsystem::runtime_manager {

LogManager::LogManager(const std::shared_ptr<LogManagerActor> &actor) : actor_(actor)
{
    litebus::Spawn(actor_);
}

litebus::AID LogManager::GetAID() const
{
    return actor_->GetAID();
}

LogManager::~LogManager()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}

void LogManager::SetConfig(const Flags &flags) const
{
    litebus::Async(actor_->GetAID(), &LogManagerActor::SetConfig, flags);
}

void LogManager::StartScanLogs() const
{
    litebus::Async(actor_->GetAID(), &LogManagerActor::ScanLogsRegularly);
}

void LogManager::StopScanLogs() const
{
    litebus::Async(actor_->GetAID(), &LogManagerActor::StopScanLogs);
}

litebus::Future<std::string> LogManager::AcquireLogPrefix(const std::string &runtimeID)
{
    return litebus::Async(actor_->GetAID(), &LogManagerActor::AcquireLogPrefix, runtimeID);
}

void LogManager::ReleaseLogPrefix(const std::string &runtimeID)
{
    litebus::Async(actor_->GetAID(), &LogManagerActor::ReleaseLogPrefix, runtimeID);
}

}  // namespace functionsystem::runtime_manager