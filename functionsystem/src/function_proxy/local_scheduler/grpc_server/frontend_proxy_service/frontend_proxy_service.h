/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_SERVICE_H
#define FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_SERVICE_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpcpp/server_context.h>
#include <grpcpp/support/sync_stream.h>

#include "async/future.hpp"
#include "common/proto/pb/posix/frontend_proxy_service.grpc.pb.h"
#include "common/proto/pb/posix_pb.h"
#include "local_scheduler/instance_control/frontend_kill_cleanup_snapshot.h"

namespace functionsystem::local_scheduler {

struct FrontendProxyServiceParam {
    using InvokeDispatcher =
        std::function<litebus::Future<SharedStreamMsg>(const std::string &, const SharedStreamMsg &)>;
    using InvokeTenantAuthorizer = std::function<bool(const std::string &, const std::string &)>;
    using CreateDispatcher =
        std::function<litebus::Future<SharedStreamMsg>(const std::string &, const SharedStreamMsg &)>;
    using CreateReadyDispatcher =
        std::function<litebus::Future<::frontend_proxy::CreateInstanceResponse>(
            const ::frontend_proxy::CreateInstanceRequest &)>;
    using CreateWaitCanceller = std::function<void(const std::string &, const std::string &)>;
    using KillDispatcher =
        std::function<litebus::Future<SharedStreamMsg>(const std::string &, const SharedStreamMsg &)>;
    using KillReadyDispatcher =
        std::function<litebus::Future<::frontend_proxy::KillInstanceResponse>(
            const ::frontend_proxy::KillInstanceRequest &)>;
    using KillCleanupProbe =
        std::function<litebus::Future<FrontendKillCleanupSnapshot>(const std::string &, const std::string &)>;
    // WorkspaceStorageRoot 返回 workspace 存储根目录(默认 /home/agentos/.users)。
    // 单元测试可注入临时目录;生产环境从 --workspace_storage_root flag 读取。
    using WorkspaceStorageRootProvider = std::function<std::string()>;
    // WorkspaceUidGidProvider 返回 workspace 目录的 (uid, gid),默认 1000:1000。
    // 单元测试可注入自定义值;生产环境从 flag 读取。
    using WorkspaceUidGidProvider = std::function<std::pair<uid_t, gid_t>()>;
    // WorkspaceOwnerAuthorizer 校验调用租户(tenantID)是否有权操作指定 owner 的 workspace 目录。
    // 生产环境必须接线(结合 context.tenantID 与 owner 的绑定关系),
    // 与 invokeTenantAuthorizer 的 tenant 归属校验约定保持一致;
    // 为空时视为单租户部署,信任 frontend 侧已完成的 owner 派生。
    using WorkspaceOwnerAuthorizer = std::function<bool(const std::string &, const std::string &)>;

