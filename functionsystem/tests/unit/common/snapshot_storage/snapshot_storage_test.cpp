/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "common/snapshot_storage/data_system_snapshot_storage.h"
#include "common/snapshot_storage/obs_snapshot_storage.h"
#include "common/snapshot_storage/snapshot_artifact_publisher.h"
#include "datasystem/utils/status.h"

namespace {
std::function<void()> gBeforeSnapshotRename;
}

namespace snapshot_storage_test_hooks {
std::mutex gCloseHookMutex;
std::string gClosePathMarker;
int gCloseTrigger{ 0 };
int gMatchingCloses{ 0 };
std::function<void(const std::string &)> gAfterMatchingClose;

void ArmRenameHook(std::function<void()> hook)
{
    gBeforeSnapshotRename = std::move(hook);
}

void ResetRenameHook()
{
    gBeforeSnapshotRename = {};
}

void ArmCloseHook(std::string marker, int trigger, std::function<void(const std::string &)> hook)
{
    std::lock_guard<std::mutex> lock(gCloseHookMutex);
    gClosePathMarker = std::move(marker);
    gCloseTrigger = trigger;
    gMatchingCloses = 0;
    gAfterMatchingClose = std::move(hook);
}

void ResetCloseHook()
{
    std::lock_guard<std::mutex> lock(gCloseHookMutex);
    gClosePathMarker.clear();
    gCloseTrigger = 0;
    gMatchingCloses = 0;
    gAfterMatchingClose = {};
}
}  // namespace snapshot_storage_test_hooks

extern "C" int renameat(int oldDirectoryFd, const char *oldPath, int newDirectoryFd, const char *newPath)
{
    if (gBeforeSnapshotRename && oldPath != nullptr && newPath != nullptr &&
        std::strstr(oldPath, ".staging.") != nullptr && std::strcmp(newPath, "checkpoint.img") == 0) {
        auto hook = std::move(gBeforeSnapshotRename);
        gBeforeSnapshotRename = {};
        hook();
    }
    return static_cast<int>(syscall(SYS_renameat, oldDirectoryFd, oldPath, newDirectoryFd, newPath));
}

extern "C" int close(int fd)
{
    std::array<char, 1024> pathBuffer{};
    auto descriptor = "/proc/self/fd/" + std::to_string(fd);
    auto size = readlink(descriptor.c_str(), pathBuffer.data(), pathBuffer.size() - 1);
    std::string path = size > 0 ? std::string(pathBuffer.data(), static_cast<size_t>(size)) : std::string();
    auto result = static_cast<int>(syscall(SYS_close, fd));
    using namespace snapshot_storage_test_hooks;
    std::function<void(const std::string &)> hook;
    {
        std::lock_guard<std::mutex> lock(gCloseHookMutex);
        if (!gClosePathMarker.empty() && path.find(gClosePathMarker) != std::string::npos &&
            ++gMatchingCloses == gCloseTrigger && gAfterMatchingClose) {
            hook = std::move(gAfterMatchingClose);
            gClosePathMarker.clear();
            gCloseTrigger = 0;
            gMatchingCloses = 0;
        }
    }
    if (hook) {
        hook(path);
    }
    return result;
}

namespace functionsystem::snapshot_storage {
namespace {

namespace fs = std::filesystem;

std::string ReadFile(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path &path, const std::string &content)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

fs::path FindStagingLeaf(const fs::path &parent, const std::string &writerPath)
{
    auto exposedLeaf = fs::path(writerPath).filename();
    if (exposedLeaf.string().find(".staging.") != std::string::npos) {
        return exposedLeaf;
    }
    for (const auto &entry : fs::directory_iterator(parent)) {
        if (entry.path().filename().string().find(".staging.") != std::string::npos) {
            return entry.path().filename();
        }
    }
    return {};
}

class TempDirectory {
public:
    TempDirectory()
    {
        auto pattern = (fs::temp_directory_path() / "snapshot-storage-test-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        path_ = mkdtemp(buffer.data());
    }

    ~TempDirectory()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path &Path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

struct ObjectRecord {
    std::string payload;
    SnapshotObjectMetadata metadata;
    std::string etag;
};

class FakeObsClient final : public ObsSnapshotClient {
public:
    Status MultipartUpload(const std::string &key, const std::string &sourceFile,
                           const SnapshotObjectMetadata &metadata) override
    {
        objects[key] = ObjectRecord{ ReadFile(sourceFile), metadata, metadata.sha256 };
        return Status::OK();
    }

    ObsHeadResult Head(const std::string &key) override
    {
        auto iter = objects.find(key);
        if (iter == objects.end()) {
            return { Status(StatusCode::FILE_NOT_FOUND), {}, {} };
        }
        return { Status::OK(), iter->second.metadata, iter->second.etag };
    }

    Status ConditionalCopy(const std::string &temporaryKey, const std::string &finalKey,
                           const std::string &expectedTemporaryETag,
                           const SnapshotObjectMetadata &metadata) override
    {
        auto iter = objects.find(temporaryKey);
        if (iter == objects.end()) {
            return Status(StatusCode::FILE_NOT_FOUND);
        }
        if (iter->second.etag != expectedTemporaryETag) {
            return Status(StatusCode::SCHEDULE_CONFLICTED);
        }
        objects[finalKey] = { iter->second.payload, metadata, metadata.sha256 };
        return Status::OK();
    }

    Status Download(const std::string &key, const std::string &destinationFile) override
    {
        auto iter = objects.find(key);
        if (iter == objects.end()) {
            return Status(StatusCode::FILE_NOT_FOUND);
        }
        if (beforeDownload) {
            beforeDownload();
        }
        if (!forbiddenDownloadDirectory.empty()) {
            std::error_code error;
            auto resolved = fs::canonical(fs::path(destinationFile).parent_path(), error);
            wroteThroughForbiddenParent = !error && resolved == fs::canonical(forbiddenDownloadDirectory, error);
        }
        std::ofstream output(destinationFile, std::ios::binary | std::ios::trunc);
        output << iter->second.payload;
        if (throwAfterDownloadWrite) {
            throw std::runtime_error("fake download exception");
        }
        return output.good() ? Status::OK() : Status(StatusCode::FAILED, "fake download write failed");
    }

    Status Delete(const std::string &key) override
    {
        objects.erase(key);
        return Status::OK();
    }

    std::map<std::string, ObjectRecord> objects;
    std::function<void()> beforeDownload;
    fs::path forbiddenDownloadDirectory;
    bool wroteThroughForbiddenParent{ false };
    bool throwAfterDownloadWrite{ false };
};

class OneShotNotFoundServer {
public:
    OneShotNotFoundServer()
    {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            throw std::runtime_error("failed to create test listener");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
            || listen(listener_, 1) != 0) {
            close(listener_);
            throw std::runtime_error("failed to bind test listener");
        }
        socklen_t size = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
            close(listener_);
            throw std::runtime_error("failed to resolve test listener port");
        }
        endpoint_ = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        server_ = std::thread([this]() {
            int connection = accept(listener_, nullptr, nullptr);
            if (connection < 0) {
                return;
            }
            std::array<char, 4096> request{};
            (void)recv(connection, request.data(), request.size(), 0);
            constexpr char response[] =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            (void)send(connection, response, sizeof(response) - 1, 0);
            close(connection);
        });
    }

