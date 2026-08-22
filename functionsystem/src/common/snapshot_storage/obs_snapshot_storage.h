/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_OBS_SNAPSHOT_STORAGE_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_OBS_SNAPSHOT_STORAGE_H

#include "common/snapshot_storage/snapshot_storage.h"

namespace functionsystem::snapshot_storage {

struct ObsSnapshotConfig {
    std::string endpoint;
    std::string bucket;
    std::string accessKey;
    std::string secretKey;
    std::string securityToken;
    bool useHttps{ true };
    bool pathStyle{ false };
};

struct ObsHeadResult {
    Status status;
    SnapshotObjectMetadata metadata;
    std::string etag;
};

class ObsSnapshotClient {
public:
    virtual ~ObsSnapshotClient() = default;
    virtual Status MultipartUpload(const std::string &key, const std::string &sourceFile,
                                   const SnapshotObjectMetadata &metadata) = 0;
    virtual ObsHeadResult Head(const std::string &key) = 0;
    virtual Status ConditionalCopy(const std::string &temporaryKey, const std::string &finalKey,
                                   const std::string &expectedTemporaryETag,
                                   const SnapshotObjectMetadata &metadata) = 0;
    virtual Status Download(const std::string &key, const std::string &destinationFile) = 0;
    virtual Status Delete(const std::string &key) = 0;
};

class HuaweiObsSnapshotClient final : public ObsSnapshotClient {
public:
    explicit HuaweiObsSnapshotClient(ObsSnapshotConfig config);

    Status MultipartUpload(const std::string &key, const std::string &sourceFile,
                           const SnapshotObjectMetadata &metadata) override;
    ObsHeadResult Head(const std::string &key) override;
    Status ConditionalCopy(const std::string &temporaryKey, const std::string &finalKey,
                           const std::string &expectedTemporaryETag,
                           const SnapshotObjectMetadata &metadata) override;
    Status Download(const std::string &key, const std::string &destinationFile) override;
    Status Delete(const std::string &key) override;

private:
    ObsSnapshotConfig config_;
};

class ObsSnapshotStorage final : public SnapshotStorage {
public:
    explicit ObsSnapshotStorage(const ObsSnapshotConfig &config, std::shared_ptr<ActorWorker> worker = nullptr);
    explicit ObsSnapshotStorage(
        std::shared_ptr<ObsSnapshotClient> client, std::shared_ptr<ActorWorker> worker = nullptr);

    litebus::Future<Status> PutTemporary(const std::string &temporaryKey, const std::string &sourceFile,
                                          const SnapshotObjectMetadata &metadata) override;
    litebus::Future<SnapshotStat> Stat(const std::string &key) override;
    litebus::Future<Status> Publish(const std::string &temporaryKey, const std::string &finalKey,
                                    const SnapshotObjectMetadata &expected) override;
    litebus::Future<Status> Get(const std::string &finalKey, const std::string &destinationFile) override;
    litebus::Future<Status> Delete(const std::string &key) override;

private:
    std::shared_ptr<ObsSnapshotClient> client_;
    std::shared_ptr<ActorWorker> worker_;
};

}  // namespace functionsystem::snapshot_storage

#endif  // FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_OBS_SNAPSHOT_STORAGE_H