    std::string nodeID;
    std::string endpointAddress;
    // Optional test seam. Production leaves this empty and dispatches through InvocationHandler::Invoke.
    InvokeDispatcher invokeDispatcher;
    // Production resolves the target instance locally and requires exact tenant
    // ownership before dispatching a frontend-originated invoke.
    InvokeTenantAuthorizer invokeTenantAuthorizer;
    // Optional test seam. Production may wire this to the reviewed create handler only after
    // frontend system-caller create semantics are enabled end-to-end.
    CreateDispatcher createDispatcher;
    // Reviewed create seam: dispatcher must return the frontend-facing final/ready response,
    // not the old immediate POSIX scheduler CreateResponse.
    CreateReadyDispatcher createReadyDispatcher;
    // Removes only the frontend ready waiter. It must never be interpreted as
    // cancelling an instance whose schedule request may already be dispatched.
    CreateWaitCanceller createWaitCanceller;
    // Optional test seam. Production may wire this to the reviewed kill handler only after
    // frontend system-caller lifecycle semantics are enabled end-to-end.
    KillDispatcher killDispatcher;
    // Reviewed kill seam: dispatcher must return the frontend-facing response,
    // not the old POSIX stream KillResponse wrapper.
    KillReadyDispatcher killReadyDispatcher;
    // Read-only production probe. A successful KillResponse is not cleanup evidence.
    KillCleanupProbe killCleanupProbe;
    // Workspace 存储根目录 Provider;为空时使用默认值 /home/agentos/.users
    WorkspaceStorageRootProvider workspaceStorageRootProvider;
    // Workspace 目录的 (uid, gid) Provider;为空时使用默认值 (1000, 1000)
    WorkspaceUidGidProvider workspaceUidGidProvider;
    // Workspace owner 归属授权;为空时不做 owner↔tenant 绑定校验(单租户前提)。
    // 生产环境接线后,非本租户的 owner 操作返回 ERR_AUTHORIZE_FAILED。
    WorkspaceOwnerAuthorizer workspaceOwnerAuthorizer;
    bool enableCreateDispatch { false };
    bool enableKillDispatch { false };
    bool requireAuthenticatedPeer { false };
    uint64_t invokeResultTimeoutMs { 60000 };
    uint64_t invokeResultTimeoutBufferMs { 5000 };
    uint64_t killCleanupTimeoutMs { 2000 };
};

// FrontendProxyService is the same-port frontend entrypoint for faasfrontend.
// It is deliberately separate from PosixService: frontend service callers are
// system services and must not be registered as runtime stream clients.
class FrontendProxyService final : public frontend_proxy::FrontendProxyService::Service {
public:
    inline static constexpr const char *lifecycleTransport = "raw-unary";
    inline static constexpr const char *readyOperation = "ready";

    explicit FrontendProxyService(FrontendProxyServiceParam &&param);
    ~FrontendProxyService() override = default;

    ::grpc::Status InvokeInstance(::grpc::ServerContext *context,
                                  const ::frontend_proxy::InvokeInstanceRequest *request,
                                  ::frontend_proxy::InvokeInstanceResponse *response) override;

    ::grpc::Status InvokeInstanceStream(
        ::grpc::ServerContext *context,
        const ::frontend_proxy::InvokeInstanceRequest *request,
        ::grpc::ServerWriter<::frontend_proxy::InvokeInstanceStreamResponse> *writer) override;

    ::grpc::Status CreateInstance(::grpc::ServerContext *context,
                                  const ::frontend_proxy::CreateInstanceRequest *request,
                                  ::frontend_proxy::CreateInstanceResponse *response) override;

    ::grpc::Status KillInstance(::grpc::ServerContext *context,
                                const ::frontend_proxy::KillInstanceRequest *request,
                                ::frontend_proxy::KillInstanceResponse *response) override;

    // PrepareWorkspace 在归属节点本地创建 workspace 存储目录:
    //   mkdir -p ${workspace_storage_root}/<owner> + chown <uid>:<gid> + chmod <mode>
    // 幂等:目录已存在不报错,默认权限 0700,默认 uid:gid = 1000:1000。
    ::grpc::Status PrepareWorkspace(::grpc::ServerContext *context,
                                    const ::frontend_proxy::PrepareWorkspaceRequest *request,
                                    ::frontend_proxy::PrepareWorkspaceResponse *response) override;