    ~OneShotNotFoundServer()
    {
        if (server_.joinable()) {
            server_.join();
        }
        close(listener_);
    }

    const std::string &Endpoint() const
    {
        return endpoint_;
    }

private:
    int listener_{ -1 };
    std::string endpoint_;
    std::thread server_;
};

class OneShotCopyServer {
public:
    OneShotCopyServer()
    {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0) {
            throw std::runtime_error("failed to create copy test listener");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0
            || listen(listener_, 1) != 0) {
            close(listener_);
            throw std::runtime_error("failed to bind copy test listener");
        }
        socklen_t size = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
            close(listener_);
            throw std::runtime_error("failed to resolve copy test listener port");
        }
        endpoint_ = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        server_ = std::thread([this]() {
            int connection = accept(listener_, nullptr, nullptr);
            if (connection < 0) {
                return;
            }
            std::array<char, 16384> request{};
            auto received = recv(connection, request.data(), request.size(), 0);
            if (received > 0) {
                request_.assign(request.data(), static_cast<size_t>(received));
            }
            constexpr char body[] =
                "<CopyObjectResult><LastModified>2026-08-17T00:00:00.000Z</LastModified>"
                "<ETag>copy-etag</ETag></CopyObjectResult>";
            const auto response = std::string(
                "HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nContent-Length: ")
                + std::to_string(sizeof(body) - 1) + "\r\nConnection: close\r\n\r\n" + body;
            (void)send(connection, response.data(), response.size(), 0);
            close(connection);
        });
    }

    ~OneShotCopyServer()
    {
        if (server_.joinable()) {
            server_.join();
        }
        close(listener_);
    }

    const std::string &Endpoint() const
    {
        return endpoint_;
    }

    const std::string &Request()
    {
        if (server_.joinable()) {
            server_.join();
        }
        return request_;
    }

private:
    int listener_{ -1 };
    std::string endpoint_;
    std::string request_;
    std::thread server_;
};

TEST(HuaweiObsSnapshotClientTest, EmptyBucketHeadUsesFileNotFoundSemantics)
{
    OneShotNotFoundServer server;
    HuaweiObsSnapshotClient client({ .endpoint = server.Endpoint(),
                                      .bucket = "empty-bucket",
                                      .accessKey = "test-access-key",
                                      .secretKey = "test-secret-key",
                                      .useHttps = false,
                                      .pathStyle = true });

    auto result = client.Head("missing-snapshot");

    EXPECT_EQ(result.status.StatusCode(), StatusCode::FILE_NOT_FOUND)
        << result.status.ToString();
}

TEST(HuaweiObsSnapshotClientTest, ConditionalCopyReplacesCanonicalSnapshotMetadata)
{
    OneShotCopyServer server;
    HuaweiObsSnapshotClient client({ .endpoint = server.Endpoint(),
                                      .bucket = "snapshot-bucket",
                                      .accessKey = "test-access-key",
                                      .secretKey = "test-secret-key",
                                      .useHttps = false,
                                      .pathStyle = true });
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5",
                                     true, 1786906356 };

    auto status = client.ConditionalCopy("temporary", "final", "temporary-etag", metadata);
    const auto &request = server.Request();
    const auto hasMetadataHeader = [&request](const std::string &suffix) {
        return request.find("x-obs-" + suffix) != std::string::npos
            || request.find("x-amz-" + suffix) != std::string::npos;
    };

    EXPECT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_NE(request.find("PUT /snapshot-bucket/final"), std::string::npos) << request;
    EXPECT_TRUE(hasMetadataHeader("meta-yr-snapshot-id: snapshot-7")) << request;
    EXPECT_TRUE(hasMetadataHeader("meta-yr-source-instance-version: 19")) << request;
    EXPECT_TRUE(hasMetadataHeader(
        "meta-yr-sha256: 239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5")) << request;
    EXPECT_TRUE(hasMetadataHeader("meta-yr-complete: true")) << request;
    EXPECT_TRUE(hasMetadataHeader("meta-yr-expires-at-unix-seconds: 1786906356")) << request;
    EXPECT_TRUE(hasMetadataHeader("copy-source-if-match: temporary-etag")) << request;
}

class FakeDataSystemClient final : public DataSystemSnapshotClient {
public:
    Status Init(const DataSystemSnapshotConfig &config) override
    {
        ++initCalls;
        initializedHost = config.host;
        initializedPort = config.port;
        return initStatus;
    }

    Status Put(const std::string &key, const std::string &value) override
    {
        values[key] = value;
        if (corruptOnPut && !values[key].empty()) {
            values[key].back() = 'x';
        }
        return Status::OK();
    }

    DataSystemGetResult Get(const std::string &key) override
    {
        auto iter = values.find(key);
        if (iter == values.end()) {
            return { Status(StatusCode::FILE_NOT_FOUND), {} };
        }
        return { Status::OK(), iter->second };
    }

    Status Delete(const std::string &key) override
    {
        if (deleteStatus.IsError()) {
            return deleteStatus;
        }
        if (values.find(key) == values.end()) {
            return Status(StatusCode::FILE_NOT_FOUND, "missing snapshot key");
        }
        values.erase(key);
        return Status::OK();
    }

    std::map<std::string, std::string> values;
    bool corruptOnPut{ false };
    Status deleteStatus{ Status::OK() };
    Status initStatus{ Status::OK() };
    int initCalls{ 0 };
    std::string initializedHost;
    int32_t initializedPort{ 0 };
};

