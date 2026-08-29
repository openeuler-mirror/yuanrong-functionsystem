/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_ARTIFACT_PUBLISHER_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_ARTIFACT_PUBLISHER_H

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include "common/snapshot_storage/snapshot_storage.h"
#include "common/logs/logging.h"

namespace functionsystem::snapshot_storage {

struct ArtifactPublishRequest {
    std::string sourceFile;
    std::string temporaryKey;
    std::string finalKey;
    std::string snapshotID;
    int64_t sourceInstanceVersion{ 0 };
    int64_t createdAtUnixSeconds{ 0 };
    int32_t ttlSeconds{ 0 };
    bool compress{ false };
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
        operation->startedAt = Clock::now();
        operation->prepareStartedAt = operation->startedAt;
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
        PrepareSnapshotPublicationFile(worker_, request.sourceFile, request.compress)
            .OnComplete([self = shared_from_this(), operation](
                            const litebus::Future<SnapshotPublicationFile> &prepared) {
                self->OnPrepared(operation, prepared);
            });
        return future;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct Operation {
        ArtifactPublishRequest request;
        SnapshotObjectMetadata metadata;
        std::shared_ptr<litebus::Promise<ArtifactPublishResult>> promise;
        std::atomic_bool completed{ false };
        std::string publicationFile;
        bool publicationFileTemporary{ false };
        Clock::time_point startedAt;
        Clock::time_point prepareStartedAt;
        Clock::time_point putStartedAt;
    };

    static int64_t ElapsedMs(Clock::time_point started)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    }

    void Complete(const std::shared_ptr<Operation> &operation, ArtifactPublishResult result)
    {
        bool expected = false;
        if (operation->completed.compare_exchange_strong(expected, true)) {
            if (operation->publicationFileTemporary && !operation->publicationFile.empty()) {
                (void)std::remove(operation->publicationFile.c_str());
            }
            operation->promise->SetValue(std::move(result));
        }
    }

    void OnPrepared(const std::shared_ptr<Operation> &operation,
                    const litebus::Future<SnapshotPublicationFile> &prepared)
    {
        if (prepared.IsError()) {
            Complete(operation, { Status(StatusCode::ERR_INNER_COMMUNICATION,
                                         "snapshot compression future failed"), {}, true });
            return;
        }
        if (prepared.Get().status.IsError()) {
            Complete(operation, { prepared.Get().status, {}, false });
            return;
        }
        operation->publicationFile = prepared.Get().path;
        operation->publicationFileTemporary = prepared.Get().temporary;
        YRLOG_INFO("{}|snapshot publication prepared, checkpoint.compress_ms: {}, "
                   "checkpoint.published_bytes: {}, compressed: {}",
                   operation->request.snapshotID, ElapsedMs(operation->prepareStartedAt),
                   prepared.Get().size, operation->request.compress);
        if (prepared.Get().metadataReady) {
            operation->metadata = { operation->request.snapshotID,
                                    operation->request.sourceInstanceVersion,
                                    prepared.Get().size,
                                    prepared.Get().sha256,
                                    false,
                                    0 };
            BeginPut(operation);
            return;
        }
        InspectLocalSnapshotFile(worker_, operation->publicationFile,
                                 operation->request.snapshotID,
                                 operation->request.sourceInstanceVersion)
            .OnComplete([self = shared_from_this(), operation](
                            const litebus::Future<SnapshotStat> &inspection) {
                self->OnInspected(operation, inspection);
            });
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
        BeginPut(operation);
    }

    void BeginPut(const std::shared_ptr<Operation> &operation)
    {
        operation->metadata.complete = false;
        operation->metadata.expiresAtUnixSeconds = operation->request.ttlSeconds == 0
            ? 0
            : operation->request.createdAtUnixSeconds + operation->request.ttlSeconds;
        if (storage_->SupportsDirectFinalPut()) {
            operation->metadata.complete = true;
            operation->putStartedAt = Clock::now();
            storage_->PutFinal(operation->request.finalKey, operation->publicationFile,
                               operation->metadata)
                .OnComplete([self = shared_from_this(), operation](const litebus::Future<Status> &put) {
                    self->OnPublished(operation, put);
                });
            return;
        }
        operation->putStartedAt = Clock::now();
        storage_->PutTemporary(operation->request.temporaryKey, operation->publicationFile,
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
        YRLOG_INFO("{}|snapshot publication completed, checkpoint.remote_put_ms: {}, "
                   "checkpoint.total_ms: {}, direct_final: {}, success: {}",
                   operation->request.snapshotID, ElapsedMs(operation->putStartedAt),
                   ElapsedMs(operation->startedAt), storage_->SupportsDirectFinalPut(),
                   !publish.IsError() && publish.Get().IsOk());
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
