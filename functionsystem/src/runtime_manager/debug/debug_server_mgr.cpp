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

#include "debug_server_mgr.h"

#include <memory>

namespace functionsystem::runtime_manager {
DebugServerMgr::DebugServerMgr(const std::shared_ptr<DebugServerMgrActor> &actor) : actor_(actor)
{
    litebus::Spawn(actor_);
}
litebus::AID DebugServerMgr::GetAID() const
{
    return actor_->GetAID();
}
DebugServerMgr::~DebugServerMgr()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Await(actor_->GetAID());
}
bool DebugServerMgr::IsEnableDebug()
{
    return enableDebug_;
}
void DebugServerMgr::SetEnableDebug()
{
    enableDebug_ = true;
}
void DebugServerMgr::AddRecord(const std::string &runtimeID, const pid_t &pid) const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::AddRecord, runtimeID, pid);
}
void DebugServerMgr::SetConfig(const Flags &flags) const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::SetConfig, flags);
}
litebus::Future<Status> DebugServerMgr::CreateServer(const std::string &runtimeID, const std::string &port,
                                                     const std::string &instanceID, const std::string &language) const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::CreateServer, runtimeID, port, instanceID, language);
}
litebus::Future<Status> DebugServerMgr::DestroyServer(const std::string &runtimeID) const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::DestroyServer, runtimeID);
}
litebus::Future<Status> DebugServerMgr::DestroyAllServers() const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::DestroyAllServers);
}
litebus::Future<messages::QueryDebugInstanceInfosResponse> DebugServerMgr::QueryDebugInstanceInfos(
    const std::string &requestID) const
{
    return litebus::Async(actor_->GetAID(), &DebugServerMgrActor::QueryDebugInstanceInfos, requestID);
}
}  // namespace functionsystem::runtime_manager