enum class BackendKind { OBS, DATA_SYSTEM };

class SnapshotStorageContractTest : public testing::TestWithParam<BackendKind> {
protected:
    void SetUp() override
    {
        if (GetParam() == BackendKind::OBS) {
            obsClient = std::make_shared<FakeObsClient>();
            storage = std::make_shared<ObsSnapshotStorage>(obsClient);
        } else {
            dataSystemClient = std::make_shared<FakeDataSystemClient>();
            storage = std::make_shared<DataSystemSnapshotStorage>(dataSystemClient);
        }
        source = directory.Path() / "source.img";
        WriteFile(source, "payload");
    }

    SnapshotObjectMetadata TemporaryMetadata() const
    {
        return { "snapshot-7", 19, 7,
                 "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    }

    SnapshotObjectMetadata FinalMetadata() const
    {
        auto metadata = TemporaryMetadata();
        metadata.complete = true;
        return metadata;
    }

    void PutAndPublish()
    {
        ASSERT_TRUE(storage->PutTemporary("temporary", source, TemporaryMetadata()).Get().IsOk());
        ASSERT_TRUE(storage->Publish("temporary", "final", FinalMetadata()).Get().IsOk());
    }

    TempDirectory directory;
    fs::path source;
    std::shared_ptr<FakeObsClient> obsClient;
    std::shared_ptr<FakeDataSystemClient> dataSystemClient;
    std::shared_ptr<SnapshotStorage> storage;
};

TEST(SnapshotStorageKeyTest, BuildsExactPauseKeys)
{
    EXPECT_EQ(BuildPauseSnapshotFinalKey("tenant-1", "instance-2"),
              "snapshots/pause/tenant-1/instance-2/snapshot.img");
    EXPECT_EQ(BuildPauseSnapshotTemporaryKey("tenant-1", "instance-2", "snapshot-3"),
              "snapshots/pause/tenant-1/instance-2/attempts/snapshot-3.tmp");
}

TEST(SnapshotStorageKeyTest, DifferentSnapshotIDsNeverShareFinalKey)
{
    const auto first = BuildPauseSnapshotKey("tenant-hash", "instance-1", "snapshot-a");
    const auto second = BuildPauseSnapshotKey("tenant-hash", "instance-1", "snapshot-b");

    EXPECT_EQ(first, "pause/v2/tenant-hash/instance-1/snapshot-a/checkpoint.img");
    EXPECT_EQ(second, "pause/v2/tenant-hash/instance-1/snapshot-b/checkpoint.img");
    EXPECT_NE(first, second);
    EXPECT_EQ(BuildPauseSnapshotTemporaryKey("tenant-hash", "instance-1", "snapshot-a", "attempt-7"),
              "pause/v2/tenant-hash/instance-1/snapshot-a/attempts/attempt-7.tmp");
}

TEST(SnapshotStorageKeyTest, ReusableSnapshotKeyIsTenantScopedAndIndependentOfSourceInstance)
{
    EXPECT_EQ(BuildReusableSnapshotKey("tenant-hash", "snapshot-a"),
              "reusable/v1/tenant-hash/snapshot-a/checkpoint.img");
    EXPECT_EQ(BuildReusableSnapshotTemporaryKey("tenant-hash", "snapshot-a", "attempt-7"),
              "reusable/v1/tenant-hash/snapshot-a/attempts/attempt-7.tmp");
    EXPECT_NE(BuildReusableSnapshotKey("tenant-hash", "snapshot-a"),
              BuildReusableSnapshotKey("tenant-hash", "snapshot-b"));
    EXPECT_NE(BuildReusableSnapshotKey("tenant-hash", "snapshot-a"),
              BuildReusableSnapshotKey("other-tenant", "snapshot-a"));
}

TEST(DataSystemSnapshotStorageKeyTest, EncodesLogicalPauseKeyForDataSystemObjectIDs)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    WriteFile(source, "payload");
    auto client = std::make_shared<FakeDataSystemClient>();
    DataSystemSnapshotStorage storage(client);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    const auto logicalKey = BuildPauseSnapshotTemporaryKey(
        "tenant-hash", "instance-1", "snapshot-7", "attempt-1");

    ASSERT_TRUE(storage.PutTemporary(logicalKey, source, metadata).Get().IsOk());
    ASSERT_EQ(client->values.size(), 1U);
    const auto &dataSystemKey = client->values.begin()->first;
    EXPECT_EQ(dataSystemKey, detail::DataSystemObjectKey(logicalKey));
    EXPECT_EQ(dataSystemKey.size(), std::string("yr-snapshot:").size() + 64U);
    EXPECT_EQ(dataSystemKey.find('/'), std::string::npos);
    EXPECT_NE(dataSystemKey, logicalKey);
    EXPECT_NE(dataSystemKey, detail::DataSystemObjectKey(logicalKey + "-other"));
}

TEST(SnapshotStorageMetadataTest, ExpiryParticipatesInImmutableMetadataEquality)
{
    SnapshotObjectMetadata first{ "snapshot-7", 19, 7,
                                  "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", true };
    first.expiresAtUnixSeconds = 1'800'000'000;
    auto differentExpiry = first;
    differentExpiry.expiresAtUnixSeconds = 1'800'000'001;

    EXPECT_TRUE(detail::MetadataEqual(first, first));
    EXPECT_FALSE(detail::MetadataEqual(first, differentExpiry));
    EXPECT_FALSE(detail::MetadataIdentityEqual(first, differentExpiry));
}

TEST_P(SnapshotStorageContractTest, ExpiredObjectHasNotFoundSemanticsAndExactCleanup)
{
    auto temporary = TemporaryMetadata();
    temporary.expiresAtUnixSeconds = 1;
    auto final = temporary;
    final.complete = true;
    ASSERT_TRUE(storage->PutTemporary("temporary-expired", source, temporary).Get().IsOk());
    ASSERT_TRUE(storage->Publish("temporary-expired", "final-expired", final).Get().IsOk());

    auto first = storage->Stat("final-expired").Get();
    auto second = storage->Stat("final-expired").Get();

    EXPECT_EQ(first.status.StatusCode(), StatusCode::FILE_NOT_FOUND);
    EXPECT_EQ(second.status.StatusCode(), StatusCode::FILE_NOT_FOUND);
}

TEST(DataSystemSnapshotStorageCreateTest, InitializesDedicatedClientWithConfiguredEndpoint)
{
    auto client = std::make_shared<FakeDataSystemClient>();
    std::shared_ptr<DataSystemSnapshotStorage> storage;

    auto status = DataSystemSnapshotStorage::Create({ .host = "data-system.internal", .port = 31501 }, storage,
                                                    client);

    ASSERT_TRUE(status.IsOk());
    ASSERT_NE(storage, nullptr);
    EXPECT_EQ(client->initCalls, 1);
    EXPECT_EQ(client->initializedHost, "data-system.internal");
    EXPECT_EQ(client->initializedPort, 31501);
}

TEST(DataSystemSnapshotStorageCreateTest, PropagatesDedicatedClientInitializationFailure)
{
    auto client = std::make_shared<FakeDataSystemClient>();
    client->initStatus = Status(StatusCode::BP_DATASYSTEM_ERROR, "dedicated client init failed");
    std::shared_ptr<DataSystemSnapshotStorage> storage =
        std::make_shared<DataSystemSnapshotStorage>(std::make_shared<FakeDataSystemClient>());

    auto status = DataSystemSnapshotStorage::Create({ .host = "data-system.internal", .port = 31501 }, storage,
                                                    client);

    EXPECT_EQ(status.StatusCode(), StatusCode::BP_DATASYSTEM_ERROR);
    EXPECT_EQ(status.RawMessage(), "dedicated client init failed");
    EXPECT_EQ(storage, nullptr);
    EXPECT_EQ(client->initCalls, 1);
}

TEST(DataSystemSnapshotStorageCreateTest, RejectsInvalidConfigBeforeInitializingClient)
{
    auto client = std::make_shared<FakeDataSystemClient>();
    std::shared_ptr<DataSystemSnapshotStorage> storage;

    auto emptyHost = DataSystemSnapshotStorage::Create({ .host = "", .port = 31501 }, storage, client);
    auto invalidPort = DataSystemSnapshotStorage::Create({ .host = "data-system.internal", .port = 0 }, storage,
                                                         client);

    EXPECT_EQ(emptyHost.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(invalidPort.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(storage, nullptr);
    EXPECT_EQ(client->initCalls, 0);
}

TEST(LocalSnapshotInspectionTest, ReturnsCompleteMetadataFromWorker)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "payload");
    auto worker = std::make_shared<ActorWorker>();

    auto result = InspectLocalSnapshotFile(worker, source, "snapshot-7", 19).Get();

    ASSERT_TRUE(result.status.IsOk()) << result.status.ToString();
    EXPECT_EQ(result.metadata.snapshotID, "snapshot-7");
    EXPECT_EQ(result.metadata.sourceInstanceVersion, 19);
    EXPECT_EQ(result.metadata.size, 7U);
    EXPECT_EQ(result.metadata.sha256, "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5");
    EXPECT_TRUE(result.metadata.complete);
}

TEST(SnapshotDirectoryPublicationTest, RoundTripsOpaqueMultiFileDirectory)
{
    for (const bool compress : {false, true}) {
        TempDirectory directory;
        const auto source = directory.Path() / "source";
        const auto destination = directory.Path() / "destination";
        fs::create_directories(source / "nested" / "empty");
        fs::create_directories(destination);
        WriteFile(source / "checkpoint.img", "state");
        WriteFile(source / "pages_meta.img", "metadata");
        WriteFile(source / "pages.img", "pages");
        WriteFile(source / "nested" / "runtime.img", "nested-state");
        auto worker = std::make_shared<ActorWorker>();

        const auto prepared = PrepareSnapshotPublicationFile(
            worker, source.string(), compress).Get();
        ASSERT_TRUE(prepared.status.IsOk()) << prepared.status.ToString();
        EXPECT_TRUE(prepared.temporary);
        EXPECT_GT(prepared.size, 0U);
        ASSERT_TRUE(fs::is_regular_file(prepared.path));

        const auto materialized = MaterializeSnapshotPublicationDirectory(
            worker, prepared.path, destination).Get();
        ASSERT_TRUE(materialized.IsOk()) << materialized.ToString();
        EXPECT_EQ(ReadFile(destination / "checkpoint.img"), "state");
        EXPECT_EQ(ReadFile(destination / "pages_meta.img"), "metadata");
        EXPECT_EQ(ReadFile(destination / "pages.img"), "pages");
        EXPECT_EQ(ReadFile(destination / "nested" / "runtime.img"), "nested-state");
        EXPECT_TRUE(fs::is_directory(destination / "nested" / "empty"));
        fs::remove(prepared.path);
    }
}

TEST(SnapshotDirectoryPublicationTest, LegacySingleFileMaterializesAsCheckpointImage)
{
    TempDirectory directory;
    const auto source = directory.Path() / "legacy.img";
    const auto destination = directory.Path() / "destination";
    WriteFile(source, "legacy-payload");
    fs::create_directories(destination);

    const auto materialized = MaterializeSnapshotPublicationDirectory(
        std::make_shared<ActorWorker>(), source.string(), destination).Get();

    ASSERT_TRUE(materialized.IsOk()) << materialized.ToString();
    EXPECT_EQ(ReadFile(destination / "checkpoint.img"), "legacy-payload");
}

TEST(LocalSnapshotInspectionTest, MissingSourceReturnsFileNotFound)
{
    TempDirectory directory;
    auto worker = std::make_shared<ActorWorker>();

    auto result =
        InspectLocalSnapshotFile(worker, directory.Path() / "missing-checkpoint.img", "snapshot-7", 19).Get();

    EXPECT_EQ(result.status.StatusCode(), StatusCode::FILE_NOT_FOUND);
    EXPECT_FALSE(result.metadata.complete);
}

TEST(LocalSnapshotInspectionTest, RejectsSymlinkSource)
{
    TempDirectory directory;
    auto actual = directory.Path() / "actual.img";
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(actual, "payload");
    fs::create_symlink(actual, source);
    auto worker = std::make_shared<ActorWorker>();

    auto result = InspectLocalSnapshotFile(worker, source, "snapshot-7", 19).Get();

    EXPECT_EQ(result.status.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_FALSE(result.metadata.complete);
}

TEST(LocalSnapshotInspectionTest, QueuesInspectionWithoutBlockingCaller)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "payload");
    auto worker = std::make_shared<ActorWorker>();
    auto started = std::make_shared<litebus::Promise<Status>>();
    auto release = std::make_shared<litebus::Promise<Status>>();
    auto blocker = worker->AsyncWork([started, release]() {
        started->SetValue(Status::OK());
        (void)release->GetFuture().Get();
    });
    ASSERT_TRUE(started->GetFuture().WaitFor(5000).IsOK());

