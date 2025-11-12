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

#include "migrate_controller_actor.h"
#include "metadata/constants.h"

void functionsystem::local_scheduler::MigrateControllerActor::Update(const std::string &instanceID,
                                                                     const resources::InstanceInfo &instanceInfo,
                                                                     bool isForceUpdate) {
    if (IsInstHibernate(instanceInfo))
    auto owner = instanceInfo.functionproxyid();
    auto state = instanceInfo.instancestatus().code();
    YRLOG_INFO("MigrateControllerActor Update instanceID:{} owner:{} isForceUpdate:{}", instanceID, owner,
               isForceUpdate);
}

void functionsystem::local_scheduler::MigrateControllerActor::Delete(const std::string &instanceID) {
}

void functionsystem::local_scheduler::MigrateControllerActor::InstUtilChangeCallback(const std::string &instanceID,
    const int utilization) {
}

void functionsystem::local_scheduler::MigrateControllerActor::CheckPointRespCallback(const std::string &instanceID,
    const std::shared_ptr<runtime::CheckpointResponse> &status) {
}

bool functionsystem::local_scheduler::MigrateControllerActor::IsInstHibernate(
    const resources::InstanceInfo &instanceInfo) {
    auto iter = instanceInfo.createoptions().find(ENABLE_SUSPEND_RESUME);
    if (iter != instanceInfo.createoptions().end()) {
        return iter->second == "true";
    }
    return false;
}