    // RemoveWorkspace 在归属节点本地删除 workspace 存储目录:
    //   rm -rf ${workspace_storage_root}/<owner>
    // 幂等:目录不存在不报错。
    ::grpc::Status RemoveWorkspace(::grpc::ServerContext *context,
                                   const ::frontend_proxy::RemoveWorkspaceRequest *request,
                                   ::frontend_proxy::RemoveWorkspaceResponse *response) override;

private:
    bool ValidateInvokeRequest(const ::frontend_proxy::InvokeInstanceRequest &request,
                               ::frontend_proxy::InvokeInstanceResponse &response) const;
    ::grpc::Status DispatchInvokeRequest(::grpc::ServerContext *context,
                                         const ::frontend_proxy::InvokeInstanceRequest &request,
                                         ::frontend_proxy::InvokeInstanceResponse &response);
    ::grpc::Status AwaitInvokeResult(::grpc::ServerContext *context,
                                     const ::frontend_proxy::InvokeInstanceRequest &request,
                                     ::frontend_proxy::InvokeInstanceResponse &response,
                                     const litebus::Future<SharedStreamMsg> &resultFuture,
                                     const std::chrono::steady_clock::time_point &waitDeadline);
    static std::chrono::milliseconds ResolveInvokeResultTimeout(
        const ::frontend_proxy::InvokeInstanceRequest &request, const FrontendProxyServiceParam &param);
    bool ValidateCreateRequest(const ::frontend_proxy::CreateInstanceRequest &request,
                               ::frontend_proxy::CreateInstanceResponse &response) const;
    ::grpc::Status DispatchCreateRequest(::grpc::ServerContext *context,
                                         const ::frontend_proxy::CreateInstanceRequest &request,
                                         ::frontend_proxy::CreateInstanceResponse &response);
    ::grpc::Status FinalizeCreateResponse(const ::frontend_proxy::CreateInstanceRequest &request,
                                          ::frontend_proxy::CreateInstanceResponse &response);
    bool ValidateKillRequest(const ::frontend_proxy::KillInstanceRequest &request,
                             ::frontend_proxy::KillInstanceResponse &response) const;
    ::grpc::Status DispatchKillRequest(::grpc::ServerContext *context,
                                       const ::frontend_proxy::KillInstanceRequest &request,
                                       ::frontend_proxy::KillInstanceResponse &response);
    static bool HasRequiredContext(const ::frontend_proxy::FrontendRequestContext &context);
    static bool HasRequiredLifecycleContext(const ::frontend_proxy::FrontendRequestContext &context);
    bool HasAuthenticatedPeer(const ::grpc::ServerContext *context) const;
    static void SetStatus(::frontend_proxy::FrontendProxyStatus *status, common::ErrorCode code,
                          const std::string &message, bool retryable = false,
                          const std::string &retryReason = "");
    // ResolveWorkspaceStorageRoot 解析 workspace 存储根目录:
    // 优先使用注入的 provider,否则使用默认值 /home/agentos/.users。
    std::string ResolveWorkspaceStorageRoot() const;
    // ResolveWorkspaceUidGid 解析 workspace 目录的 (uid, gid):
    // 优先使用注入的 provider,否则使用默认值 (1000, 1000)。
    std::pair<uid_t, gid_t> ResolveWorkspaceUidGid() const;
    // ValidateWorkspaceOwner 校验 owner 名:非空、不含 "/"、非 ".."、不以 "." 开头。
    // 返回空字符串表示校验通过,否则返回错误描述。
    static std::string ValidateWorkspaceOwner(const std::string &owner);
    // ParseWorkspaceMode 解析权限位字符串(如 "0700"、"755"),失败返回 -1。
    static int ParseWorkspaceMode(const std::string &mode);
    // ValidateWorkspaceCall 组合校验 workspace 请求:
    //   请求身份(frontendClientID+requestID 必填)、租户标签一致性、owner 合法性。
    // 调用方契约(handler.go)不传 tenantID,故不强制 tenantid 非空。
    // 校验失败时写入 status 并返回 false,否则返回 true。
    bool ValidateWorkspaceCall(const ::frontend_proxy::FrontendRequestContext *context,
                               const std::string &owner,
                               ::frontend_proxy::FrontendProxyStatus *status) const;
    FrontendProxyServiceParam param_;
};

}  // namespace functionsystem::local_scheduler

#endif  // FUNCTION_PROXY_LOCAL_SCHEDULER_GRPC_SERVER_FRONTEND_PROXY_SERVICE_FRONTEND_PROXY_SERVICE_H