    auto inspection = InspectLocalSnapshotFile(worker, source, "snapshot-7", 19);

    EXPECT_TRUE(inspection.IsInit());
    release->SetValue(Status::OK());
    ASSERT_TRUE(blocker.WaitFor(5000).IsOK());
    ASSERT_TRUE(inspection.WaitFor(5000).IsOK());
    EXPECT_TRUE(inspection.Get().status.IsOk());
}

TEST(LocalSnapshotInspectionTest, RejectsInPlaceAppendDuringInspection)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "payload");
    auto worker = std::make_shared<ActorWorker>();

    auto result = detail::InspectLocalSnapshotFileWithHookForTest(
        worker, source, "snapshot-7", 19, [source]() {
            std::ofstream output(source, std::ios::binary | std::ios::app);
            output << "-mutated";
        }).Get();

    EXPECT_EQ(result.status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_FALSE(result.metadata.complete);
}

TEST(LocalSnapshotInspectionTest, RejectsInPlaceTruncateDuringInspection)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "payload");
    auto worker = std::make_shared<ActorWorker>();

    auto result = detail::InspectLocalSnapshotFileWithHookForTest(
        worker, source, "snapshot-7", 19, [source]() { ASSERT_EQ(truncate(source.c_str(), 3), 0); }).Get();

    EXPECT_EQ(result.status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_FALSE(result.metadata.complete);
}

