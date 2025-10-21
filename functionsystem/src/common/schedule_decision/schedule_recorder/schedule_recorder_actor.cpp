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
#include "schedule_recorder_actor.h"

namespace functionsystem::schedule_decision {

litebus::Future<Status> ScheduleRecorderActor::TryQueryScheduleErr(const std::string &requestID)
{
    auto iter = records_.find(requestID);
    if (iter == records_.end()) {
        return Status::OK();
    }
    auto message = "which has been scheduled for " + std::to_string(iter->second.failedTimes) +
                   " times. The latest failure: " + iter->second.latelyStatus.RawMessage();
    auto code = iter->second.latelyStatus.StatusCode();
    (void)records_.erase(iter);
    return Status(code, message);
}

void ScheduleRecorderActor::RecordScheduleErr(const std::string &requestID, const Status &status)
{
    auto iter = records_.find(requestID);
    if (iter == records_.end()) {
        records_[requestID] = { status, 1 };
        return;
    }
    iter->second.failedTimes++;
    iter->second.latelyStatus = status;
}

void ScheduleRecorderActor::EraseScheduleErr(const std::string &requestID)
{
    auto iter = records_.find(requestID);
    if (iter == records_.end()) {
        return;
    }
    (void)records_.erase(iter);
}

}  // namespace functionsystem::schedule_decision