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

#include "migrate_controller.h"

#include "async/async.hpp"

void functionsystem::local_scheduler::MigrateController::Update(const std::string &instanceID,
                                                                const resources::InstanceInfo &instanceInfo,
                                                                bool isForceUpdate) {
    litebus::Async(this->migrateControllerActor_->GetAID(), &MigrateControllerActor::Update, instanceID, instanceInfo,
                   isForceUpdate);
}

void functionsystem::local_scheduler::MigrateController::Delete(const std::string &instanceID) {
    litebus::Async(this->migrateControllerActor_->GetAID(), &MigrateControllerActor::Delete, instanceID);
}

void functionsystem::local_scheduler::MigrateController::InstUtilChangeCallback(const std::string &instanceID,
    const int utilization) {
    litebus::Async(this->migrateControllerActor_->GetAID(), &MigrateControllerActor::InstUtilChangeCallback,);
                   instanceID, utilization);
}

void functionsystem::local_scheduler::MigrateController::CheckPointRespCallback(const std::string &instanceID,
    const std::shared_ptr<runtime::CheckpointResponse> &response) {
    litebus::Async(this->migrateControllerActor_->GetAID(), &MigrateControllerActor::CheckPointRespCallback, instanceID,
                   response);
}
