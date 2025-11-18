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

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_H

#include <actor/actor.hpp>
#include <async/async.hpp>
#include <exec/exec.hpp>

#include "debug_server_mgr_actor.h"
#include "runtime_manager/config/flags.h"

namespace functionsystem::runtime_manager {

class DebugServerMgr {
public:
    explicit DebugServerMgr(const std::shared_ptr<DebugServerMgrActor> &actor);

    ~DebugServerMgr();

    litebus::Future<Status> CreateServer(const std::string &runtimeID, const std::string &port,
                                         const std::string &instanceID, const std::string &language) const;

    void AddRecord(const std::string &runtimeID, const pid_t &pid) const;

    litebus::Future<Status> DestroyServer(const std::string &runtimeID) const;

    litebus::Future<Status> DestroyAllServers() const;

    litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstanceInfos(
        const std::string &requestID) const;

    /**
     * Set flags to DebugServerMgrActor
     * @param flags
     * @return
     */
    void SetConfig(const Flags &flags) const;

    void SetEnableDebug();

    bool IsEnableDebug();

    litebus::AID GetAID() const;

private:
    std::shared_ptr<DebugServerMgrActor> actor_;
    bool enableDebug_{ false }; // cached
};
}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_DEBUG_DEBUG_SERVER_H
