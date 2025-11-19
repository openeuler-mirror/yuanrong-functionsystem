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
#include "bus_service.h"

#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>

#include <cstdint>
#include <string>

#include "async/future.hpp"
#include "common/constants/signal.h"
#include "common/logs/logging.h"
#include "common/observer/control_plane_observer/control_plane_observer.h"
#include "common/proto/pb/posix/resource.pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/status/status.h"
#include "common/utils/generate_message.h"
#include "common/utils/param_check.h"
#include "common/utils/version.h"

namespace functionsystem::local_scheduler {
static const std::string DRIVER_DSTID = "driver";
static const std::string SOURCE = "source";
static const std::string DRIVER_FUNCKEY_SUFFIX = "/func/latest";
static const std::int32_t OBSERVER_TIMEOUT_MS = 60000;
static const uint64_t WAIT_REGISTERED_TIMEOUT = 10000;
static const std::int32_t CREATE_INSTANCE_CLIENT_TIMEOUT_MS = 300 * 1000;
static const std::string DEFAULT_TENANT_ID = "0";

resources::InstanceInfo GenInstanceInfo(const std::string &instanceID, const std::string &nodeID,
                                        const std::string &addr, const std::string &jobID)
{
    resources::InstanceInfo instanceInfo;
    instanceInfo.set_function(jobID + DRIVER_FUNCKEY_SUFFIX);
    instanceInfo.set_functionproxyid(nodeID);
    instanceInfo.set_instanceid(instanceID);
    instanceInfo.set_jobid(jobID);
    instanceInfo.set_runtimeid(instanceID);
    instanceInfo.set_runtimeaddress(addr);
    instanceInfo.set_tenantid(jobID);
    instanceInfo.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    (*instanceInfo.mutable_extensions())[SOURCE] = DRIVER_DSTID;
    return instanceInfo;
}

litebus::Future<::grpc::Status> PutInstance(
    const std::shared_ptr<function_proxy::ControlPlaneObserver> &controlPlaneObserver,
    const resources::InstanceInfo &instanceInfo)
{
    const auto &instanceID = instanceInfo.instanceid();
    litebus::Future<::grpc::Status> status =
        controlPlaneObserver->PutInstance(instanceInfo)
            .After(OBSERVER_TIMEOUT_MS,
                   [instanceID](litebus::Future<Status>) -> litebus::Future<Status> {
                       YRLOG_ERROR("timeout to put driver instance({})", instanceID);
                       std::string errorMessage = "timeout to put driver instance " + instanceID;
                       return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, errorMessage);
                   })
            .Then([instanceID](const Status &status) -> litebus::Future<::grpc::Status> {
                if (status.IsOk()) {
                    return ::grpc::Status();
                }
                YRLOG_ERROR("failed to put driver instance({}), error: {}", instanceID, status.GetMessage());
                return ::grpc::Status(::grpc::StatusCode::INTERNAL, status.GetMessage());
            });
    return status;
}

BusService::BusService(BusServiceParam &&param)
{
    param_ = param;
    waitRegisteredTimeout_ = WAIT_REGISTERED_TIMEOUT;
}

BusService::~BusService()
{
}

::grpc::Status BusService::DiscoverDriver(::grpc::ServerContext *, const ::bus_service::DiscoverDriverRequest *request,
                                          ::bus_service::DiscoverDriverResponse *response)
{
    if (!request || !response) {
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "invalid args nullptr" };
    }
    *response = ::bus_service::DiscoverDriverResponse();
    response->set_serverversion(BUILD_VERSION);
    response->set_nodeid(param_.nodeID);
    response->set_hostip(param_.hostIP);
    // check if request parameters are valid.
    if (!IsIPValid(request->driverip()) || !IsPortValid(request->driverport())) {
        YRLOG_ERROR("discover driver, address is invalid string");
        return { ::grpc::StatusCode::INVALID_ARGUMENT, "start grpc server failed." };
    }
    ASSERT_IF_NULL(param_.localSchedSrv);
    if (!param_.localSchedSrv->IsRegisteredToGlobal().WaitFor(waitRegisteredTimeout_).IsOK()) {
        YRLOG_ERROR("function_proxy is not ready for driver register");
        return  { ::grpc::StatusCode::DEADLINE_EXCEEDED, "function_proxy is not ready for driver register" };
    }
    std::string dstID = DRIVER_DSTID + "-" + request->jobid();
    std::string addr = request->driverip() + ":" + request->driverport();
    std::string jobID = request->jobid();
    if (!request->instanceid().empty()) {
        dstID = request->instanceid();
    }
    YRLOG_INFO("discover driver, address: {}:{}, jobID:{} instanceID:{} function:{}", request->driverip(),
               request->driverport(), request->jobid(), dstID, request->functionname());
    // create posix client
    ASSERT_IF_NULL(param_.instanceCtrl);
    ASSERT_IF_NULL(param_.controlPlaneObserver);
    auto instanceInfo = GenInstanceInfo(dstID, param_.nodeID, addr, jobID);
    if (!request->functionname().empty()) {
        instanceInfo.set_function(request->functionname());
        auto funcNameArr = litebus::strings::Split(request->functionname(), "/");
        if (!funcNameArr.empty()) {
            instanceInfo.set_tenantid(funcNameArr[0]);
        }
        if (param_.instanceCtrl->IsSystemTenant(instanceInfo.tenantid()).Get()) {
            instanceInfo.set_issystemfunc(true);
        }
    }
    // function-proxy is available if any of the following conditions is met:
    // 1.function-proxy router info is successfully written into etcd.
    // 2.function proxy is successfully registered into the global.
    // here, function-proxy router info has been written into the ETCD.
    if (auto status = PutInstance(param_.controlPlaneObserver, instanceInfo).Get(); !status.ok()) {
        // to make sure routeInfo published
        return status;
    }
    // posix connection would be built on route published
    return ::grpc::Status::OK;
}
}  // namespace functionsystem::local_scheduler