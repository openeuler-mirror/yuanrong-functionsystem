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


#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_BUS_SERVICE_BUS_SERVICE
#define FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_BUS_SERVICE_BUS_SERVICE

#include "common/proto/pb/posix/bus_service.grpc.pb.h"
#include "instance_control/instance_ctrl.h"
#include "local_scheduler_service/local_sched_srv.h"

namespace functionsystem::local_scheduler {
struct BusServiceParam {
    std::string nodeID;
    // controlPlaneObserver manages instances of driver jobId.
    std::shared_ptr<function_proxy::ControlPlaneObserver> controlPlaneObserver;
    // controlInterfaceClientMgr is for creating a grpc stream client.
    std::shared_ptr<ControlInterfaceClientManagerProxy> controlInterfaceClientMgr;
    // instanceCtrl is for killing all instance given the driver jobId.
    std::shared_ptr<InstanceCtrl> instanceCtrl;
    // localSchedSrv is for check local is registered to global
    std::shared_ptr<LocalSchedSrv> localSchedSrv;
    // enable posix server on proxy
    bool isEnableServerMode = false;
    // host_ip
    std::string hostIP;
};

class BusService final : public bus_service::BusService::Service {
public:
    explicit BusService(BusServiceParam &&param);

    ~BusService() override;

    ::grpc::Status DiscoverDriver(::grpc::ServerContext* context,
                                  const ::bus_service::DiscoverDriverRequest* request,
                                  ::bus_service::DiscoverDriverResponse* response) override;

    static void DriverDisconnection(const std::shared_ptr<function_proxy::ControlPlaneObserver> &controlPlaneObserver,
                             const std::shared_ptr<ControlInterfaceClientManagerProxy> &controlInterfaceClientMgr,
                             const std::shared_ptr<InstanceCtrl> &instanceCtrl,
                             const std::string &jobID, const std::string &instanceID);

private:
    BusServiceParam param_;
    uint64_t waitRegisteredTimeout_;
};
} // namespace functionsystem::local_scheduler
#endif // FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_BUS_SERVICE_BUS_SERVICE