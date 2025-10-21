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

#include "bundle_mgr.h"

#include "bundle_mgr_actor.h"

namespace functionsystem::local_scheduler {

void BundleMgr::OnHealthyStatus(const functionsystem::Status &status)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &BundleMgrActor::OnHealthyStatus, status);
}

litebus::Future<Status> BundleMgr::SyncBundles(const std::string &agentID)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &BundleMgrActor::SyncBundles, agentID);
}

litebus::Future<Status> BundleMgr::SyncFailedBundles(
    const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &BundleMgrActor::SyncFailedBundles, agentMap);
}

litebus::Future<Status> BundleMgr::NotifyFailedAgent(const std::string &failedAgentID)
{
    ASSERT_IF_NULL(actor_);
    return litebus::Async(actor_->GetAID(), &BundleMgrActor::NotifyFailedAgent, failedAgentID);
}

void BundleMgr::UpdateBundlesStatus(const std::string &agentID, const resource_view::UnitStatus &status)
{
    ASSERT_IF_NULL(actor_);
    litebus::Async(actor_->GetAID(), &BundleMgrActor::UpdateBundlesStatus, agentID, status);
}
}  // namespace functionsystem::local_scheduler
