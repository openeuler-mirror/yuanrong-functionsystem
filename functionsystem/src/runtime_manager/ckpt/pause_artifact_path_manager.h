/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_PAUSE_ARTIFACT_PATH_MANAGER_H
#define FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_PAUSE_ARTIFACT_PATH_MANAGER_H

#include <filesystem>
#include <memory>
#include <string>

#include "async/future.hpp"
#include "common/status/status.h"
#include "common/utils/actor_worker.h"

namespace functionsystem::runtime_manager {

struct PauseArtifactPath {
    Status status;
    std::filesystem::path path;
};

class PauseArtifactPathManager {
public:
    PauseArtifactPathManager(std::filesystem::path checkpointRoot, std::string tenantHash,
                             std::string instanceID, std::shared_ptr<ActorWorker> worker = nullptr);

    static std::string StableTenantHash(const std::string &tenantID);

    PauseArtifactPath PlanSourceArtifact(const std::string &snapshotID) const;
    PauseArtifactPath PlanRestoreAttempt(const std::string &snapshotID,
                                         const std::string &targetAttemptID) const;

    litebus::Future<Status> DeleteRestoreAttempt(const std::string &snapshotID,
                                                 const std::string &targetAttemptID);
    litebus::Future<Status> PruneSourceArtifactParents(const std::string &snapshotID);

private:
    std::filesystem::path checkpointRoot_;
    std::string tenantHash_;
    std::string instanceID_;
    std::shared_ptr<ActorWorker> worker_;
};

}  // namespace functionsystem::runtime_manager

#endif  // FUNCTIONSYSTEM_SRC_RUNTIME_MANAGER_CKPT_PAUSE_ARTIFACT_PATH_MANAGER_H