TEST(LocalSnapshotInspectionTest, RejectsSameSizeInPlaceWriteDuringInspection)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "payload");
    auto worker = std::make_shared<ActorWorker>();

    auto result = detail::InspectLocalSnapshotFileWithHookForTest(
        worker, source, "snapshot-7", 19, [source]() {
            struct stat before {};
            ASSERT_EQ(stat(source.c_str(), &before), 0);
            int fd = open(source.c_str(), O_WRONLY | O_CLOEXEC);
            ASSERT_GE(fd, 0);
            ASSERT_EQ(pwrite(fd, "P", 1, 0), 1);
            struct timespec times[2] = { before.st_atim, before.st_mtim };
            ++times[1].tv_sec;
            ASSERT_EQ(futimens(fd, times), 0);
            close(fd);
        }).Get();

    EXPECT_EQ(result.status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_FALSE(result.metadata.complete);
}

TEST(SnapshotStorageWorkerTest, DispatchFailureCompletesResultFuture)
{
    bool operationRan = false;
    auto result = detail::RunOnWorkerWithDispatch<SnapshotStat>(
        [&operationRan]() {
            operationRan = true;
            return SnapshotStat{ Status::OK(), {} };
        },
        [](std::function<void()>) {
            litebus::Promise<Status> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed.GetFuture();
        });

    ASSERT_TRUE(result.WaitFor(5000).IsOK());
    EXPECT_FALSE(operationRan);
    EXPECT_EQ(result.Get().status.StatusCode(), StatusCode::FAILED);
}

TEST(SnapshotStorageWorkerTest, LateDispatchFailureDoesNotDoubleSetCompletedResult)
{
    auto result = detail::RunOnWorkerWithDispatch<SnapshotStat>(
        []() { return SnapshotStat{ Status::OK(), { "snapshot-7", 19, 7, "sha256", true } }; },
        [](std::function<void()> operation) {
            operation();
            litebus::Promise<Status> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::FAILED));
            return failed.GetFuture();
        });

    ASSERT_TRUE(result.WaitFor(5000).IsOK());
    EXPECT_TRUE(result.Get().status.IsOk());
    EXPECT_TRUE(result.Get().metadata.complete);
}

TEST_P(SnapshotStorageContractTest, PutStatPublishGetAndIdempotentDelete)
{
    ASSERT_TRUE(storage->PutTemporary("temporary", source, TemporaryMetadata()).Get().IsOk());
    auto temporary = storage->Stat("temporary").Get();
    ASSERT_TRUE(temporary.status.IsOk());
    EXPECT_EQ(temporary.metadata.snapshotID, "snapshot-7");
    EXPECT_EQ(temporary.metadata.sourceInstanceVersion, 19);
    EXPECT_EQ(temporary.metadata.size, 7U);
    EXPECT_EQ(temporary.metadata.sha256, TemporaryMetadata().sha256);
    EXPECT_FALSE(temporary.metadata.complete);

    ASSERT_TRUE(storage->Publish("temporary", "final", FinalMetadata()).Get().IsOk());
    auto published = storage->Stat("final").Get();
    ASSERT_TRUE(published.status.IsOk());
    EXPECT_TRUE(published.metadata.complete);

    auto destination = directory.Path() / "download" / "checkpoint.img";
    ASSERT_TRUE(storage->Get("final", destination).Get().IsOk());
    EXPECT_EQ(ReadFile(destination), "payload");
    EXPECT_TRUE(storage->Delete("final").Get().IsOk());
    EXPECT_TRUE(storage->Delete("final").Get().IsOk());
    EXPECT_EQ(storage->Stat("final").Get().status.StatusCode(), StatusCode::FILE_NOT_FOUND);
}

