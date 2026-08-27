/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CHECKPOINT_PLAN_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CHECKPOINT_PLAN_H

#include <filesystem>
#include <string>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"

namespace functionsystem::runtime_manager {

enum class ArtifactLifecycle {
    USER_MANAGED,
    INSTANCE_MANAGED,
};

struct CheckpointPlan {
    std::string sandboxID;
    std::string checkpointID;
    std::string checkpointDirectory;
    int32_t ttlSeconds{ 0 };
    uint32_t timeoutSeconds{ 180 };
    ArtifactLifecycle lifecycle{ ArtifactLifecycle::USER_MANAGED };
    bool compress{ true };
    bool leaveRuntimeRunning{ false };
};

struct CheckpointResult {
    Status status;
    int64_t size{ 0 };
    std::string sha256;
};

inline bool IsSafeCheckpointIdentityComponent(const std::string &component)
{
    return !component.empty() && component != "." && component != ".."
        && component.find('/') == std::string::npos
        && component.find('\\') == std::string::npos
        && component.find('\0') == std::string::npos;
}

inline Status BuildCheckpointPlan(const messages::SnapshotRuntimeRequest &request,
                                  const std::string &sandboxID,
                                  ArtifactLifecycle lifecycle,
                                  bool leaveRuntimeRunning,
                                  CheckpointPlan &plan)
{
    if (sandboxID.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "checkpoint sandbox identity is empty");
    }
    if (!IsSafeCheckpointIdentityComponent(request.snapshotid())) {
        return Status(StatusCode::PARAMETER_ERROR, "checkpoint identity is invalid");
    }
    if (request.checkpointdir().empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "checkpoint directory must be resolved before execution");
    }
    const auto directory = std::filesystem::path(request.checkpointdir()).lexically_normal();
    if (!directory.is_absolute()) {
        return Status(StatusCode::PARAMETER_ERROR, "checkpoint directory must be absolute");
    }

    plan.sandboxID = sandboxID;
    plan.checkpointID = request.snapshotid();
    plan.checkpointDirectory = directory.string();
    plan.ttlSeconds = request.ttl();
    plan.lifecycle = lifecycle;
    plan.leaveRuntimeRunning = leaveRuntimeRunning;
    return Status::OK();
}

}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_CHECKPOINT_PLAN_H
