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

#ifndef SCHEDULER_RECORDER_H
#define SCHEDULER_RECORDER_H

#include <vector>

#include "async/future.hpp"
#include "litebus.hpp"

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::schedule_decision {
struct ScheduleQueueRecord {
    std::shared_ptr<messages::ScheduleRequest> request;
    int64_t enqueueTimeMs{ 0 };
};

class ScheduleRecorder {
public:
    explicit ScheduleRecorder(const litebus::ActorReference &actor) : recorder_(actor) {}
    ~ScheduleRecorder()
    {
        if (recorder_ != nullptr) {
            litebus::Terminate(recorder_->GetAID());
            litebus::Await(recorder_->GetAID());
            recorder_ = nullptr;
        }
    }

    static std::shared_ptr<ScheduleRecorder> CreateScheduleRecorder();

    litebus::Future<Status> TryQueryScheduleErr(const std::string &requestID);
    void RecordScheduleErr(const std::string &requestID, const Status &status);
    void EraseScheduleErr(const std::string &requestID);
    void RecordScheduleRequest(const std::shared_ptr<messages::ScheduleRequest> &request);
    void EraseScheduleRequest(const std::string &requestID);
    litebus::Future<std::vector<ScheduleQueueRecord>> QueryScheduleQueue();

private:
    litebus::ActorReference recorder_;
};

}  // namespace functionsystem::schedule_decision
#endif  // SCHEDULER_RECORDER_H
