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

#ifndef VIRTUAL_ENV_CLEAN_ACTOR_H
#define VIRTUAL_ENV_CLEAN_ACTOR_H

#include "actor/actor.hpp"
#include "common/status/status.h"
#include "common/utils/cmd_tool.h"
#include "executor/executor.h"
#include "timer/timer.hpp"

namespace functionsystem::runtime_manager {

struct EnvInfo {
    std::unordered_set<std::string> runtimeIds;
    uint64_t lastRemoveTimestamp{ 0 };
};

constexpr uint32_t DEFAULT_RECYCLE_UNUSED_ENVS_INTERVAL = 5000;

class VirtualEnvMgrActor : public litebus::ActorBase {
public:
    explicit VirtualEnvMgrActor(const std::string &name, int virtualEnvIdleTimeLimit);

    void AddEnvReferInfos(const std::string &envName, const std::string &runtimeId);

    void RmEnvReferInfos(const std::string &runtimeId);

    void RecycleUnusedEnvs();

    // for test
    [[maybe_unused]] std::unordered_map<std::string, EnvInfo> GetEnvReferInfos()
    {
        return envReferInfos_;
    }

    // for test
    [[maybe_unused]] void SetRecycleUnusedEnvsInterval(uint32_t interval)
    {
        recycleUnusedEnvsInterval_ = interval;
    }

protected:
    void Init() override;

    void Finalize() override;

private:
    bool NeedToRecycleEnv(std::unordered_map<std::string, EnvInfo>::iterator envReferInfoIter);

    int64_t getCurrentTimestampMs();

    // key:环境名，value:EnvInfo
    std::unordered_map<std::string, EnvInfo> envReferInfos_;

    litebus::Timer recycleUnusedEnvsTimer_;

    uint32_t recycleUnusedEnvsInterval_{ DEFAULT_RECYCLE_UNUSED_ENVS_INTERVAL };

    int virtualEnvIdleTimeLimit_;

    std::shared_ptr<CmdTool> cmdTool_;
};

}  // namespace functionsystem::runtime_manager

#endif  // VIRTUAL_ENV_CLEAN_ACTOR_H
