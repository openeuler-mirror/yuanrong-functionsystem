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

#include "status.h"

#include <iostream>
#include <map>
#include <thread>
#include <vector>

namespace functionsystem {

std::map<enum StatusCode, std::string> Status::statusInfoMap_ = {
    // Common
    { FAILED, "Common error code" },
    { SUCCESS, "Ok" },
    { RESERVED, "Reserved error code" },
    { LOG_CONFIG_ERROR, "Log config error" },
    { PARAMETER_ERROR, "Parameter error" },
    { ENV_CONFIG_ERROR, "Env config error" },
    { REQUEST_TIME_OUT, "Request timeout error" },
    { RESOURCE_NOT_ENOUGH, "Instance schedule with resource not enough error" },
    { SCHEDULE_CONFLICTED, "Instance schedule conflict error" },
    { INSTANCE_ALLOCATED, "Instance was already been scheduled error" },
    { FILE_NOT_FOUND, "File not found error" },
    { JSON_PARSE_ERROR, "Json parse error" },
    { REGISTER_ERROR, "Register error" },
    { INVALID_RESOURCE_PARAMETER,
      "invalid resource parameter, request resource is greater than each node's max resource" },
    { AFFINITY_SCHEDULE_FAILED, "affinity schedule failed"},

    // Common component RPC
    { SYNC_GRPC_CALL_ERROR, "Failed to call grpc interface Sync" },
    { GRPC_CQ_ERROR, "Grpc completion queue return error" },
    { GRPC_CALL_OBJ_ERROR, "Failed to allocate grpc call object memory" },
    { GRPC_OK, "grpc OK" },
    { GRPC_CANCELLED, "grpc error cancel" },
    { GRPC_UNKNOWN, "grpc error unknown" },
    { GRPC_INVALID_ARGUMENT, "grpc error invalid argument" },
    { GRPC_DEADLINE_EXCEEDED, "grpc error deadline exceeded" },
    { GRPC_NOT_FOUND, "grpc error not found" },
    { GRPC_ALREADY_EXISTS, "grpc error already exists" },
    { GRPC_PERMISSION_DENIED, "grpc error permission denied" },
    { GRPC_RESOURCE_EXHAUSTED, "grpc error resource exhausted" },
    { GRPC_FAILED_PRECONDITION, "grpc error failed precondition" },
    { GRPC_ABORTED, "grpc error aborted" },
    { GRPC_OUT_OF_RANGE, "grpc error out of range" },
    { GRPC_UNIMPLEMENTED, "grpc error unimplemented" },
    { GRPC_INTERNAL, "grpc error internal" },
    { GRPC_UNAVAILABLE, "grpc error unavailable" },
    { GRPC_DATA_LOSS, "grpc error data loss" },
    { GRPC_UNAUTHENTICATED, "grpc error unauthenticated" },

    // BusProxy
    { BP_DATASYSTEM_ERROR, "Datasystem error in busproxy" },
    { BP_INSTANCE_NOT_FOUND, "Instance not found in busproxy" },
    { BP_META_STORAGE_PUT_ERROR, "Meta storage put error in busproxy" },
    { BP_META_STORAGE_DELETE_ERROR, "Meta storage delete error in busproxy" },
    { BP_META_STORAGE_REVOKE_ERROR, "Meta storage revoke error in busproxy" },
    { BP_LEASE_ID_NOT_FOUND, "Lease ID not found in busproxy" },
    { BP_PROXYACTOR_NULL, "Null proxy actor in client" },
    { BP_META_STORAGE_GRANT_ERROR, "Meta storage grant error in busproxy" },
    { INSTANCE_HEARTBEAT_LOST, "instance heart beat lost"},
    { INSTANCE_HEALTH_CHECK_ERROR, "instance heart beat check health failed" },
    { INSTANCE_SUB_HEALTH, "instance heart beat sub health" },

    // Function Accessor
    { FA_HTTP_REGISTER_HANDLER_NULL_ERROR, "Try to register null handler" },
    { FA_HTTP_REGISTER_REPEAT_URL_ERROR, "Try to register the same url again" },
    { FA_REQUIRED_CPU_SIZE_INVALID, "Required CPU resource size is invalid" },
    { FA_REQUIRED_MEMORY_SIZE_INVALID, "Required memory resource size is invalid" },
    { FA_CPU_NOT_ENOUGH, "CPU resource not enough" },
    { FA_MEMORY_NOT_ENOUGH, "Memory resources not enough" },
    { FA_CUSTOM_RESOURCE_NOT_ENOUGH, "Custom resources not enough" },
    { FA_FUNCTION_META_NOT_EXISTED, "Function not existed in meta" },
    { FA_FUNCTION_META_EMPTY_CPU, "Function meta not contain CPU resources" },
    { FA_FUNCTION_META_EMPTY_MEMORY, "Function meta not contain memory resources" },

    // Global scheduler
    { GS_GET_FROM_METASTORE_FAILED, "Failed to get from MetaStore in GlobalScheduler" },
    { GS_PUT_TO_METASTORE_FAILED, "Failed to put into MetaStore in GlobalScheduler" },
    { GS_SCHED_TOPOLOGY_BROKEN, "Scheduler topology stored in MetaStore is broken" },
    { GS_ACTIVATE_DOMAIN_FAILED, "Failed to activate domain scheduler" },

    // local scheduler
    { LS_DOMAIN_SCHEDULER_AID_EMPTY, "Domain scheduler aid is empty in local scheduler when registering" },
    { LS_GLOBAL_SCHEDULER_AID_EMPTY, "Global scheduler aid is empty in local scheduler when registering" },
    { LS_INSTANCE_CTRL_IS_NULL, "instance control is null in local scheduler" },
    { LS_META_STORE_ACCESSOR_IS_NULL, "meta store accessor is null in local scheduler" },
    { LS_SYNC_KILL_INSTANCE_FAIL, "instance ctrl failed to kill instance when sync instances" },
    { LS_SYNC_DEPLOY_INSTANCE_FAIL, "instance ctrl failed to deploy instance when sync instances" },
    { LS_SYNC_INSTANCE_FAIL, "instance ctrl failed to sync instances" },
    { LS_DEPLOY_INSTANCE_FAILED, "Failed to deploy instance in local scheduler" },
    { LS_RESOURCE_VIEW_IS_NULL, "resource view is null in local scheduler" },
    { LS_SYNC_INSTANCE_COMPLETE, "The sync instance has been completed" },
    { LS_REQUEST_NOT_FOUND, "The request of create not found" },
    { LS_UPDATE_INSTANCE_FAIL, "failed to update instance info to MetaStore" },
    { LS_AGENT_EVICTED, "failed to register, agent has been evicted" },

    // function agent
    { FUNC_AGENT_OBS_OPEN_FILE_ERROR, "Function agent failed to open obs file" },
    { FUNC_AGENT_OBS_GET_OBJECT_ERROR, "Function agent failed to get object from obs" },
    { FUNC_AGENT_PING_PONG_IS_NULL, "Function agent's ping pong driver is null" },
    { FUNC_AGENT_RESOURCE_UNIT_IS_NULL, "Function agent's resource unit is null" },
    { FUNC_AGENT_FAILED_DEPLOY, "Function agent failed to deploy code" },
    { FUNC_AGENT_DEPLOYMENT_CONFIG_NOT_FOUND, "Function agent failed to find deployment config for this request" },
    { FUNC_AGENT_START_HEARTBEAT_FAILED, "Function agent faield to start heartbeat observer" },
    { FUNC_AGENT_STATUS_VPC_PROBE_FAILED, "Function agent failed to probe network" },
    { FUNC_AGENT_OBS_CONNECTION_ERROR, "Function agent failed to connect to obs" },

    // runtime manager
    { RUNTIME_MANAGER_DISK_USAGE_EXCEED_LIMIT, "runtime manager disk usage exceed limit" },
};

std::map<enum StatusCode, enum StatusCode> Status::codeToPosix_ = {
    { PARAMETER_ERROR, ERR_PARAM_INVALID },
    { RESOURCE_NOT_ENOUGH, ERR_RESOURCE_NOT_ENOUGH },
    { SCHEDULE_CONFLICTED, ERR_RESOURCE_NOT_ENOUGH },
    { REQUEST_TIME_OUT, ERR_INNER_COMMUNICATION },
    { INSTANCE_SUB_HEALTH, ERR_INSTANCE_SUB_HEALTH },
    { AFFINITY_SCHEDULE_FAILED, ERR_RESOURCE_CONFIG_ERROR },

    { BP_INSTANCE_NOT_FOUND, ERR_INSTANCE_NOT_FOUND },
    { BP_META_STORAGE_PUT_ERROR, ERR_ETCD_OPERATION_ERROR },
    { INSTANCE_TRANSACTION_WRONG_VERSION, ERR_ETCD_OPERATION_ERROR },
    { BP_META_STORAGE_DELETE_ERROR, ERR_ETCD_OPERATION_ERROR },
    { BP_META_STORAGE_REVOKE_ERROR, ERR_ETCD_OPERATION_ERROR },

    { LS_UPDATE_INSTANCE_FAIL, ERR_ETCD_OPERATION_ERROR },
    { LS_DEPLOY_INSTANCE_FAILED, ERR_INNER_COMMUNICATION},
    { LS_FORWARD_INSTANCE_MANAGER_TIMEOUT, ERR_INNER_COMMUNICATION },

    { FUNC_AGENT_REQUEST_ID_ILLEGAL_ERROR, ERR_PARAM_INVALID },
    { FUNC_AGENT_INVALID_DEPLOYER_ERROR, ERR_PARAM_INVALID },
    { FUNC_AGENT_INVALID_TOKEN_ERROR, ERR_PARAM_INVALID },
    { FUNC_AGENT_INVALID_ACCESS_KEY_ERROR, ERR_PARAM_INVALID },
    { FUNC_AGENT_INVALID_SECRET_ACCESS_KEY_ERROR, ERR_PARAM_INVALID },
    { FUNC_AGENT_INVALID_WORKING_DIR_FILE, ERR_PARAM_INVALID },
    { FUNC_AGENT_UNSUPPORTED_WORKING_DIR_SCHEMA, ERR_PARAM_INVALID },
    { FUNC_AGENT_MKDIR_DEST_WORKING_DIR_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_SET_NETWORK_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_NETWORK_WORK_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_GET_OBJECT_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_INIT_OPTIONS_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_OPEN_FILE_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_ADD_BUCKET_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_DEL_BUCKET_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_PUT_OBJECT_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_GET_OBJECT_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_RENAME_TMP_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },
    { FUNC_AGENT_OBS_CONNECTION_ERROR, ERR_FUNCTION_AGENT_OPERATION_ERROR },

    { RUNTIME_MANAGER_PARAMS_INVALID, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_BUILD_ARGS_INVALID, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_POST_START_EXEC_FAILED, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_MOUNT_VOLUME_FAILED, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_PORT_UNAVAILABLE, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_EXEC_PATH_NOT_FOUND, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_CREATE_EXEC_FAILED, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, ERR_PARAM_INVALID },
    { INVALID_RESOURCE_PARAMETER, ERR_RESOURCE_CONFIG_ERROR },
    { RUNTIME_MANAGER_STOP_INSTANCE_FAILED, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_START_INSTANCE_FAILED, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_DEPLOY_DIR_IS_EMPTY, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_DEBUG_SERVER_NOTFOUND, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_CONDA_PARAMS_INVALID, ERR_PARAM_INVALID },
    { RUNTIME_MANAGER_CONDA_ENV_FILE_WRITE_FAILED, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
    { RUNTIME_MANAGER_CONDA_ENV_NOT_EXIST, ERR_RUNTIME_MANAGER_OPERATION_ERROR },
};

common::ErrorCode Status::GetPosixErrorCode(enum StatusCode code)
{
    if (NeedKeepStatusCode(code)) {
        return static_cast<common::ErrorCode>(code);
    }

    // other code maps to ERR_INNER_SYSTEM_ERROR
    const auto iter = codeToPosix_.find(code);
    return static_cast<common::ErrorCode>(iter == codeToPosix_.end() ? StatusCode::ERR_INNER_SYSTEM_ERROR
                                                                     : iter->second);
}

common::ErrorCode Status::GetPosixErrorCode(common::ErrorCode code)
{
    return GetPosixErrorCode(static_cast<enum StatusCode>(code));
}

common::ErrorCode Status::GetPosixErrorCode(int32_t code)
{
    return GetPosixErrorCode(static_cast<enum StatusCode>(code));
}

StatusCode Status::GrpcCode2StatusCode(int grpcErrCode)
{
    return static_cast<enum StatusCode>(static_cast<int>(GRPC_OK) + grpcErrCode);
}

bool Status::NeedKeepStatusCode(enum StatusCode code)
{
    // keep success code and posix code
    const auto minPosixCode = static_cast<enum StatusCode>(1000);
    const auto maxPosixCode = static_cast<enum StatusCode>(10000);
    return code == SUCCESS || (code >= minPosixCode && code < maxPosixCode);
}

std::string Status::GetStatusInfo(enum StatusCode code)
{
    const auto iter = statusInfoMap_.find(code);
    return iter == statusInfoMap_.end() ? "" : iter->second;
}

struct Status::Data {
    enum StatusCode statusCode = SUCCESS;
    std::string statusInfo = GetStatusInfo(statusCode);
    std::vector<std::string> detailInfo;
    int lineOfCode = -1;
    std::string fileName;
};

Status::Status() : data_(std::make_shared<Data>())
{
}

Status::Status(enum StatusCode statusCode, const std::string &errMsg) : data_(std::make_shared<Data>())
{
    if (data_ == nullptr) {
        return;
    }

    data_->statusCode = statusCode;

    std::ostringstream ss;
#ifdef __DEBUG__
    ss << "Thread ID " << std::this_thread::get_id() << " " << GetStatusInfo(statusCode);
#else
    ss << GetStatusInfo(statusCode);
#endif
    data_->statusInfo = ss.str();

    if (!errMsg.empty()) {
        data_->detailInfo.push_back(errMsg);
    }
}

Status::Status(const enum StatusCode code, int lineOfCode, const char *fileName, const std::string &errMsg)
    : data_(std::make_shared<Data>())
{
    if (data_ == nullptr) {
        return;
    }
    data_->statusCode = code;
    data_->lineOfCode = lineOfCode;

    if (!errMsg.empty()) {
        data_->detailInfo.push_back(errMsg);
    }

    if (fileName != nullptr) {
        data_->fileName = fileName;
    }

    std::ostringstream ss;
#ifdef __DEBUG__
    ss << "Thread ID " << std::this_thread::get_id() << " " << GetStatusInfo(code);
#else
    ss << GetStatusInfo(code);
#endif
    ss << "\n";
    ss << "Line of code : " << lineOfCode << "\n";

    if (fileName != nullptr) {
        ss << "File         : " << fileName;
    }

    data_->statusInfo = ss.str();
}

std::string Status::ToString() const
{
    if (data_ == nullptr) {
        return "";
    }
    std::ostringstream ss;
    ss << "[code: " << static_cast<int>(data_->statusCode) << ", status: " << data_->statusInfo;
    if (data_->detailInfo.empty()) {
        ss << "]";
        return ss.str();
    }
    ss << "], detail: ";
    for (auto &info : data_->detailInfo) {
        ss << "[" << info << "]";
    }
    ss << "]";
    return ss.str();
}

std::string Status::GetMessage() const
{
    if (data_->detailInfo.empty()) {
        return "[]";
    }
    std::ostringstream ss;
    for (auto &info : data_->detailInfo) {
        ss << "[" << info << "]";
    }
    return ss.str();
}

const std::string &Status::RawMessage() const
{
    if (data_->detailInfo.empty()) {
        static std::string nullStr;
        return nullStr;
    }
    return data_->detailInfo[0];
}

bool Status::MultipleErr() const
{
    if (data_->detailInfo.empty()) {
        return false;
    }
    return data_->detailInfo.size() > 1;
}

void Status::AppendMessage(const std::string &errMsg)
{
    if (data_ == nullptr) {
        return;
    }
    data_->detailInfo.push_back(errMsg);
}

enum StatusCode Status::StatusCode() const
{
    if (data_ == nullptr) {
        return SUCCESS;
    }
    return data_->statusCode;
}

int Status::GetLineOfCode() const
{
    if (data_ == nullptr) {
        return -1;
    }
    return data_->lineOfCode;
}

std::ostream &operator<<(std::ostream &os, const Status &s)
{
    os << s.ToString();
    return os;
}

bool Status::operator==(const Status &other) const
{
    if (data_ == nullptr || other.data_ == nullptr) {
        return false;
    }
    if (data_ == nullptr && other.data_ == nullptr) {
        return true;
    }
    return data_->statusCode == other.data_->statusCode;
}

bool Status::operator==(enum StatusCode otherCode) const
{
    return StatusCode() == otherCode;
}

bool Status::operator!=(const Status &other) const
{
    return !operator==(other);
}

bool Status::operator!=(enum StatusCode otherCode) const
{
    return !operator==(otherCode);
}

Status::operator bool() const
{
    return (StatusCode() == SUCCESS);
}

Status::operator int() const
{
    return static_cast<int>(StatusCode());
}

Status Status::OK()
{
    // Return a Status with default status code SUCCESS.
    return {};
}

bool Status::IsOk() const
{
    return (StatusCode() == StatusCode::SUCCESS);
}

bool Status::IsError() const
{
    return !IsOk();
}

}  // namespace functionsystem
