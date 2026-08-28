/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_STORAGE_H
#define FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_STORAGE_H

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "async/future.hpp"
#include "common/snapshot_storage/secure_directory.h"
#include "common/status/status.h"
#include "common/utils/actor_worker.h"

namespace functionsystem::snapshot_storage {

struct SnapshotObjectMetadata {
    std::string snapshotID;
    int64_t sourceInstanceVersion{ 0 };
    uint64_t size{ 0 };
    std::string sha256;
    bool complete{ false };
    int64_t expiresAtUnixSeconds{ 0 };
};

struct SnapshotStat {
    Status status;
    SnapshotObjectMetadata metadata;
};

struct SnapshotPublicationFile {
    Status status;
    std::string path;
    bool temporary{ false };
};

class SnapshotStorage {
public:
    virtual ~SnapshotStorage() = default;

    virtual litebus::Future<Status> PutTemporary(const std::string &temporaryKey, const std::string &sourceFile,
                                                  const SnapshotObjectMetadata &metadata) = 0;
    virtual litebus::Future<SnapshotStat> Stat(const std::string &key) = 0;
    virtual litebus::Future<Status> Publish(const std::string &temporaryKey, const std::string &finalKey,
                                            const SnapshotObjectMetadata &expected) = 0;
    virtual litebus::Future<Status> Get(const std::string &finalKey, const std::string &destinationFile) = 0;
    virtual litebus::Future<Status> Delete(const std::string &key) = 0;
};

// Resolve the configured logical backend from the concrete storage dependency.
// The fallback name exists for injected adapters whose concrete type is not a
// built-in backend; callers must not accept a runtime-reported backend instead.
Status ResolveStorageBackend(const std::shared_ptr<SnapshotStorage> &storage,
                             const std::string &configuredBackend,
                             std::string &resolvedBackend);

std::string StableTenantHash(const std::string &tenantID);

std::string BuildPauseSnapshotFinalKey(const std::string &tenantID, const std::string &instanceID);
std::string BuildPauseSnapshotTemporaryKey(const std::string &tenantID, const std::string &instanceID,
                                           const std::string &snapshotID);
std::string BuildPauseSnapshotKey(const std::string &tenantHash, const std::string &instanceID,
                                  const std::string &snapshotID);
std::string BuildPauseSnapshotTemporaryKey(const std::string &tenantHash, const std::string &instanceID,
                                           const std::string &snapshotID, const std::string &attemptID);
std::string BuildReusableSnapshotKey(const std::string &tenantHash, const std::string &snapshotID);
std::string BuildReusableSnapshotTemporaryKey(const std::string &tenantHash, const std::string &snapshotID,
                                              const std::string &attemptID);
litebus::Future<SnapshotStat> InspectLocalSnapshotFile(const std::shared_ptr<ActorWorker> &worker,
                                                       const std::string &sourceFile, const std::string &snapshotID,
                                                       int64_t sourceInstanceVersion);
litebus::Future<SnapshotPublicationFile> PrepareSnapshotPublicationFile(
    const std::shared_ptr<ActorWorker> &worker, const std::string &sourceFile,
    bool compress);

namespace detail {

litebus::Future<SnapshotStat> InspectLocalSnapshotFileWithHookForTest(
    const std::shared_ptr<ActorWorker> &worker, const std::string &sourceFile, const std::string &snapshotID,
    int64_t sourceInstanceVersion, std::function<void()> afterInitialStat);

class SecureDownloadTarget {
public:
    ~SecureDownloadTarget();

    Status Prepare(const std::string &destination);
    const std::string &StagingPath() const;
    Status Commit(const SnapshotObjectMetadata &metadata);
    void Cleanup();

private:
    SecureDirectory directory_;
    int stagingFd_{ -1 };
    std::string stagingPath_;
    std::string stagingName_;
    std::string destinationName_;
    bool pinnedDestination_{ false };
};

bool MetadataEqual(const SnapshotObjectMetadata &left, const SnapshotObjectMetadata &right);
bool MetadataIdentityEqual(const SnapshotObjectMetadata &left, const SnapshotObjectMetadata &right);
bool IsExpired(const SnapshotObjectMetadata &metadata);
Status ValidateFile(const std::string &path, const SnapshotObjectMetadata &metadata);
Status Conflict(const std::string &message);

template <typename T, typename F, typename Dispatch>
litebus::Future<T> RunOnWorkerWithDispatch(F &&operation, Dispatch &&dispatch)
{
    auto promise = std::make_shared<litebus::Promise<T>>();
    auto future = promise->GetFuture();
    auto completed = std::make_shared<std::atomic_bool>(false);
    auto complete = [promise, completed](T result) mutable {
        bool expected = false;
        if (completed->compare_exchange_strong(expected, true)) {
            promise->SetValue(std::move(result));
        }
    };
    auto fail = [complete](Status status) mutable {
        if constexpr (std::is_same_v<T, Status>) {
            complete(std::move(status));
        } else {
            T result;
            result.status = std::move(status);
            complete(std::move(result));
        }
    };
    auto work = [complete, fail, operation = std::forward<F>(operation)]() mutable {
        try {
            complete(operation());
        } catch (const std::exception &error) {
            fail(Status(StatusCode::FAILED, error.what()));
        }
    };
    try {
        auto dispatched = dispatch(std::move(work));
        (void)dispatched.OnComplete([fail](const litebus::Future<Status> &result) mutable {
            if (result.IsError()) {
                fail(Status(StatusCode::FAILED,
                            "snapshot worker dispatch failed: " + std::to_string(result.GetErrorCode())));
                return;
            }
            if (result.Get().IsError()) {
                fail(result.Get());
            }
        });
    } catch (const std::exception &error) {
        fail(Status(StatusCode::FAILED, error.what()));
    }
    return future;
}

template <typename T, typename F>
litebus::Future<T> RunOnWorker(const std::shared_ptr<ActorWorker> &worker, F &&operation)
{
    return RunOnWorkerWithDispatch<T>(
        std::forward<F>(operation),
        [worker](std::function<void()> handler) { return worker->AsyncWork(std::move(handler)); });
}

}  // namespace detail
}  // namespace functionsystem::snapshot_storage

#endif  // FUNCTIONSYSTEM_SRC_COMMON_SNAPSHOT_STORAGE_SNAPSHOT_STORAGE_H
