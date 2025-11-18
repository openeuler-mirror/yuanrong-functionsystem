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

#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_GENERATE_MESSAGE_H
#define FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_GENERATE_MESSAGE_H

#include "common/metadata/metadata.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem {

inline messages::ScheduleResponse GenScheduleResponse(int32_t code, const std::string &message,
                                                      const messages::ScheduleRequest &scheduleReq)
{
    messages::ScheduleResponse response;
    response.set_code(code);
    response.set_message(message);
    response.set_traceid(scheduleReq.traceid());
    response.set_requestid(scheduleReq.requestid());
    response.set_instanceid(scheduleReq.instance().instanceid());
    *response.mutable_updateresources() = scheduleReq.updateresources();
    response.mutable_contexts()->insert(scheduleReq.contexts().begin(), scheduleReq.contexts().end());
    return response;
}

inline messages::ScheduleResponse GenScheduleResponse(StatusCode code, const std::string &message,
                                                      const messages::ScheduleRequest &scheduleReq)
{
    return GenScheduleResponse(static_cast<int32_t>(code), message, scheduleReq);
}

std::shared_ptr<messages::DeployInstanceRequest> GetDeployInstanceReq(
    const FunctionMeta &funcMeta, const std::shared_ptr<messages::ScheduleRequest> &request);

void BuildDeploySpec(const FunctionMeta &funcMeta,
                     const std::shared_ptr<messages::DeployInstanceRequest> &deployInstanceRequest);

std::shared_ptr<messages::StaticFunctionChangeRequest> GetStaticFunctionChangeRequest(const InstanceInfo &instanceInfo,
                                                                                      int32_t status);

}  // namespace functionsystem
#endif  // FUNCTION_PROXY_LOCAL_SCHEDULER_INSTANCE_CONTROL_GENERATE_MESSAGE_H
