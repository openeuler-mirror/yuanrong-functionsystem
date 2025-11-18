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

#include "virtual_env_mgr_actor.h"

#include "async/asyncafter.hpp"
#include "timer/timertools.hpp"

namespace functionsystem::runtime_manager {

VirtualEnvMgrActor::VirtualEnvMgrActor(const std::string &name, int virtualEnvIdleTimeLimit)
    : ActorBase(name), virtualEnvIdleTimeLimit_(virtualEnvIdleTimeLimit)
{
    envReferInfos_ = std::unordered_map<std::string, EnvInfo>();
    cmdTool_ = std::make_shared<CmdTool>();
}

void VirtualEnvMgrActor::Init()
{
    ActorBase::Init();
    litebus::AsyncAfter(recycleUnusedEnvsInterval_, GetAID(), &VirtualEnvMgrActor::RecycleUnusedEnvs);
}

void VirtualEnvMgrActor::Finalize()
{
    envReferInfos_.clear();
    (void)litebus::TimerTools::Cancel(recycleUnusedEnvsTimer_);
    ActorBase::Finalize();
}

void VirtualEnvMgrActor::AddEnvReferInfos(const std::string &envName, const std::string &runtimeId)
{
    YRLOG_INFO("env:{} add one runtime:{}", envName, runtimeId);
    if (auto iter = envReferInfos_.find(envName); iter == envReferInfos_.end()) {
        (void)envReferInfos_.emplace(envName, EnvInfo{ { runtimeId } });
    } else {
        (void)iter->second.runtimeIds.emplace(runtimeId);
    }
}

void VirtualEnvMgrActor::RmEnvReferInfos(const std::string &runtimeId)
{
    for (auto &pair : envReferInfos_) {
        if (!pair.second.runtimeIds.count(runtimeId)) {
            continue;
        }

        pair.second.runtimeIds.erase(runtimeId);
        YRLOG_INFO("runtimeId:{} is removed on envName:{}", runtimeId, pair.first);

        if (pair.second.runtimeIds.empty()) {
            pair.second.lastRemoveTimestamp = getCurrentTimestampMs();
        }

        break;
    }
}

void VirtualEnvMgrActor::RecycleUnusedEnvs()
{
    if (virtualEnvIdleTimeLimit_ == -1) {
        YRLOG_INFO("No need to recycle envs");
        return;
    }

    if (envReferInfos_.empty()) {
        YRLOG_INFO("There is no env to be recycled");
        recycleUnusedEnvsTimer_ =
            litebus::AsyncAfter(recycleUnusedEnvsInterval_, GetAID(), &VirtualEnvMgrActor::RecycleUnusedEnvs);
        return;
    }

    for (auto envReferInfoIter = envReferInfos_.begin(); envReferInfoIter != envReferInfos_.end();) {
        if (!NeedToRecycleEnv(envReferInfoIter)) {
            (void)++envReferInfoIter;
            continue;
        }

        auto envName = envReferInfoIter->first;

        ASSERT_IF_NULL(cmdTool_);
        std::ostringstream clearCondaEnvCoimmandOs;
        clearCondaEnvCoimmandOs << "conda env remove --name " << envName << " --yes";
        std::string clearCondaEnvCoimmand = clearCondaEnvCoimmandOs.str();
        YRLOG_DEBUG("clearCondaEnvCoimmand: {}", clearCondaEnvCoimmand);
        std::vector<std::string> clearEnvResult = cmdTool_->GetCmdResultWithError(clearCondaEnvCoimmand);
        bool isEnvRecycled = std::any_of(clearEnvResult.begin(), clearEnvResult.end(), [](const std::string &line) {
            return line.find("Remove all packages in environment") != std::string::npos;
        });
        if (isEnvRecycled) {
            YRLOG_INFO("env:{} is recycled", envName);
        }

        envReferInfoIter = envReferInfos_.erase(envReferInfoIter);
    }

    recycleUnusedEnvsTimer_ =
        litebus::AsyncAfter(recycleUnusedEnvsInterval_, GetAID(), &VirtualEnvMgrActor::RecycleUnusedEnvs);
}

bool VirtualEnvMgrActor::NeedToRecycleEnv(std::unordered_map<std::string, EnvInfo>::iterator envReferInfoIter)
{
    bool envIsIdle = envReferInfoIter->second.runtimeIds.empty();

    auto now = getCurrentTimestampMs();
    bool envIdleExceedLimit =
        now - envReferInfoIter->second.lastRemoveTimestamp >= static_cast<uint64_t>(virtualEnvIdleTimeLimit_);

    return envIsIdle && envIdleExceedLimit;
}

int64_t VirtualEnvMgrActor::getCurrentTimestampMs()
{
    auto now = std::chrono::system_clock::now();

    auto duration = now.time_since_epoch();

    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}
}  // namespace functionsystem::runtime_manager