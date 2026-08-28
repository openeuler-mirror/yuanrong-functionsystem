/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0.
 */

#include "sandboxd_checkpoint_orchestrator.h"

#include "common/logs/logging.h"
#include "common/status/status.h"

namespace functionsystem::runtime_manager {

SandboxdCheckpointOrchestrator::SandboxdCheckpointOrchestrator(
    std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd)
    : sandboxd_(std::move(sandboxd))
{
}

litebus::Future<CheckpointResult> SandboxdCheckpointOrchestrator::DoCheckpoint(
    const std::shared_ptr<runtime::v1::CheckpointRequest> &request)
{
    ASSERT_IF_NULL(sandboxd_);
    auto response = std::make_shared<runtime::v1::CheckpointResponse>();
    return sandboxd_->CallAsyncX(
        "Checkpoint", *request, response.get(),
        &runtime::v1::SandboxService::Stub::AsyncCheckpoint)
        .Then([request, response](const Status &status) -> CheckpointResult {
            if (status.IsError()) {
                YRLOG_ERROR("checkpoint gRPC failed for sandbox({}): {}",
                            request->id(), status.RawMessage());
                return {status};
            }
            return {Status::OK()};
        });
}

litebus::Future<CheckpointResult> SandboxdCheckpointOrchestrator::CheckpointLocal(
    const CheckpointPlan &plan)
{
    auto request = std::make_shared<runtime::v1::CheckpointRequest>();
    request->set_id(plan.sandboxID);
    request->set_checkpoint_dir(plan.checkpointDirectory);
    request->set_timeout_seconds(plan.timeoutSeconds);
    request->set_compress(plan.compress);
    request->set_leave_running(plan.leaveRuntimeRunning);
    return DoCheckpoint(request);
}

}  // namespace functionsystem::runtime_manager
