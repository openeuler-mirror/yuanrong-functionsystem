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

#ifndef DOMAIN_SCHED_SRV_PROXY_H
#define DOMAIN_SCHED_SRV_PROXY_H

#include <async/future.hpp>
#include <litebus.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::domain_scheduler {
class DomainSchedSrv {
public:
    explicit DomainSchedSrv(const litebus::AID &aid) : aid_(aid)
    {
    }
    virtual ~DomainSchedSrv() = default;

    /**
     * encapsulation of Async call NotifySchedAbnormal
     * Report the managed scheduler exception.
     * @param req: abnormal infomation
     * @return request result
     */
    virtual litebus::Future<Status> NotifySchedAbnormal(const messages::NotifySchedAbnormalRequest &req);

    virtual litebus::Future<Status> NotifyWorkerStatus(const messages::NotifyWorkerStatusRequest &req);

    /**
     * encapsulation of Async call ForwardSchedule
     * Submit an instance scheduling request by forward req to upLayer
     * @param req: schedule request body
     * @return request result
     */
    virtual litebus::Future<std::shared_ptr<messages::ScheduleResponse>> ForwardSchedule(
        const std::shared_ptr<messages::ScheduleRequest> &req);

    virtual litebus::Future<Status> EnableMetrics(const bool enableMetrics);

private:
    litebus::AID aid_;
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHED_SRV_PROXY_H
