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
#include "async/async.hpp"
#include "schedule_recorder.h"

#include "schedule_recorder_actor.h"
namespace functionsystem::schedule_decision {

std::shared_ptr<ScheduleRecorder> ScheduleRecorder::CreateScheduleRecorder()
{
    auto actor = std::make_shared<ScheduleRecorderActor>("ScheduleRecorderActor-"
                                                         + litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    litebus::Spawn(actor);
    return std::make_shared<ScheduleRecorder>(actor);
}

litebus::Future<Status> ScheduleRecorder::TryQueryScheduleErr(const std::string &requestID)
{
    ASSERT_IF_NULL(recorder_);
    return litebus::Async(recorder_->GetAID(), &ScheduleRecorderActor::TryQueryScheduleErr, requestID);
}

void ScheduleRecorder::RecordScheduleErr(const std::string &requestID, const Status &status)
{
    ASSERT_IF_NULL(recorder_);
    return litebus::Async(recorder_->GetAID(), &ScheduleRecorderActor::RecordScheduleErr, requestID, status);
}

void ScheduleRecorder::EraseScheduleErr(const std::string &requestID)
{
    ASSERT_IF_NULL(recorder_);
    return litebus::Async(recorder_->GetAID(), &ScheduleRecorderActor::EraseScheduleErr, requestID);
}

}  // namespace functionsystem::schedule_decision