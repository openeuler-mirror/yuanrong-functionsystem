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

#ifndef COMMON_UTILS_GENERATE_MESSAGE_H
#define COMMON_UTILS_GENERATE_MESSAGE_H

#include <string>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/status/status.h"
#include "async/uuid_generator.hpp"

namespace functionsystem {
inline messages::ScheduleResponse GenScheduleResponse(int32_t code, const std::string &message,
                                                      const std::string &traceId,
                                                      const std::string &requestID)
{
    messages::ScheduleResponse rsp;
    rsp.set_code(code);
    rsp.set_message(message);
    rsp.set_traceid(traceId);
    rsp.set_requestid(requestID);
    return rsp;
}

inline messages::Registered GenRegistered(int32_t code, const std::string &message)
{
    messages::Registered rsp;
    rsp.set_code(code);
    rsp.set_message(message);
    return rsp;
}

inline messages::Registered GenRegistered(StatusCode code, const std::string &message)
{
    messages::Registered rsp;
    rsp.set_code(static_cast<int32_t>(code));
    rsp.set_message(message);
    return rsp;
}

inline messages::Registered GenRegistered(StatusCode code, const std::string &message,
                                          const messages::ScheduleTopology &topology)
{
    messages::Registered rsp;
    rsp.set_code(static_cast<int32_t>(code));
    rsp.set_message(message);
    rsp.mutable_topo()->CopyFrom(topology);
    return rsp;
}

inline messages::Register GenRegister(const std::string &name, const std::string &address)
{
    messages::Register req;
    req.set_name(name);
    req.set_address(address);
    return req;
}

inline messages::Register GenRegister(const std::string &name, const std::string &address,
                                      const std::string &funcAgentRegisInfoInitStr)
{
    messages::Register req;
    req.set_name(name);
    req.set_address(address);
    req.set_message(funcAgentRegisInfoInitStr);
    return req;
}

inline messages::NotifySchedAbnormalRequest GenNotifySchedAbnormalRequest(const std::string &name)
{
    messages::NotifySchedAbnormalRequest req;
    req.set_schedname(name);
    return req;
}

inline messages::DeployInstanceResponse GenDeployInstanceResponse(const StatusCode &code, const std::string &msg,
                                                                  const std::string &requestID)
{
    messages::DeployInstanceResponse resp;
    resp.set_code(static_cast<int32_t>(code));
    resp.set_message(msg);
    resp.set_requestid(requestID);
    return resp;
}

inline messages::StaticFunctionChangeResponse GenStaticFunctionChangeResponse(const StatusCode &code,
                                                                              const std::string &msg,
                                                                              const std::string &requestID,
                                                                              const std::string &instanceID)
{
    messages::StaticFunctionChangeResponse resp;
    resp.set_code(static_cast<int32_t>(code));
    resp.set_message(msg);
    resp.set_requestid(requestID);
    resp.set_instanceid(instanceID);
    return resp;
}

inline std::shared_ptr<messages::KillInstanceRequest> GenKillInstanceRequest(const std::string &requestID,
                                                                             const std::string &instanceID,
                                                                             const std::string &traceID,
                                                                             const std::string &storageType,
                                                                             bool isMonopoly = false)
{
    auto req = std::make_shared<messages::KillInstanceRequest>();
    req->set_requestid(requestID);
    req->set_instanceid(instanceID);
    req->set_traceid(traceID);
    req->set_storagetype(storageType);
    req->set_ismonopoly(isMonopoly);
    return req;
}

inline messages::KillInstanceResponse GenKillInstanceResponse(const StatusCode &code, const std::string &message,
                                                              const std::string &requestID)
{
    messages::KillInstanceResponse req;
    req.set_requestid(requestID);
    req.set_code(static_cast<int32_t>(code));
    req.set_message(message);
    return req;
}

inline messages::UpdateInstanceStatusResponse GenUpdateInstanceStatusResponse(const StatusCode &status,
                                                                              const std::string &message,
                                                                              const std::string &requestID)
{
    messages::UpdateInstanceStatusResponse resp;
    resp.set_status(static_cast<int32_t>(status));
    resp.set_message(message);
    resp.set_requestid(requestID);
    return resp;
}

inline std::shared_ptr<messages::UpdateInstanceStatusRequest> GenUpdateInstanceStatusRequest(
    const std::string &instanceID, int32_t status, const std::string &requestID)
{
    auto req = std::make_shared<messages::UpdateInstanceStatusRequest>();
    auto info = req->mutable_instancestatusinfo();
    info->set_instanceid(instanceID);
    info->set_status(status);
    info->set_requestid(requestID);
    req->set_requestid(requestID);
    return req;
}

inline std::shared_ptr<KillRequest> GenKillRequest(const std::string &instanceID, int32_t signal)
{
    auto killRequest = std::make_shared<KillRequest>();
    killRequest->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    killRequest->set_instanceid(instanceID);
    killRequest->set_signal(signal);
    return killRequest;
}

inline KillResponse GenKillResponse(const common::ErrorCode &errCode, const std::string &message)
{
    KillResponse killRsp;
    killRsp.set_code(errCode);
    killRsp.set_message(message);

    return killRsp;
}

inline messages::StartInstanceResponse GenFailStartInstanceResponse(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const StatusCode &code,
    const std::string &msg = "start instance failed")
{
    messages::StartInstanceResponse response;
    response.set_code(static_cast<int32_t>(code));
    response.set_message(msg);
    response.set_requestid(request->runtimeinstanceinfo().requestid());
    return response;
}

inline internal::ForwardKillResponse GenForwardKillResponse(const std::string &requestID,
                                                            const common::ErrorCode errorCode,
                                                            const std::string &message)
{
    internal::ForwardKillResponse forwardKillResponse;
    forwardKillResponse.set_requestid(requestID);
    forwardKillResponse.set_code(errorCode);
    forwardKillResponse.set_message(message);
    return forwardKillResponse;
}

inline messages::ForwardKillResponse GenForwardKillResponse(const std::string &requestID, const int errorCode,
                                                            const std::string &message)
{
    messages::ForwardKillResponse forwardKillResponse;
    forwardKillResponse.set_requestid(requestID);
    forwardKillResponse.set_code(errorCode);
    forwardKillResponse.set_message(message);
    return forwardKillResponse;
}

inline std::shared_ptr<internal::ForwardKillRequest> GenForwardKillRequest(const std::string &requestID,
                                                                           const std::string &srcInstanceID,
                                                                           KillRequest &&killRequest)
{
    auto forwardKillRequest = std::make_shared<internal::ForwardKillRequest>();
    forwardKillRequest->set_requestid(requestID);
    forwardKillRequest->set_srcinstanceid(srcInstanceID);
    *forwardKillRequest->mutable_req() = killRequest;

    return forwardKillRequest;
}

inline StateSaveResponse GenStateSaveResponse(const common::ErrorCode &errCode, const std::string &message,
                                              const std::string &checkpointId = "")
{
    StateSaveResponse rsp;
    rsp.set_code(errCode);
    rsp.set_message(message);
    rsp.set_checkpointid(checkpointId);
    return rsp;
}

inline std::shared_ptr<runtime_rpc::StreamingMessage> GenStateSaveRspStreamMessage(const common::ErrorCode &errCode,
                                                                                   const std::string &message,
                                                                                   const std::string &checkpointId = "")
{
    auto response = std::make_shared<runtime_rpc::StreamingMessage>();
    *response->mutable_saversp() = std::move(GenStateSaveResponse(errCode, message, checkpointId));
    return response;
}

inline StateLoadResponse GenStateLoadResponse(const common::ErrorCode &errCode, const std::string &message,
                                              const std::string &state = "")
{
    StateLoadResponse rsp;
    rsp.set_code(errCode);
    rsp.set_message(message);
    rsp.set_state(state);
    return rsp;
}

inline std::shared_ptr<runtime_rpc::StreamingMessage> GenStateLoadRspStreamMessage(const common::ErrorCode &errCode,
                                                                                   const std::string &message,
                                                                                   const std::string &state = "")
{
    auto response = std::make_shared<runtime_rpc::StreamingMessage>();
    *response->mutable_loadrsp() = std::move(GenStateLoadResponse(errCode, message, state));
    return response;
}

inline std::shared_ptr<messages::DeployInstanceResponse> BuildDeployInstanceResponse(
    const messages::StartInstanceResponse &startInstanceResponse,
    const std::shared_ptr<messages::DeployInstanceRequest> &request)
{
    auto deployInstanceResponse = std::make_shared<messages::DeployInstanceResponse>();
    deployInstanceResponse->set_code(startInstanceResponse.code());
    deployInstanceResponse->set_message(startInstanceResponse.message());
    deployInstanceResponse->set_requestid(request->requestid());
    deployInstanceResponse->set_instanceid(request->instanceid());
    deployInstanceResponse->set_runtimeid(startInstanceResponse.startruntimeinstanceresponse().runtimeid());
    deployInstanceResponse->set_address(startInstanceResponse.startruntimeinstanceresponse().address());
    deployInstanceResponse->set_cputype(startInstanceResponse.startruntimeinstanceresponse().cputype());
    deployInstanceResponse->set_pid(startInstanceResponse.startruntimeinstanceresponse().pid());
    return deployInstanceResponse;
}

inline std::shared_ptr<messages::KillInstanceResponse> BuildKillInstanceResponse(const int32_t &code,
                                                                                 const std::string &message,
                                                                                 const std::string &requestID,
                                                                                 const std::string &instanceID)
{
    auto killInstanceResponse = std::make_shared<messages::KillInstanceResponse>();
    killInstanceResponse->set_code(code);
    killInstanceResponse->set_message(message);
    killInstanceResponse->set_requestid(requestID);
    killInstanceResponse->set_instanceid(instanceID);
    return killInstanceResponse;
}

inline runtime::CheckpointResponse GenCheckpointResponse(const common::ErrorCode &errCode, const std::string &message,
                                                         const std::string &state = "")
{
    runtime::CheckpointResponse rsp;
    rsp.set_code(errCode);
    rsp.set_message(message);
    rsp.set_state(state);
    return rsp;
}

inline runtime::RecoverResponse GenRecoverResponse(const common::ErrorCode &errCode, const std::string &message)
{
    runtime::RecoverResponse rsp;
    rsp.set_code(errCode);
    rsp.set_message(message);
    return rsp;
}

inline messages::UpdateAgentStatusResponse GenUpdateAgentStatusResponse(const std::string &requestID,
                                                                        const int32_t &status,
                                                                        const std::string &message)
{
    messages::UpdateAgentStatusResponse response;
    response.set_requestid(requestID);
    response.set_status(status);
    response.set_message(message);
    return response;
}

inline std::shared_ptr<messages::CancelSchedule> GenCancelSchedule(const std::string &id,
                                                                   const messages::CancelType &type,
                                                                   const std::string &reason)
{
    auto cancelRequest = std::make_shared<messages::CancelSchedule>();
    cancelRequest->set_id(id);
    cancelRequest->set_type(type);
    cancelRequest->set_reason(reason);
    cancelRequest->set_msgid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
    return cancelRequest;
}

}  // namespace functionsystem

#endif  // COMMON_UTILS_GENERATE_MESSAGE_H
