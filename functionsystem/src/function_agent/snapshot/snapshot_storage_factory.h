/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_FACTORY_H
#define FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_FACTORY_H

#include <memory>

#include "function_agent/snapshot/snapshot_storage_config.h"

namespace functionsystem::function_agent {

Status CreateSnapshotStorage(const SnapshotStorageStartConfig &config,
                             std::shared_ptr<snapshot_storage::SnapshotStorage> &output,
                             const std::shared_ptr<snapshot_storage::DataSystemSnapshotClient> &dataSystemClient =
                                 nullptr);

}  // namespace functionsystem::function_agent

#endif  // FUNCTIONSYSTEM_FUNCTION_AGENT_SNAPSHOT_STORAGE_FACTORY_H