TEST_P(SnapshotStorageContractTest, SameMetadataRetryIsIdempotentAndDifferentHashConflicts)
{
    ASSERT_TRUE(storage->PutTemporary("temporary", source, TemporaryMetadata()).Get().IsOk());
    ASSERT_TRUE(storage->PutTemporary("temporary", source, TemporaryMetadata()).Get().IsOk());

    auto conflict = TemporaryMetadata();
    conflict.sha256 = std::string(64, '0');
    EXPECT_EQ(storage->PutTemporary("temporary", source, conflict).Get().StatusCode(),
              StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_EQ(storage->Stat("temporary").Get().metadata.sha256, TemporaryMetadata().sha256);
}

TEST_P(SnapshotStorageContractTest, EveryDifferentIdentityFieldConflicts)
{
    ASSERT_TRUE(storage->PutTemporary("temporary", source, TemporaryMetadata()).Get().IsOk());
    auto snapshot = TemporaryMetadata();
    snapshot.snapshotID = "snapshot-other";
    auto version = TemporaryMetadata();
    version.sourceInstanceVersion = 20;
    auto size = TemporaryMetadata();
    size.size = 8;
    auto hash = TemporaryMetadata();
    hash.sha256 = std::string(64, '0');

    for (const auto &conflict : { snapshot, version, size, hash }) {
        EXPECT_EQ(storage->PutTemporary("temporary", source, conflict).Get().StatusCode(),
                  StatusCode::SCHEDULE_CONFLICTED);
    }
}

TEST_P(SnapshotStorageContractTest, PublishRetryIsIdempotentAndDifferentIdentityConflicts)
{
    PutAndPublish();
    EXPECT_TRUE(storage->Publish("temporary", "final", FinalMetadata()).Get().IsOk());

    auto conflict = FinalMetadata();
    conflict.snapshotID = "snapshot-other";
    EXPECT_EQ(storage->Publish("temporary", "final", conflict).Get().StatusCode(),
              StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_EQ(storage->Stat("final").Get().metadata.snapshotID, "snapshot-7");
}

TEST_P(SnapshotStorageContractTest, CorruptDownloadDoesNotReplaceDestinationOrLeakStaging)
{
    PutAndPublish();
    if (obsClient != nullptr) {
        obsClient->objects["final"].payload = "corrupt";
    } else {
        auto &envelope = dataSystemClient->values[detail::DataSystemObjectKey("final")];
        envelope.back() = envelope.back() == 'x' ? 'y' : 'x';
    }

    auto destination = directory.Path() / "checkpoint.img";
    WriteFile(destination, "old");
    EXPECT_FALSE(storage->Get("final", destination).Get().IsOk());
    EXPECT_EQ(ReadFile(destination), "old");
    for (const auto &entry : fs::directory_iterator(directory.Path())) {
        EXPECT_EQ(entry.path().filename().string().find(".staging."), std::string::npos);
    }
}

TEST_P(SnapshotStorageContractTest, DestinationParentSymlinkIsRejected)
{
    PutAndPublish();
    auto outside = directory.Path() / "outside";
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, directory.Path() / "download");

    auto destination = directory.Path() / "download" / "checkpoint.img";
    EXPECT_FALSE(storage->Get("final", destination).Get().IsOk());
    EXPECT_FALSE(fs::exists(outside / "checkpoint.img"));
}

TEST(SnapshotStorageRaceTest, DestinationParentSwapCannotEscapePinnedDirectory)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    WriteFile(source, "payload");
    auto client = std::make_shared<FakeObsClient>();
    ObsSnapshotStorage storage(client);
    SnapshotObjectMetadata temporary{ "snapshot-7", 19, 7,
                                      "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    ASSERT_TRUE(storage.PutTemporary("temporary", source, temporary).Get().IsOk());
    auto final = temporary;
    final.complete = true;
    ASSERT_TRUE(storage.Publish("temporary", "final", final).Get().IsOk());

    auto parent = directory.Path() / "download";
    auto displaced = directory.Path() / "displaced";
    auto outside = directory.Path() / "outside";
    fs::create_directories(parent);
    fs::create_directories(outside);
    client->forbiddenDownloadDirectory = outside;
    client->beforeDownload = [&]() {
        fs::rename(parent, displaced);
        fs::create_directory_symlink(outside, parent);
    };

    auto status = storage.Get("final", parent / "checkpoint.img").Get();
    EXPECT_FALSE(status.IsOk());
    EXPECT_FALSE(client->wroteThroughForbiddenParent);
    EXPECT_FALSE(fs::exists(outside / "checkpoint.img"));
}

TEST(SnapshotStorageRaceTest, DownloadExceptionRemovesPreparedStagingFile)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    WriteFile(source, "payload");
    auto client = std::make_shared<FakeObsClient>();
    ObsSnapshotStorage storage(client);
    SnapshotObjectMetadata temporary{ "snapshot-7", 19, 7,
                                      "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    ASSERT_TRUE(storage.PutTemporary("temporary", source, temporary).Get().IsOk());
    auto final = temporary;
    final.complete = true;
    ASSERT_TRUE(storage.Publish("temporary", "final", final).Get().IsOk());
    client->throwAfterDownloadWrite = true;

    auto parent = directory.Path() / "download";
    auto status = storage.Get("final", parent / "checkpoint.img").Get();

    EXPECT_FALSE(status.IsOk());
    EXPECT_FALSE(fs::exists(parent / "checkpoint.img"));
    for (const auto &entry : fs::directory_iterator(parent)) {
        EXPECT_EQ(entry.path().filename().string().find(".staging."), std::string::npos);
    }
}

TEST(SnapshotStorageRaceTest, StagingLeafSwapCannotRedirectFopenOutsidePinnedDirectory)
{
    TempDirectory directory;
    auto parent = directory.Path() / "download";
    auto destination = parent / "checkpoint.img";
    auto forbidden = directory.Path() / "forbidden.img";
    WriteFile(forbidden, "forbidden");
    detail::SecureDownloadTarget target;
    ASSERT_TRUE(target.Prepare(destination).IsOk());
    auto stagingLeaf = FindStagingLeaf(parent, target.StagingPath());
    ASSERT_FALSE(stagingLeaf.empty());
    std::error_code ignored;
    fs::remove(parent / stagingLeaf, ignored);
    fs::create_symlink(forbidden, parent / stagingLeaf);

    FILE *writer = fopen(target.StagingPath().c_str(), "wb");
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(fwrite("payload", 1, 7, writer), 7U);
    ASSERT_EQ(fclose(writer), 0);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", true };
    auto status = target.Commit(metadata);

    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(ReadFile(forbidden), "forbidden");
    EXPECT_FALSE(fs::exists(destination));
}

TEST(SnapshotStorageRaceTest, ComposedDownloadTargetsKeepOnePinnedDestinationDescriptor)
{
    TempDirectory directory;
    auto destination = directory.Path() / "download/checkpoint.img";
    detail::SecureDownloadTarget outer;
    ASSERT_TRUE(outer.Prepare(destination).IsOk());
    detail::SecureDownloadTarget backend;
    ASSERT_TRUE(backend.Prepare(outer.StagingPath()).IsOk());

    FILE *writer = fopen(backend.StagingPath().c_str(), "wb");
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(fwrite("payload", 1, 7, writer), 7U);
    ASSERT_EQ(fclose(writer), 0);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", true };

    ASSERT_TRUE(backend.Commit(metadata).IsOk());
    ASSERT_TRUE(outer.Commit(metadata).IsOk());
    EXPECT_EQ(ReadFile(destination), "payload");
}

TEST(SnapshotStorageRaceTest, ParentSwapAfterPreRenameCheckRemovesDetachedFinal)
{
    TempDirectory directory;
    auto parent = directory.Path() / "download";
    auto detached = directory.Path() / "detached";
    auto destination = parent / "checkpoint.img";
    detail::SecureDownloadTarget target;
    ASSERT_TRUE(target.Prepare(destination).IsOk());
    FILE *writer = fopen(target.StagingPath().c_str(), "wb");
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(fwrite("payload", 1, 7, writer), 7U);
    ASSERT_EQ(fclose(writer), 0);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", true };
    gBeforeSnapshotRename = [&]() {
        fs::rename(parent, detached);
        fs::create_directories(parent);
    };

    auto status = target.Commit(metadata);
    gBeforeSnapshotRename = {};

    EXPECT_FALSE(status.IsOk());
    EXPECT_FALSE(fs::exists(destination));
    EXPECT_FALSE(fs::exists(detached / "checkpoint.img"));
}

TEST(SnapshotStorageRaceTest, HeldStagingDescriptorPreventsVerifiedLeafReplacement)
{
    TempDirectory directory;
    auto parent = directory.Path() / "download";
    auto destination = parent / "checkpoint.img";
    detail::SecureDownloadTarget target;
    ASSERT_TRUE(target.Prepare(destination).IsOk());
    FILE *writer = fopen(target.StagingPath().c_str(), "wb");
    ASSERT_NE(writer, nullptr);
    ASSERT_EQ(fwrite("payload", 1, 7, writer), 7U);
    ASSERT_EQ(fclose(writer), 0);
    bool replaced = false;
    snapshot_storage_test_hooks::ArmCloseHook("checkpoint.img.staging.", 1, [&](const std::string &staging) {
        replaced = true;
        fs::remove(staging);
        WriteFile(staging, "replace");
    });
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", true };

    auto status = target.Commit(metadata);
    snapshot_storage_test_hooks::ResetCloseHook();

    EXPECT_TRUE(status.IsOk()) << status.ToString();
    EXPECT_FALSE(replaced);
    EXPECT_EQ(ReadFile(destination), "payload");
}

INSTANTIATE_TEST_SUITE_P(SnapshotStorageBackends, SnapshotStorageContractTest,
                         testing::Values(BackendKind::OBS, BackendKind::DATA_SYSTEM));

TEST(SnapshotStorageDataSystemIntegrityTest, StatRejectsEnvelopeWithCorruptPayload)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    WriteFile(source, "payload");
    auto client = std::make_shared<FakeDataSystemClient>();
    DataSystemSnapshotStorage storage(client);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    ASSERT_TRUE(storage.PutTemporary("temporary", source, metadata).Get().IsOk());
    client->values[detail::DataSystemObjectKey("temporary")].back() = 'x';
    EXPECT_FALSE(storage.Stat("temporary").Get().status.IsOk());
}

