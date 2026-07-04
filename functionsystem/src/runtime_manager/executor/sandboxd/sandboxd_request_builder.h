/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_REQUEST_BUILDER_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_REQUEST_BUILDER_H

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/sandbox_api.pb.h"
#include "common/status/status.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/config/command_builder.h"

namespace functionsystem::runtime_manager {

/**
 * SandboxdStartParams — all inputs required to build a flat SandboxService
 * SandboxStartRequest. Mirrors SandboxStartParams but drops checkpoint/restore
 * inputs (restore is not implemented against the sandboxd SandboxService API).
 */
struct SandboxdStartParams {
    // Source request from the upper layer (never mutated by builder)
    std::shared_ptr<messages::StartInstanceRequest> request;

    // Resolved command arguments from CommandBuilder
    CommandArgs cmdArgs;

    // Combined posix/user/framework envs (builder calls CombineEnvs internally)
    Envs envs;

    // Runtime ID used as sandbox_id and for log path construction
    std::string runtimeID;

    // Allocated port numbers for port-forwarding (may be empty)
    std::vector<std::string> portMappings;

    // Template IDs known to be registered in sandboxd. This is a per-build
    // snapshot so the request builder stays stateless and lifetime-safe.
    std::unordered_set<std::string> registeredTemplateIDs;
};

/**
 * SandboxdRequestBuilder — stateless construction of the flat sandboxd
 * SandboxStartRequest.
 *
 * It is the sandboxd counterpart of SandboxRequestBuilder: same rootfs/mount/
 * env/resource/port resolution, but writes the flat SandboxService fields
 * (sandbox_id/runtime/rootfs/command/envs/mounts/...) instead of the legacy
 * FunctionRuntime-bearing StartRequest.
 *
 * Does NOT allocate ports, call any gRPC stub, or mutate its inputs.
 */
class SandboxdRequestBuilder {
public:
    explicit SandboxdRequestBuilder(const CommandBuilder &cmdBuilder);

    std::pair<Status, std::shared_ptr<runtime::v1::SandboxStartRequest>> Build(
        const SandboxdStartParams &params) const;

    // Adds the bootstrap working-root mount and returns the working-root path
    // that should be set in YR_RT_WORKING_DIR. Shared with the sandbox executor.
    void ApplyBootstrapMount(const std::shared_ptr<messages::StartInstanceRequest> &request,
                             google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                             std::string &workingRoot) const;

private:
    std::pair<Status, std::shared_ptr<runtime::v1::SandboxStartRequest>> BuildStart(
        const SandboxdStartParams &params) const;

    Status BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                       runtime::v1::SandboxStartRequest &start) const;

    // ── Shared helpers (mirror sandbox_request_builder) ──────────────────────

    Envs ApplyCodeMounts(const std::shared_ptr<messages::StartInstanceRequest> &request,
                         google::protobuf::RepeatedPtrField<runtime::v1::Mount> *mounts,
                         const Envs &envs) const;

    void ApplyCommands(const std::shared_ptr<messages::StartInstanceRequest> &request,
                       const CommandArgs &cmdArgs, runtime::v1::SandboxStartRequest *start) const;

    void ApplyResources(const std::shared_ptr<messages::StartInstanceRequest> &request,
                        google::protobuf::Map<std::string, double> *resources) const;

    void ApplyEnvsAndLogs(const Envs &envs, const std::string &runtimeID,
                          runtime::v1::SandboxStartRequest *start) const;

    void ApplyExtraConfig(const std::shared_ptr<messages::StartInstanceRequest> &request,
                          runtime::v1::SandboxStartRequest *start) const;

    void ApplyPortMappings(const std::vector<std::string> &portMappings,
                           google::protobuf::RepeatedPtrField<std::string> *ports) const;

    const CommandBuilder &cmdBuilder_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_REQUEST_BUILDER_H
