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

#include "busproxy_startup.h"

#include <litebus.hpp>
#include <string>
#include <utility>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/meta_store_kv_operation.h"
#include "function_proxy/busproxy/instance_proxy/instance_proxy.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"
#include "function_proxy/common/observer/data_plane_observer/data_plane_observer.h"
#include "function_proxy/common/observer/observer_actor.h"

using namespace functionsystem;

namespace functionsystem {

BusproxyStartup::BusproxyStartup(BusProxyStartParam &&param,
                                 const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
    : param_(std::move(param))
{
    metaStorageAccessor_ = metaStorageAccessor;
}

BusproxyStartup::~BusproxyStartup()
{
    ASSERT_IF_NULL(proxyActor_);
    litebus::Terminate(proxyActor_->GetAID());
    litebus::Await(proxyActor_->GetAID());

    ASSERT_IF_NULL(requestRouter_);
    litebus::Terminate(requestRouter_->GetAID());
    litebus::Await(requestRouter_->GetAID());

    metaStorageAccessor_ = nullptr;
    proxyActor_ = nullptr;
    registry_ = nullptr;
    requestRouter_ = nullptr;
}

void BusproxyStartup::StartProxyActor(const std::string &nodeID, const std::string &modelName)
{
    // start proxy actor
    std::string proxyActorName = modelName + nodeID;
    proxyActor_ = std::make_shared<proxy::Actor>(proxyActorName);
    litebus::Spawn(proxyActor_);
}

void BusproxyStartup::InitRegistry(const litebus::AID &proxyActorAID, const std::string &nodeID,
                                   std::shared_ptr<MetaStorageAccessor> metaStorage)
{
    registry_ = std::make_shared<ServiceRegistry>();
    auto info = function_proxy::GetServiceRegistryInfo(nodeID, proxyActorAID);
    registry_->Init(std::move(metaStorage), info, param_.serviceTTL);
}

void BusproxyStartup::StartRequestRouter()
{
    // start common instanceProxy actor
    requestRouter_ = std::make_shared<busproxy::RequestRouter>(REQUEST_ROUTER_NAME);
    litebus::Spawn(requestRouter_);
}

Status BusproxyStartup::Run()
{
    YRLOG_INFO("Start to init Busproxy, nodeID: {}, modelName: {}", param_.nodeID, param_.modelName);

    // start observer actor
    InvocationHandler::BindUrl(param_.localAddress);
    busproxy::InstanceProxy::BindObserver(param_.dataPlaneObserver);
    busproxy::RequestDispatcher::BindDataInterfaceClientManager(param_.dataInterfaceClientMgr);
    busproxy::RequestDispatcher::BindInternalIAM(param_.internalIam);
    InvocationHandler::BindInstanceProxy(std::make_shared<busproxy::InstanceProxyWrapper>());
    InvocationHandler::BindMemoryMonitor(param_.memoryMonitor);
    InvocationHandler::BindInternalIAM(param_.internalIam);
    InvocationHandler::EnablePerf(param_.isEnablePerf);
    busproxy::Perf::Enable(param_.isEnablePerf);

    StartRequestRouter();

    // start proxy actor
    StartProxyActor(param_.nodeID, param_.modelName);

    // start registry
    if (proxyActor_ == nullptr) {
        YRLOG_ERROR("invalid parameter, proxy actor is null");
        return Status(StatusCode::FAILED, "proxy actor is null");
    }
    InitRegistry(proxyActor_->GetAID(), param_.nodeID, metaStorageAccessor_);
    ASSERT_IF_NULL(registry_);
    auto status = registry_->Register();
    if (status.IsError()) {
        YRLOG_ERROR("Failed to register Busproxy to meta store, nodeID: {}, errMsg: {}", param_.nodeID,
                    status.ToString());
        return status;
    }

    YRLOG_INFO("Succeed to init Busproxy, nodeID: {}, modelName: {}", param_.nodeID, param_.modelName);

    return Status(StatusCode::SUCCESS);
}

Status BusproxyStartup::Stop() const
{
    if (registry_ != nullptr && param_.unRegisterWhileStop) {
        registry_->Stop();
    }
    if (proxyActor_ != nullptr) {
        litebus::Terminate(proxyActor_->GetAID());
    }
    if (requestRouter_ != nullptr) {
        litebus::Terminate(requestRouter_->GetAID());
    }
    return Status::OK();
}

void BusproxyStartup::Await() const
{
    ASSERT_IF_NULL(proxyActor_);
    litebus::Await(proxyActor_->GetAID());

    ASSERT_IF_NULL(requestRouter_);
    litebus::Await(requestRouter_->GetAID());
}

}  // namespace functionsystem
