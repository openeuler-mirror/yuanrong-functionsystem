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
#ifndef SCHEDULE_RECORDER_ACTOR_H
#define SCHEDULE_RECORDER_ACTOR_H
#include <unordered_map>

#include "async/future.hpp"
#include "litebus.hpp"
#include "common/status/status.h"
namespace functionsystem::schedule_decision {
struct ScheduleRecordInfo {
    Status latelyStatus;
    int32_t failedTimes;
};
class ScheduleRecorderActor : public litebus::ActorBase {
public:
    explicit ScheduleRecorderActor(const std::string &name)
        : ActorBase(name)
    {
    }
    ~ScheduleRecorderActor() override = default;
    // status is ok means no err found, which may cause by schedule is not performed
    litebus::Future<Status> TryQueryScheduleErr(const std::string &requestID);
    void RecordScheduleErr(const std::string &requestID, const Status &status);
    void EraseScheduleErr(const std::string &requestID);
private:
    std::unordered_map<std::string, ScheduleRecordInfo> records_;
};

}  // namespace functionsystem::schedule_decision
#endif  // SCHEDULE_RECORDER_ACTOR_H
