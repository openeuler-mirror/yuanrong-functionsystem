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

#ifndef DOMAIN_UNDERLAYER_SCHED_MGR_PROXY_H
#define DOMAIN_UNDERLAYER_SCHED_MGR_PROXY_H

#include <async/future.hpp>
#include <litebus.hpp>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::domain_scheduler {
class UnderlayerSchedMgr {
public:
    explicit UnderlayerSchedMgr(const litebus::AID &aid) : aid_(aid) {}
    virtual ~UnderlayerSchedMgr() = default;

    virtual litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DispatchSchedule(
        const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req);
    virtual void UpdateUnderlayerTopo(const messages::ScheduleTopology &req);
    virtual litebus::Future<bool> IsRegistered(const std::string &name);
    virtual void SetDomainLevel(bool isHeader);

    virtual litebus::Future<std::shared_ptr<messages::ScheduleResponse>> Reserve(
        const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req);
    virtual litebus::Future<Status> UnReserve(const std::string &selectedName,
                                      const std::shared_ptr<messages::ScheduleRequest> &req);

    virtual litebus::Future<Status> Bind(
        const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req);
    virtual litebus::Future<Status> UnBind(const std::string &selectedName,
                                   const std::shared_ptr<messages::ScheduleRequest> &req);
    virtual void SetScalerAddress(const std::string &address);
private:
    litebus::AID aid_;
};
}


#endif // DOMAIN_UNDERLAYER_SCHED_MGR_PROXY_H
