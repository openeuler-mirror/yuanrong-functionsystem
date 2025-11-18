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
#ifndef COMMON_TYPES_INSTANCE_STATUS_H
#define COMMON_TYPES_INSTANCE_STATUS_H

#include <memory>
#include <string>
#include <unordered_set>

namespace functionsystem {

// Instance lifecycle status, some statuses are not used at present, reserved for future use
// SCHEDULE_FAILED while scheduling fails, the instance switches to this state. In this state, repeated requests can be
// received and rescheduling is triggered.
enum class InstanceState : int32_t {
    NEW,
    SCHEDULING,  // return Instance ID and the function do 'Invoke' on other proxy.
    CREATING,
    RUNNING,  // notify other proxy to forward 'Invoke' to the incident proxy.
    FAILED,
    EXITING,
    FATAL,
    SCHEDULE_FAILED,
    EXITED,
    EVICTING,
    EVICTED,
    SUB_HEALTH,
    SUSPEND
};

enum class GroupState : int32_t {
    SCHEDULING,
    RUNNING,
    FAILED,
    SUSPEND,
};

const std::unordered_map<GroupState, std::string> GROUP_STATES = {
    {GroupState::SCHEDULING, "SCHEDULING"},
    {GroupState::RUNNING, "RUNNING"},
    {GroupState::FAILED, "FAILED"},
    {GroupState::SUSPEND, "SUSPEND"},
};

inline std::string ToString(GroupState state)
{
    return GROUP_STATES.find(state) != GROUP_STATES.end() ? GROUP_STATES.at(state) : "Unknown";
}

const std::unordered_set<InstanceState> NO_UPDATE_ROUTE_STATE{ InstanceState::CREATING };
const std::unordered_set<InstanceState> NO_UPDATE_ROUTE_STATE_WITH_META_STORE{ InstanceState::SCHEDULING,
                                                                               InstanceState::CREATING };
const std::unordered_set<InstanceState> NEED_PERSISTENCE_STATE = {
    InstanceState::NEW,
    InstanceState::FAILED,
    InstanceState::SCHEDULE_FAILED,
};

const std::unordered_set<InstanceState> TERMINAL_INSTANCE_STATES = {
    InstanceState::EXITING,
    InstanceState::EXITED,
    InstanceState::EVICTING,
    InstanceState::EVICTED,
    InstanceState::FATAL,
};

[[maybe_unused]] static inline bool NeedUpdateRouteState(const InstanceState &state, bool isMetaStoreEnable = false)
{
    return isMetaStoreEnable
               ? NO_UPDATE_ROUTE_STATE_WITH_META_STORE.find(state) == NO_UPDATE_ROUTE_STATE_WITH_META_STORE.end()
               : NO_UPDATE_ROUTE_STATE.find(state) == NO_UPDATE_ROUTE_STATE.end();
}

[[maybe_unused]] static inline bool NeedPersistenceState(const InstanceState &state)
{
    return NEED_PERSISTENCE_STATE.find(state) != NEED_PERSISTENCE_STATE.end();
}

[[maybe_unused]] static inline bool IsNonRecoverableStatus(const int32_t &code)
{
    // InstanceState::FAILED will only appear when runtime_recover_enable is ture
    // Instances in InstanceState::FAILED status will be recovered by function_proxy
    return code == static_cast<int32_t>(InstanceState::FATAL) ||
           code == static_cast<int32_t>(InstanceState::SCHEDULE_FAILED) ||
           code == static_cast<int32_t>(InstanceState::EVICTED);
}

[[maybe_unused]] static inline bool IsWaitingStatus(const int32_t &code)
{
    return code == static_cast<int32_t>(InstanceState::SCHEDULING) ||
           code == static_cast<int32_t>(InstanceState::CREATING) ||
           code == static_cast<int32_t>(InstanceState::EXITING) ||
           code == static_cast<int32_t>(InstanceState::EVICTING);
}

[[maybe_unused]] static inline bool IsTerminalStatus(const InstanceState &state)
{
    return TERMINAL_INSTANCE_STATES.find(state) != TERMINAL_INSTANCE_STATES.end();
}

const std::string INSTANCE_MANAGER_OWNER = "InstanceManagerOwner";
const std::string GROUP_MANAGER_OWNER = "GroupManagerOwner";
}  // namespace functionsystem

#endif  // COMMON_TYPES_INSTANCE_STATUS_H