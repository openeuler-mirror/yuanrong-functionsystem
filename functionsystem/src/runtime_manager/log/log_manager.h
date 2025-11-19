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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_H

#include "logmanager_actor.h"
#include "runtime_manager/config/flags.h"

#include <actor/actor.hpp>
#include <exec/exec.hpp>

namespace functionsystem::runtime_manager {

class LogManager {
public:
    explicit LogManager(const std::shared_ptr<LogManagerActor> &actor);

    ~LogManager();

    void StartScanLogs() const;

    void StopScanLogs() const;

    litebus::Future<std::string> AcquireLogPrefix(const std::string &runtimeID);

    void ReleaseLogPrefix(const std::string &runtimeID);

    /**
     * Set flags to LogManagerActor
     * @param flags
     * @return
     */
    void SetConfig(const Flags &flags) const;

    litebus::AID GetAID() const;

private:
    std::shared_ptr<LogManagerActor> actor_;
};
}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_LOG_LOG_MANAGER_H
