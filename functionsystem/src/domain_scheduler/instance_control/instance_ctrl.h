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

#ifndef DOMAIN_INSTANCE_CTRL_PROXY_H
#define DOMAIN_INSTANCE_CTRL_PROXY_H

#include <async/future.hpp>
#include <litebus.hpp>
#include <queue>
#include "common/proto/pb/message_pb.h"

namespace functionsystem::domain_scheduler {
class InstanceCtrl {
public:
    explicit InstanceCtrl(const litebus::AID &aid);
    virtual ~InstanceCtrl() = default;

    virtual litebus::Future<std::shared_ptr<messages::ScheduleResponse>> Schedule(
        const std::shared_ptr<messages::ScheduleRequest> &req);
    virtual void UpdateMaxSchedRetryTimes(const uint32_t &retrys);
    virtual void SetDomainLevel(bool isHeader);
    virtual void SetScalerAddress(const std::string& address);
    virtual void TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest);
    virtual litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> GetSchedulerQueue();

private:
    litebus::AID aid_;
};
}  // namespace functionsystem::domain_scheduler

#endif  // DOMAIN_INSTANCE_CTRL_PROXY_H