TEST(SnapshotStorageDataSystemIntegrityTest, PutRejectsBackendWriteThatFailsPostcondition)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    WriteFile(source, "payload");
    auto client = std::make_shared<FakeDataSystemClient>();
    client->corruptOnPut = true;
    DataSystemSnapshotStorage storage(client);
    SnapshotObjectMetadata metadata{ "snapshot-7", 19, 7,
                                     "239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5", false };
    EXPECT_FALSE(storage.PutTemporary("temporary", source, metadata).Get().IsOk());
}

TEST(SnapshotStorageDataSystemIntegrityTest, DeleteDoesNotSwallowNonNotFoundError)
{
    auto client = std::make_shared<FakeDataSystemClient>();
    client->deleteStatus = Status(StatusCode::BP_DATASYSTEM_ERROR, "transport not found in routing table");
    DataSystemSnapshotStorage storage(client);

    EXPECT_EQ(storage.Delete("missing").Get().StatusCode(), StatusCode::BP_DATASYSTEM_ERROR);
}

TEST(SnapshotStorageDataSystemIntegrityTest, MapsRawDataSystemStatusByCode)
{
    EXPECT_EQ(detail::MapDataSystemStatus(datasystem::Status(datasystem::K_NOT_FOUND, "opaque missing")).StatusCode(),
              StatusCode::FILE_NOT_FOUND);
    EXPECT_EQ(detail::MapDataSystemStatus(datasystem::Status(datasystem::K_IO_ERROR, "not found text")).StatusCode(),
              StatusCode::BP_DATASYSTEM_ERROR);
}

TEST(SnapshotStorageDataSystemIntegrityTest, FailedPayloadWriteRemovesStagingAndDestination)
{
    TempDirectory directory;
    auto source = directory.Path() / "source.img";
    std::string payload(64 * 1024, 'x');
    WriteFile(source, payload);
    auto client = std::make_shared<FakeDataSystemClient>();
    DataSystemSnapshotStorage storage(client);
    SnapshotObjectMetadata metadata{ "snapshot-large", 19, payload.size(),
                                     "1f8745f0d2d1387ec1af2211a3cf417b2e9e885e853472649c1d979d0e9370e3", false };
    ASSERT_TRUE(storage.PutTemporary("temporary", source, metadata).Get().IsOk());
    metadata.complete = true;
    ASSERT_TRUE(storage.Publish("temporary", "final", metadata).Get().IsOk());

    struct rlimit originalLimit {};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &originalLimit), 0);
    auto originalSignal = std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit limited = originalLimit;
    limited.rlim_cur = 1024;
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &limited), 0);
    auto destination = directory.Path() / "download" / "checkpoint.img";
    auto status = storage.Get("final", destination).Get();
    EXPECT_EQ(setrlimit(RLIMIT_FSIZE, &originalLimit), 0);
    std::signal(SIGXFSZ, originalSignal);

    EXPECT_FALSE(status.IsOk());
    EXPECT_FALSE(fs::exists(destination));
    for (const auto &entry : fs::directory_iterator(destination.parent_path())) {
        EXPECT_EQ(entry.path().filename().string().find(".staging."), std::string::npos);
    }
}

class ScriptedArtifactStorage final : public SnapshotStorage {
public:
    litebus::Future<Status> PutTemporary(const std::string &, const std::string &,
                                         const SnapshotObjectMetadata &) override
    {
        ++putCalls;
        return putStatus;
    }

