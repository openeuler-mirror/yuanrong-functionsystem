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
#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_VIEW_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_VIEW_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "common/data_view/proxy_view/proxy_view.h"
#include "common/posix_client/data_plane_client/data_interface_client_manager_proxy.h"
#include "common/state_machine//instance_listener.h"
#include "common/status/status.h"
#include "common/types/instance_state.h"
#include "function_proxy/busproxy/instance_proxy/instance_proxy.h"

namespace functionsystem::busproxy {
using EventHandler = std::function<void(const std::string &, const resources::InstanceInfo &)>;
// InstanceView used for manager lifecycle of instance proxy.
class InstanceView : public InstanceListener {
public:
    explicit InstanceView(const std::string &nodeID);
    ~InstanceView() override;
    void Update(const std::string &instanceID, const resources::InstanceInfo &instanceInfo,
                bool isForceUpdate) override;
    void Delete(const std::string &instanceID, int64_t modRevision = -1) override;

    void BindDataInterfaceClientManager(
        const std::shared_ptr<DataInterfaceClientManagerProxy> &dataInterfaceClientManager)
    {
        dataInterfaceClientManager_ = dataInterfaceClientManager;
    }

    void BindProxyView(const std::shared_ptr<ProxyView> &proxyView)
    {
        proxyView_ = proxyView;
    }

    Status SubscribeInstanceEvent(const std::string &subscriber, const std::string &targetInstance,
                                  bool ignoreNonExist = false);

    void NotifyMigratingRequest(const std::string &instanceID);

private:
    void Creating(const std::string &instanceID, const resources::InstanceInfo &instanceInfo);
    void Running(const std::string &instanceID, const resources::InstanceInfo &instanceInfo);
    void Fatal(const std::string &, const resources::InstanceInfo &);
    void Reject(const std::string &, const resources::InstanceInfo &);
    void SpawnInstanceProxy(const std::string &, const resources::InstanceInfo &);
    void ReadyStatusChanged(const std::string &, const resources::InstanceInfo &);
    void NotifyReady(const std::string &, const resources::InstanceInfo &);
    void NotifyChanged(const litebus::AID &aid, const std::string &instanceID, const std::string &functionProxyID,
                       const std::shared_ptr<InstanceRouterInfo> &routeInfo);
    void NotifySubscriberInstanceReady(const std::string &, const resources::InstanceInfo &);
    void TerminateMigratedInstanceProxy(const std::string &instanceID);

    std::shared_ptr<DataInterfaceClientManagerProxy> dataInterfaceClientManager_ { nullptr };
    std::shared_ptr<ProxyView> proxyView_ { nullptr };
    std::unordered_map<std::string, std::shared_ptr<InstanceProxy>> localInstances_;
    // InstanceInfo should be replaced by shared ptr in future
    std::unordered_map<std::string, resources::InstanceInfo> allInstances_;
    // key : subscribed instance value: subscribers
    std::unordered_map<std::string, std::unordered_set<std::string>> subscribedInstances_;
    // key : subscriber value: subscribed instance
    std::unordered_map<std::string, std::unordered_set<std::string>> subscribers_;
    std::unordered_map<InstanceState, EventHandler> eventHandlers_;
    std::string nodeID_;
};
}  // namespace functionsystem::busproxy
#endif