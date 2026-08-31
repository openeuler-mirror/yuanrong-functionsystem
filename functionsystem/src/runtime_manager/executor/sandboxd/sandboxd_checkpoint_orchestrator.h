/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H

#include <memory>

#include "common/proto/pb/posix/sandbox_api.grpc.pb.h"
#include "common/rpc/client/grpc_client.h"
#include "runtime_manager/ckpt/checkpoint_plan.h"

namespace functionsystem::runtime_manager {

// RuntimeManager only translates a local checkpoint plan into sandboxd's
// Checkpoint RPC. Publication, caching and cleanup policy belong to
// FunctionAgent and FunctionProxy.
class SandboxdCheckpointOrchestrator {
public:
    explicit SandboxdCheckpointOrchestrator(
        std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd);

    litebus::Future<CheckpointResult> DoCheckpoint(
        const std::shared_ptr<runtime::v1::CheckpointRequest> &request);
    litebus::Future<CheckpointResult> CheckpointLocal(const CheckpointPlan &plan);

private:
    std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_CHECKPOINT_ORCHESTRATOR_H
