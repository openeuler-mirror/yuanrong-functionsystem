/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_ARTIFACT_PUBLISHER_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_ARTIFACT_PUBLISHER_H

#include <atomic>
#include <memory>
#include <string>

#include "common/snapshot_storage/snapshot_storage.h"

namespace functionsystem::snapshot_storage {

struct ArtifactPublishRequest {
    std::string sourceFile;
    std::string temporaryKey;
    std::string finalKey;
    std::string snapshotID;
    int64_t sourceInstanceVersion{ 0 };
    int64_t createdAtUnixSeconds{ 0 };
    int32_t ttlSeconds{ 0 };
};

struct ArtifactPublishResult {
    Status status;
    SnapshotObjectMetadata metadata;
    bool resultUnknown{ false };
};

class SnapshotArtifactPublisher : public std::enable_shared_from_this<SnapshotArtifactPublisher> {
public:
    SnapshotArtifactPublisher(std::shared_ptr<SnapshotStorage> storage,
                              std::shared_ptr<ActorWorker> worker)
        : storage_(std::move(storage)), worker_(std::move(worker))
    {
    }

    litebus::Future<ArtifactPublishResult> Publish(const ArtifactPublishRequest &request)
    {
        auto operation = std::make_shared<Operation>();
        operation->request = request;
        operation->promise = std::make_shared<litebus::Promise<ArtifactPublishResult>>();
        auto future = operation->promise->GetFuture();
        if (storage_ == nullptr || worker_ == nullptr || request.sourceFile.empty()
            || request.temporaryKey.empty() || request.finalKey.empty() || request.snapshotID.empty()
            || request.ttlSeconds < 0) {
            Complete(operation, { Status(StatusCode::ERR_PARAM_INVALID,
                                         "snapshot artifact publish request is incomplete"),
                                  {}, false });
            return future;
        }
        InspectLocalSnapshotFile(worker_, request.sourceFile, request.snapshotID,
                                 request.sourceInstanceVersion)
            .OnComplete([self = shared_from_this(), operation](const litebus::Future<SnapshotStat> &inspection) {
                self->OnInspected(operation, inspection);
            });
        return future;
    }

private:
    struct Operation {
        ArtifactPublishRequest request;
        SnapshotObjectMetadata metadata;
        std::shared_ptr<litebus::Promise<ArtifactPublishResult>> promise;
        std::atomic_bool completed{ false };
    };

    void Complete(const std::shared_ptr<Operation> &operation, ArtifactPublishResult result)
    {
        bool expected = false;
        if (operation->completed.compare_exchange_strong(expected, true)) {
            operation->promise->SetValue(std::move(result));
        }
    }

    void OnInspected(const std::shared_ptr<Operation> &operation,
                     const litebus::Future<SnapshotStat> &inspection)
    {
        if (inspection.IsError()) {
            Complete(operation, { Status(StatusCode::ERR_INNER_COMMUNICATION,
                                         "snapshot artifact inspection future failed"),
                                  {}, true });
            return;
        }
        if (inspection.Get().status.IsError()) {
            Complete(operation, { inspection.Get().status, {}, false });
            return;
        }
        operation->metadata = inspection.Get().metadata;
        operation->metadata.complete = false;
        operation->metadata.expiresAtUnixSeconds = operation->request.ttlSeconds == 0
            ? 0
            : operation->request.createdAtUnixSeconds + operation->request.ttlSeconds;
        storage_->PutTemporary(operation->request.temporaryKey, operation->request.sourceFile,
                               operation->metadata)
            .OnComplete([self = shared_from_this(), operation](const litebus::Future<Status> &put) {
                self->OnTemporaryPut(operation, put);
            });
    }

    void OnTemporaryPut(const std::shared_ptr<Operation> &operation,
                        const litebus::Future<Status> &put)
    {
        if (put.IsError() || put.Get().IsError()) {
            const auto failure = put.IsError()
                ? Status(StatusCode::ERR_INNER_COMMUNICATION,
                         "snapshot temporary upload future failed")
                : put.Get();
            ReconcileFinal(operation, failure, put.IsError());
            return;
        }
        operation->metadata.complete = true;
        storage_->Publish(operation->request.temporaryKey, operation->request.finalKey,
                          operation->metadata)
            .OnComplete([self = shared_from_this(), operation](const litebus::Future<Status> &publish) {
                self->OnPublished(operation, publish);
            });
    }

    void OnPublished(const std::shared_ptr<Operation> &operation,
                     const litebus::Future<Status> &publish)
    {
        if (!publish.IsError() && publish.Get().IsOk()) {
            Complete(operation, { Status::OK(), operation->metadata, false });
            return;
        }
        const auto failure = publish.IsError()
            ? Status(StatusCode::ERR_INNER_COMMUNICATION, "snapshot publish future failed")
            : publish.Get();
        ReconcileFinal(operation, failure, publish.IsError());
    }

    void ReconcileFinal(const std::shared_ptr<Operation> &operation,
                        Status failure, bool resultUnknown)
    {
        operation->metadata.complete = true;
        storage_->Stat(operation->request.finalKey)
            .OnComplete([self = shared_from_this(), operation, failure = std::move(failure), resultUnknown](
                            const litebus::Future<SnapshotStat> &stat) mutable {
                if (stat.IsError()) {
                    self->Complete(operation, { Status(StatusCode::ERR_INNER_COMMUNICATION,
                                                      "snapshot final Stat future failed"),
                                                operation->metadata, true });
                    return;
                }
                if (stat.Get().status.IsOk()) {
                    if (detail::MetadataEqual(stat.Get().metadata, operation->metadata)) {
                        self->Complete(operation, { Status::OK(), stat.Get().metadata, false });
                    } else {
                        self->Complete(operation, { Status(StatusCode::SCHEDULE_CONFLICTED,
                                                                  "immutable snapshot metadata conflicts"),
                                                    stat.Get().metadata, false });
                    }
                    return;
                }
                if (stat.Get().status.StatusCode() == StatusCode::FILE_NOT_FOUND) {
                    self->Complete(operation, { std::move(failure), operation->metadata,
                                                resultUnknown });
                    return;
                }
                self->Complete(operation, { stat.Get().status, operation->metadata, true });
            });
    }

    std::shared_ptr<SnapshotStorage> storage_;
    std::shared_ptr<ActorWorker> worker_;
};

}  // namespace functionsystem::snapshot_storage

#endif  // FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_ARTIFACT_PUBLISHER_H
