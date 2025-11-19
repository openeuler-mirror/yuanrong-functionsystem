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

#ifndef FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_CONTEXT_H
#define FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_CONTEXT_H

#include <cstdint>

#include "common/constants/constants.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/resource.pb.h"
#include "common/resource_view/resource_type.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/types/instance_state.h"

namespace functionsystem {
using ScheduleResult = schedule_decision::ScheduleResult;

class InstanceContext {
public:
    explicit InstanceContext(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq);

    virtual ~InstanceContext(){};

    const resources::InstanceInfo &GetInstanceInfo() const;
    InstanceState GetState() const;
    std::shared_ptr<messages::ScheduleRequest> GetScheduleRequestCopy() const;
    std::shared_ptr<messages::ScheduleRequest> GetScheduleRequest() const;
    void UpdateInstanceInfo(const resources::InstanceInfo &instanceInfo);
    void UpdateOwner(const std::string &owner);
    std::string GetOwner() const;
    void SetInstanceState(InstanceState state, int32_t errCode, int32_t exitCode, const std::string &msg,
                          const int32_t type = static_cast<int32_t>(EXIT_TYPE::NONE_EXIT));
    std::string GetRequestID() const;
    void UpdateScheduleReq(const std::shared_ptr<messages::ScheduleRequest> &scheduleReq)
    {
        scheduleRequest_ = scheduleReq;
    }
    virtual void SetScheduleTimes(const int32_t &scheduleTimes) const;
    virtual void SetDeployTimes(const int32_t &deployTimes) const;
    virtual int32_t GetScheduleTimes() const;
    virtual int32_t GetDeployTimes() const;

    void SetFunctionAgentIDAndHeteroConfig(const schedule_decision::ScheduleResult &result);

    void SetDataSystemHost(const std::string &ip);

    void SetRuntimeID(const std::string &runtimeID);

    void SetStartTime(const std::string &timeInfo);

    void SetRuntimeAddress(const std::string &address);

    void IncreaseScheduleRound();

    uint32_t GetScheduleRound();

    void SetCheckpointed(const bool flag);

    void SetVersion(const int64_t version);

    int64_t GetVersion() const;

    int64_t GetGracefulShutdownTime() const;

    void SetGracefulShutdownTime(const int64_t time);

    void SetTraceID(const std::string &traceID);

    void TagStop();
    bool IsStopped();

    void SetModRevision(const int64_t modRevision);
    int64_t GetModRevision();

    litebus::Future<std::string> GetCancelFuture();
    void SetCancel(const std::string &reason);

private:
    std::shared_ptr<messages::ScheduleRequest> scheduleRequest_;
    std::shared_ptr<litebus::Promise<std::string>> cancelTag_;
    int64_t modRevision_ { 0 };
};

struct InstanceExitStatus {
    std::string instanceID;
    int32_t exitCode;
    std::string statusMsg;  // description information used to describe this status or change to this status
    int32_t exitType;
    int32_t errCode;
};

struct KillContext {
    bool isLocal = true;
    bool instanceIsFailed = false;
    KillResponse killRsp;
    std::shared_ptr<InstanceContext> instanceContext;
    std::shared_ptr<KillRequest> killRequest;
    std::string storageType;
    std::string srcInstanceID;
};

inline std::shared_ptr<InstanceExitStatus> GenInstanceStatusInfo(
    const std::string &instanceID, int32_t exitCode, const std::string &statusMsg,
    const int32_t type = static_cast<int32_t>(EXIT_TYPE::NONE_EXIT))
{
    auto instanceStatusInfo = std::make_shared<InstanceExitStatus>();
    instanceStatusInfo->instanceID = instanceID;
    switch (static_cast<EXIT_TYPE>(type)) {
        case (EXIT_TYPE::NONE_EXIT):
        case (EXIT_TYPE::RETURN): {
            instanceStatusInfo->errCode = static_cast<int32_t>(common::ErrorCode::ERR_INSTANCE_EXITED);
            break;
        }
        default:
            instanceStatusInfo->errCode = static_cast<int32_t>(common::ErrorCode::ERR_USER_FUNCTION_EXCEPTION);
    }
    instanceStatusInfo->statusMsg = statusMsg;
    instanceStatusInfo->exitType = type;
    instanceStatusInfo->exitCode = exitCode;
    return instanceStatusInfo;
}

bool IsFatal(const int32_t &exitCode);
}  // namespace functionsystem

#endif  // FUNCTION_PROXY_COMMON_STATE_MACHINE_INSTANCE_CONTEXT_H
