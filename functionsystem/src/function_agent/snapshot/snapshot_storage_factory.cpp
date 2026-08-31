/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "function_agent/snapshot/snapshot_storage_factory.h"

#include <utility>

namespace functionsystem::function_agent {
namespace {

Status ValidateCreatedStorage(const Status &status, std::shared_ptr<snapshot_storage::SnapshotStorage> &output)
{
    if (status.IsError()) {
        output.reset();
        return status;
    }
    if (output == nullptr) {
        return Status(StatusCode::FAILED, "snapshot storage creator returned null");
    }
    return Status::OK();
}

}  // namespace

Status CreateSnapshotStorage(const SnapshotStorageStartConfig &config,
                             std::shared_ptr<snapshot_storage::SnapshotStorage> &output,
                             const std::shared_ptr<snapshot_storage::DataSystemSnapshotClient> &dataSystemClient)
{
    output.reset();
    if (!config.enabled || !UsesDistributedStorage(config.mode)) {
        return Status::OK();
    }

    if (config.backend == "obs") {
        if (config.obs.endpoint.empty() || config.obs.bucket.empty() || config.obs.accessKey.empty()
            || config.obs.secretKey.empty()) {
            return Status(StatusCode::ERR_PARAM_INVALID, "invalid OBS snapshot storage config");
        }
        output = std::make_shared<snapshot_storage::ObsSnapshotStorage>(config.obs);
        return ValidateCreatedStorage(Status::OK(), output);
    }

    if (config.backend == "datasystem") {
        constexpr int32_t MAX_PORT = 65535;
        if (config.dataSystem.host.empty() || config.dataSystem.port <= 0 || config.dataSystem.port > MAX_PORT) {
            return Status(StatusCode::ERR_PARAM_INVALID, "invalid DataSystem snapshot storage config");
        }
        std::shared_ptr<snapshot_storage::DataSystemSnapshotStorage> storage;
        auto status = snapshot_storage::DataSystemSnapshotStorage::Create(
            config.dataSystem, storage, dataSystemClient);
        output = std::move(storage);
        return ValidateCreatedStorage(status, output);
    }

    return Status(StatusCode::ERR_PARAM_INVALID, "unknown snapshot storage backend");
}

}  // namespace functionsystem::function_agent
