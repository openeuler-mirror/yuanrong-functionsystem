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

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_REQUEST_BUILDER_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_REQUEST_BUILDER_H

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.pb.h"
#include "common/status/status.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/config/command_builder.h"

namespace functionsystem::runtime_manager {

/**
 * SandboxStartParams — all inputs required to build a gRPC Start/Restore request.
 *
 * Callers fill this struct and pass it to SandboxRequestBuilder. No fields are
 * mutated after construction; the struct is immutable from the builder's POV.
 */
struct SandboxStartParams {
    // Source request from the upper layer (never mutated by builder)
    std::shared_ptr<messages::StartInstanceRequest> request;

    // Resolved command arguments from CommandBuilder (exec path, args, workingDir,
    // deployOptionOverrides)
    CommandArgs cmdArgs;

    // Combined posix/user/framework envs (pre-CombineEnvs call not yet done here;
    // the builder will call CombineEnvs internally)
    Envs envs;

    // Runtime ID used for log path construction
    std::string runtimeID;

    // When non-empty: build a RestoreRequest instead of a StartRequest
    std::string checkpointID;

    // Allocated port numbers for port-forwarding (may be empty)
    std::vector<std::string> portMappings;
};

/**
 * SandboxRequestBuilder — stateless proto request construction.
 *
 * Responsibilities (only):
 *   - Build a StartRequest or RestoreRequest from SandboxStartParams.
 *   - Apply CommandArgs.deployOptionOverrides to proto deployOptions.
 *   - Populate mounts, resources, envs, log paths, extra config.
 *
 * Does NOT:
 *   - Allocate ports (caller's responsibility).
 *   - Call any gRPC stub.
 *   - Modify the SandboxStartParams or the embedded StartInstanceRequest.
 */
class SandboxRequestBuilder {
public:
    explicit SandboxRequestBuilder(const CommandBuilder &cmdBuilder);

    // Returns StartRequest or RestoreRequest depending on params.checkpointID.
    using SandboxProtoRequest = std::variant<std::shared_ptr<runtime::v1::StartRequest>,
                                             std::shared_ptr<runtime::v1::RestoreRequest>>;

    std::pair<Status, SandboxProtoRequest> Build(const SandboxStartParams &params) const;

    // Convenience accessors for each variant type
    static std::shared_ptr<runtime::v1::StartRequest> AsStart(const SandboxProtoRequest &req);
    static std::shared_ptr<runtime::v1::RestoreRequest> AsRestore(const SandboxProtoRequest &req);

    // Exposed for the warmup path: adds the bootstrap working-root mount and
    // returns the working-root path that should be set in YR_RT_WORKING_DIR.
    void ApplyBootstrapMount(const std::shared_ptr<messages::StartInstanceRequest> &request,
                             google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                             std::string &workingRoot) const;

private:
    // ── Start path ────────────────────────────────────────────────────────────

    std::pair<Status, std::shared_ptr<runtime::v1::StartRequest>> BuildStart(
        const SandboxStartParams &params) const;

    Status BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                       runtime::v1::StartRequest &start) const;

    // ── Restore path ──────────────────────────────────────────────────────────

    std::pair<Status, std::shared_ptr<runtime::v1::RestoreRequest>> BuildRestore(
        const SandboxStartParams &params) const;

    // ── Shared between Start and Restore ─────────────────────────────────────

    // Appends mounts for the code layer and updates envs accordingly.
    // Returns updated envs (does not mutate params.envs).
    Envs ApplyCodeMounts(const std::shared_ptr<messages::StartInstanceRequest> &request,
                         google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                         const Envs &envs) const;

    void ApplyCommands(const std::shared_ptr<messages::StartInstanceRequest> &request,
                       const CommandArgs &cmdArgs, runtime::v1::FunctionRuntime *funcRt) const;

    void ApplyResources(const std::shared_ptr<messages::StartInstanceRequest> &request,
                        google::protobuf::Map<std::string, double> *resources) const;

    template <typename ProtoReq>
    void ApplyEnvsAndLogs(const Envs &envs, const std::string &runtimeID, ProtoReq *req) const;

    template <typename ProtoReq>
    void ApplyExtraConfig(const std::shared_ptr<messages::StartInstanceRequest> &request,
                          ProtoReq *req) const;

    void ApplyPortMappings(const std::vector<std::string> &portMappings,
                           google::protobuf::RepeatedPtrField<std::string> *ports) const;

    const CommandBuilder &cmdBuilder_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_REQUEST_BUILDER_H
