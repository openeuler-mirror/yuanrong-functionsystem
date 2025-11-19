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

#ifndef BUSPROXY_INCLUDE_STARTUP_H
#define BUSPROXY_INCLUDE_STARTUP_H

#include "busproxy/memory_monitor/memory_monitor.h"
#include "busproxy/registry/service_registry.h"
#include "common/status/status.h"
#include "common/constants/actor_name.h"
#include "function_proxy/common/communication/proxy/actor.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"
#include "function_proxy/common/posix_client/data_plane_client/data_interface_client_manager_proxy.h"
#include "function_proxy/busproxy/instance_proxy/instance_proxy.h"
#include "function_proxy/busproxy/instance_proxy/request_router.h"

namespace functionsystem {

struct BusProxyStartParam {
    std::string nodeID;
    std::string modelName;
    std::string localAddress;
    int serviceTTL;
    std::shared_ptr<DataInterfaceClientManagerProxy> dataInterfaceClientMgr{ nullptr };
    std::shared_ptr<function_proxy::DataPlaneObserver> dataPlaneObserver{ nullptr };
    std::shared_ptr<MemoryMonitor> memoryMonitor{ nullptr };
    std::shared_ptr<function_proxy::InternalIAM> internalIam{ nullptr };
    bool isEnablePerf;
    bool unRegisterWhileStop;
};

class BusproxyStartup {
public:
    BusproxyStartup() = delete;
    BusproxyStartup(BusProxyStartParam &&param, const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor);

    ~BusproxyStartup();

    Status Run();

    Status Stop() const;

    void Await() const;

private:
    void StartProxyActor(const std::string &nodeID, const std::string &modelName);
    void InitRegistry(const litebus::AID &proxyActorAID, const std::string &nodeID,
                      std::shared_ptr<MetaStorageAccessor> metaStorage);
    void StartRequestRouter();

    BusProxyStartParam param_;
    std::shared_ptr<proxy::Actor> proxyActor_{ nullptr };
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    std::shared_ptr<ServiceRegistry> registry_{ nullptr };
    std::shared_ptr<busproxy::RequestRouter> requestRouter_;
};
}  // namespace functionsystem

#endif  // BUSPROXY_INCLUDE_STARTUP_H