    litebus::Future<SnapshotStat> Stat(const std::string &) override
    {
        ++statCalls;
        return statResult;
    }

    litebus::Future<Status> Publish(const std::string &, const std::string &,
                                    const SnapshotObjectMetadata &expected) override
    {
        ++publishCalls;
        publishedMetadata = expected;
        if (publishFutureFails) {
            litebus::Future<Status> failed;
            failed.SetFailed(static_cast<int32_t>(StatusCode::GRPC_UNAVAILABLE));
            return failed;
        }
        return publishStatus;
    }

    litebus::Future<Status> Get(const std::string &, const std::string &) override
    {
        return Status(StatusCode::FAILED, "unused");
    }

    litebus::Future<Status> Delete(const std::string &) override
    {
        return Status::OK();
    }

    Status putStatus = Status::OK();
    Status publishStatus = Status::OK();
    SnapshotStat statResult{ Status(StatusCode::FILE_NOT_FOUND), {} };
    SnapshotObjectMetadata publishedMetadata;
    bool publishFutureFails{ false };
    int putCalls{ 0 };
    int publishCalls{ 0 };
    int statCalls{ 0 };
};

ArtifactPublishRequest MakeArtifactPublishRequest(const fs::path &source)
{
    ArtifactPublishRequest request;
    request.sourceFile = source.string();
    request.temporaryKey = "temporary";
    request.finalKey = "final";
    request.snapshotID = "snapshot-publisher";
    request.sourceInstanceVersion = 19;
    request.createdAtUnixSeconds = 1'700'000'000;
    request.ttlSeconds = 600;
    return request;
}

TEST(SnapshotArtifactPublisherTest, SuccessfulConditionalPublishDoesNotAddStatRoundTrip)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "publisher payload");
    auto storage = std::make_shared<ScriptedArtifactStorage>();
    auto publisher = std::make_shared<SnapshotArtifactPublisher>(storage, std::make_shared<ActorWorker>());

    auto result = publisher->Publish(MakeArtifactPublishRequest(source));
    ASSERT_TRUE(result.WaitFor(5'000).IsOK());
    ASSERT_TRUE(result.Get().status.IsOk()) << result.Get().status.ToString();
    EXPECT_TRUE(result.Get().metadata.complete);
    EXPECT_EQ(storage->putCalls, 1);
    EXPECT_EQ(storage->publishCalls, 1);
    EXPECT_EQ(storage->statCalls, 0);
}

TEST(SnapshotArtifactPublisherTest, ZeroTtlPublishesNonExpiringObject)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "reusable snapshot payload");
    auto storage = std::make_shared<ScriptedArtifactStorage>();
    auto publisher = std::make_shared<SnapshotArtifactPublisher>(storage, std::make_shared<ActorWorker>());
    auto request = MakeArtifactPublishRequest(source);
    request.ttlSeconds = 0;

    auto result = publisher->Publish(request);

    ASSERT_TRUE(result.WaitFor(5'000).IsOK());
    ASSERT_TRUE(result.Get().status.IsOk()) << result.Get().status.ToString();
    EXPECT_EQ(result.Get().metadata.expiresAtUnixSeconds, 0);
    EXPECT_EQ(storage->publishedMetadata.expiresAtUnixSeconds, 0);
    EXPECT_EQ(storage->putCalls, 1);
    EXPECT_EQ(storage->publishCalls, 1);
}

TEST(SnapshotArtifactPublisherTest, NegativeTtlRemainsInvalid)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "invalid snapshot payload");
    auto storage = std::make_shared<ScriptedArtifactStorage>();
    auto publisher = std::make_shared<SnapshotArtifactPublisher>(storage, std::make_shared<ActorWorker>());
    auto request = MakeArtifactPublishRequest(source);
    request.ttlSeconds = -1;

    auto result = publisher->Publish(request);

    ASSERT_TRUE(result.WaitFor(5'000).IsOK());
    EXPECT_EQ(result.Get().status.StatusCode(), StatusCode::ERR_PARAM_INVALID);
    EXPECT_EQ(storage->putCalls, 0);
    EXPECT_EQ(storage->publishCalls, 0);
}

TEST(SnapshotArtifactPublisherTest, LostPublishResponseConvergesFromExactFinalObject)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "publisher payload");
    auto storage = std::make_shared<ScriptedArtifactStorage>();
    storage->publishFutureFails = true;
    auto publisher = std::make_shared<SnapshotArtifactPublisher>(storage, std::make_shared<ActorWorker>());

    auto request = MakeArtifactPublishRequest(source);
    auto inspection = InspectLocalSnapshotFile(std::make_shared<ActorWorker>(), source, request.snapshotID,
                                               request.sourceInstanceVersion);
    ASSERT_TRUE(inspection.WaitFor(5'000).IsOK());
    auto expected = inspection.Get().metadata;
    expected.complete = true;
    expected.expiresAtUnixSeconds = request.createdAtUnixSeconds + request.ttlSeconds;
    storage->statResult = { Status::OK(), expected };
    auto result = publisher->Publish(request);

    ASSERT_TRUE(result.WaitFor(5'000).IsOK());
    EXPECT_TRUE(result.Get().status.IsOk()) << result.Get().status.ToString();
    EXPECT_EQ(result.Get().metadata.sha256, expected.sha256);
    EXPECT_EQ(storage->statCalls, 1);
}

TEST(SnapshotArtifactPublisherTest, ConflictingFinalObjectFailsClosed)
{
    TempDirectory directory;
    auto source = directory.Path() / "checkpoint.img";
    WriteFile(source, "publisher payload");
    auto storage = std::make_shared<ScriptedArtifactStorage>();
    storage->publishStatus = Status(StatusCode::SCHEDULE_CONFLICTED, "already exists");
    SnapshotObjectMetadata conflict{ "snapshot-publisher", 19, 1, "different", true,
                                     1'700'000'600 };
    storage->statResult = { Status::OK(), conflict };
    auto publisher = std::make_shared<SnapshotArtifactPublisher>(storage, std::make_shared<ActorWorker>());

    auto result = publisher->Publish(MakeArtifactPublishRequest(source));

    ASSERT_TRUE(result.WaitFor(5'000).IsOK());
    EXPECT_EQ(result.Get().status.StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_FALSE(result.Get().resultUnknown);
}

}  // namespace
}  // namespace functionsystem::snapshot_storage
