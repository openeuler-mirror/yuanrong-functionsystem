/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_DATA_SYSTEM_SNAPSHOT_STORAGE_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_DATA_SYSTEM_SNAPSHOT_STORAGE_H

#include "common/snapshot_storage/snapshot_storage.h"
#include "datasystem/utils/status.h"

namespace functionsystem::snapshot_storage {

namespace detail {
Status MapDataSystemStatus(const datasystem::Status &status);
std::string DataSystemObjectKey(const std::string &logicalKey);
}

struct DataSystemSnapshotConfig {
    std::string host;
    int32_t port{ 0 };
};

struct DataSystemGetResult {
    Status status;
    std::string value;
};

class DataSystemSnapshotClient {
public:
    virtual ~DataSystemSnapshotClient() = default;
    virtual Status Init(const DataSystemSnapshotConfig &config) = 0;
    virtual Status Put(const std::string &key, const std::string &value) = 0;
    virtual DataSystemGetResult Get(const std::string &key) = 0;
    virtual Status Delete(const std::string &key) = 0;
};

class DataSystemSnapshotStorage final : public SnapshotStorage {
public:
    DataSystemSnapshotStorage();
    explicit DataSystemSnapshotStorage(std::shared_ptr<DataSystemSnapshotClient> client,
                                       std::shared_ptr<ActorWorker> worker = nullptr);

    static Status Create(const DataSystemSnapshotConfig &config,
                         std::shared_ptr<DataSystemSnapshotStorage> &storage,
                         std::shared_ptr<DataSystemSnapshotClient> client = nullptr);

    litebus::Future<Status> PutTemporary(const std::string &temporaryKey, const std::string &sourceFile,
                                          const SnapshotObjectMetadata &metadata) override;
    litebus::Future<SnapshotStat> Stat(const std::string &key) override;
    litebus::Future<Status> Publish(const std::string &temporaryKey, const std::string &finalKey,
                                    const SnapshotObjectMetadata &expected) override;
    litebus::Future<Status> Get(const std::string &finalKey, const std::string &destinationFile) override;
    litebus::Future<Status> Delete(const std::string &key) override;

private:
    std::shared_ptr<DataSystemSnapshotClient> client_;
    std::shared_ptr<ActorWorker> worker_;
};

}  // namespace functionsystem::snapshot_storage

#endif  // FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_DATA_SYSTEM_SNAPSHOT_STORAGE_H
