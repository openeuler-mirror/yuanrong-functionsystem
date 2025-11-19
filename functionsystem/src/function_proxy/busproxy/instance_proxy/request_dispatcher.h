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

#ifndef FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_REQUEST_DISPATCHER_H
#define FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_REQUEST_DISPATCHER_H

#include <memory>
#include <string>

#include "function_proxy/busproxy/instance_proxy/call_cache.h"
#include "function_proxy/busproxy/instance_proxy/perf.h"
#include "function_proxy/busproxy/instance_proxy/forward_interface.h"
#include "function_proxy/common/iam/internal_iam.h"
#include "function_proxy/common/posix_client/data_plane_client/data_interface_posix_client.h"
#include "function_proxy/common/posix_client/data_plane_client/data_interface_client_manager_proxy.h"

namespace functionsystem::busproxy {

SharedStreamMsg CreateCallResponse(const common::ErrorCode &code, const std::string &message,
                                   const std::string &messageID);

struct InstanceRouterInfo {
    bool isLocal = false;
    bool isReady = false;
    std::string runtimeID;
    std::string proxyID;
    litebus::AID remote;
    std::string tenantID;
    std::string function;
    std::shared_ptr<DataInterfacePosixClient> localClient = nullptr;
};

struct CallerInfo {
    std::string instanceID;
    std::string tenantID;
};

class RequestDispatcher {
public:
    RequestDispatcher(const std::string &instanceID, bool isLocal, const std::string &tenantID,
                      const std::shared_ptr<ForwardInterface> &remote, const std::shared_ptr<Perf> &perf)
        : instanceID_(instanceID),
          tenantID_(tenantID),
          local_(isLocal),
          callCache_(std::make_shared<CallCache>()),
          remoteClient_(remote),
          perf_(perf)
    {
    }
    ~RequestDispatcher() = default;
    litebus::Future<SharedStreamMsg> Call(const SharedStreamMsg &request,
                                          const CallerInfo &callerInfo);

    litebus::Future<SharedStreamMsg> CallResult(const SharedStreamMsg &request);

    void OnCall(const SharedStreamMsg &, const std::string &, const std::string &);

    void OnCallResult(const SharedStreamMsg &, const std::string &, common::ErrorCode callResultCode);

    void UpdateInfo(const std::shared_ptr<InstanceRouterInfo> &info);

    void Fatal(const std::string &, const StatusCode &);

    void Reject(const std::string &, const StatusCode &);

    std::list<litebus::Future<SharedStreamMsg>> GetOnRespFuture();

    Status AuthorizeInvoke(const std::string &callerTenantID);

    std::string GetTenantID()
    {
        return tenantID_;
    }

    inline void UpdateRemoteAID(const litebus::AID &aid)
    {
        remoteAid_ = aid;
        local_ = false;
    }

    static void BindDataInterfaceClientManager(const std::shared_ptr<DataInterfaceClientManagerProxy> &clientManager)
    {
        clientManager_ = clientManager;
    }

    static void BindInternalIAM(const std::shared_ptr<functionsystem::function_proxy::InternalIAM> &internalIam)
    {
        internalIam_ = internalIam;
    }

private:
    void TriggerCall(const std::string &requestID);
    void ResponseAllMessage();

    inline static std::shared_ptr<DataInterfaceClientManagerProxy> clientManager_ { nullptr };
    inline static std::shared_ptr<functionsystem::function_proxy::InternalIAM> internalIam_{ nullptr };

    void ReportCallTimesMetrics();

    void ReportCallLatency(const std::string &requestID, common::ErrorCode errCode);

    void SendNotify(const std::basic_string<char> &req, const std::shared_ptr<CallRequestContext> &context) const;

    std::string instanceID_;
    std::string runtimeID_;
    std::string proxyID_;
    std::string tenantID_;
    std::string function_;
    litebus::AID remoteAid_;
    bool local_ { false };
    bool isFatal_ { false };
    bool isReject_ { false };
    bool isReady_ { false };
    std::string fatalMsg_;
    StatusCode fatalCode_ {StatusCode::SUCCESS};
    std::shared_ptr<CallCache> callCache_ { nullptr };
    std::weak_ptr<ForwardInterface> remoteClient_;
    std::shared_ptr<DataInterfacePosixClient> dataInterfaceClient_ { nullptr };
    std::shared_ptr<Perf> perf_ { nullptr };
    std::unordered_set<std::string> verifiedCallerTenantIDs_{};

    int callTimes_ = 0;
    int failedCallTimes_ = 0;

    std::map<std::string, std::chrono::system_clock::time_point> localStartCallTimeMap_;
};
}  // namespace functionsystem::busproxy

#endif  // FUNCTION_PROXY_BUSPROXY_INSTANCE_PROXY_REQUEST_DISPATCHER_H
