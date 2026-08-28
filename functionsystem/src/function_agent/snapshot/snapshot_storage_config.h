/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_CONFIG_H
#define FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_CONFIG_H

#include <cstdint>
#include <functional>
#include <string>

#include "common/snapshot_storage/data_system_snapshot_storage.h"
#include "common/snapshot_storage/obs_snapshot_storage.h"
#include "common/status/status.h"
#include "common/utils/sensitive_value.h"
#include "function_agent/flags/function_agent_flags.h"

namespace functionsystem::function_agent {

enum class SnapshotStorageMode {
    DISTRIBUTED_CACHE,
    DISTRIBUTED_ONLY,
    LOCAL_ONLY,
};

Status ParseSnapshotStorageMode(const std::string &value, SnapshotStorageMode &mode);
bool UsesDistributedStorage(SnapshotStorageMode mode);
bool KeepsLocalSnapshot(SnapshotStorageMode mode);

struct SnapshotStorageStartConfig {
    bool enabled{ false };
    SnapshotStorageMode mode{ SnapshotStorageMode::DISTRIBUTED_CACHE };
    uint64_t localCacheMaxBytes{ 0 };
    std::string backend;
    snapshot_storage::ObsSnapshotConfig obs;
    snapshot_storage::DataSystemSnapshotConfig dataSystem;
};

using SnapshotCredentialDecryptor =
    std::function<litebus::Option<litebus::SensitiveValue>(const std::string &)>;

Status BuildSnapshotStorageStartConfig(
    const FunctionAgentFlags &flags,
    SnapshotStorageStartConfig &output,
    const SnapshotCredentialDecryptor &decrypt = {});

}  // namespace functionsystem::function_agent

#endif  // FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_CONFIG_H
