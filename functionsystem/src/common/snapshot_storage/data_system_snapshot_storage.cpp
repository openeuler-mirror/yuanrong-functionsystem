/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "common/snapshot_storage/data_system_snapshot_storage.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "common/kv_client/kv_client.h"
#include "openssl/sha.h"

namespace functionsystem::snapshot_storage {
namespace {

constexpr char ENVELOPE_MAGIC_V1[] = "YRSNAP01";
constexpr char ENVELOPE_MAGIC[] = "YRSNAP02";
constexpr size_t MAGIC_SIZE = sizeof(ENVELOPE_MAGIC) - 1;

class KVDataSystemSnapshotClient final : public DataSystemSnapshotClient {
public:
    Status Init(const DataSystemSnapshotConfig &) override
    {
        return Status::OK();
    }

    Status Put(const std::string &key, const std::string &value) override
    {
        return KVClient::GetInstance().Put(key, value);
    }

    DataSystemGetResult Get(const std::string &key) override
    {
        auto [rawStatus, buffer] = KVClient::GetInstance().GetRaw(key);
        auto status = detail::MapDataSystemStatus(rawStatus);
        if (status.IsError()) {
            return { std::move(status), {} };
        }
        return { Status::OK(), std::string(static_cast<const char *>(buffer.ImmutableData()), buffer.GetSize()) };
    }

    Status Delete(const std::string &key) override
    {
        return detail::MapDataSystemStatus(KVClient::GetInstance().DeleteRaw(key));
    }
};

class DedicatedKVDataSystemSnapshotClient final : public DataSystemSnapshotClient {
public:
    Status Init(const DataSystemSnapshotConfig &config) override
    {
        datasystem::ConnectOptions connectOptions;
        connectOptions.host = config.host;
        connectOptions.port = config.port;
        auto client = std::make_unique<datasystem::KVClient>(connectOptions);
        auto status = client->Init();
        if (status.IsError()) {
            return detail::MapDataSystemStatus(status);
        }
        client_ = std::move(client);
        return Status::OK();
    }

    Status Put(const std::string &key, const std::string &value) override
    {
        return detail::MapDataSystemStatus(client_->Set(key, value));
    }

    DataSystemGetResult Get(const std::string &key) override
    {
        datasystem::Optional<datasystem::ReadOnlyBuffer> buffer;
        auto rawStatus = client_->Get(key, buffer);
        auto status = detail::MapDataSystemStatus(rawStatus);
        if (status.IsError()) {
            return { std::move(status), {} };
        }
        return { Status::OK(), std::string(static_cast<const char *>((*buffer).ImmutableData()), (*buffer).GetSize()) };
    }

    Status Delete(const std::string &key) override
    {
        return detail::MapDataSystemStatus(client_->Del(key));
    }

private:
    std::unique_ptr<datasystem::KVClient> client_;
};

void AppendUint64(std::string &output, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
}

bool ReadUint64(const std::string &input, size_t &offset, uint64_t &value)
{
    if (offset > input.size() || input.size() - offset < sizeof(uint64_t)) {
        return false;
    }
    value = 0;
    for (size_t index = 0; index < sizeof(uint64_t); ++index) {
        value = (value << 8U) | static_cast<unsigned char>(input[offset++]);
    }
    return true;
}

std::string ReadFile(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

Status EncodeEnvelope(const std::string &sourceFile, const SnapshotObjectMetadata &metadata, std::string &output)
{
    auto status = detail::ValidateFile(sourceFile, metadata);
    if (status.IsError()) {
        return status;
    }
    auto payload = ReadFile(sourceFile);
    output.assign(ENVELOPE_MAGIC, MAGIC_SIZE);
    AppendUint64(output, metadata.snapshotID.size());
    output.append(metadata.snapshotID);
    AppendUint64(output, static_cast<uint64_t>(metadata.sourceInstanceVersion));
    AppendUint64(output, metadata.size);
    AppendUint64(output, metadata.sha256.size());
    output.append(metadata.sha256);
    output.push_back(metadata.complete ? '\1' : '\0');
    AppendUint64(output, static_cast<uint64_t>(metadata.expiresAtUnixSeconds));
    output.append(payload);
    return Status::OK();
}

struct DecodedEnvelope {
    Status status;
    SnapshotObjectMetadata metadata;
    std::string payload;
};

std::string Sha256(const std::string &value)
{
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(), digest.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (auto byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

DecodedEnvelope DecodeEnvelope(const std::string &value)
{
    const bool version2 = value.size() >= MAGIC_SIZE && value.compare(0, MAGIC_SIZE, ENVELOPE_MAGIC) == 0;
    const bool version1 = value.size() >= MAGIC_SIZE && value.compare(0, MAGIC_SIZE, ENVELOPE_MAGIC_V1) == 0;
    if (!version1 && !version2) {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope magic"), {}, {} };
    }
    size_t offset = MAGIC_SIZE;
    uint64_t snapshotIDSize = 0;
    uint64_t version = 0;
    uint64_t payloadSize = 0;
    uint64_t shaSize = 0;
    if (!ReadUint64(value, offset, snapshotIDSize) || snapshotIDSize > value.size() - offset) {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope id"), {}, {} };
    }
    SnapshotObjectMetadata metadata;
    metadata.snapshotID = value.substr(offset, snapshotIDSize);
    offset += snapshotIDSize;
    if (!ReadUint64(value, offset, version) || !ReadUint64(value, offset, payloadSize) ||
        !ReadUint64(value, offset, shaSize) || shaSize > value.size() - offset) {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope metadata"), {}, {} };
    }
    metadata.sourceInstanceVersion = static_cast<int64_t>(version);
    metadata.size = payloadSize;
    metadata.sha256 = value.substr(offset, shaSize);
    offset += shaSize;
    if (offset >= value.size()) {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope completion marker"), {}, {} };
    }
    auto complete = value[offset++];
    if (complete != '\0' && complete != '\1') {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope completion marker"), {}, {} };
    }
    metadata.complete = complete == '\1';
    if (version2) {
        uint64_t expiresAt = 0;
        if (!ReadUint64(value, offset, expiresAt)) {
            return { Status(StatusCode::FAILED, "invalid snapshot envelope expiry metadata"), {}, {} };
        }
        metadata.expiresAtUnixSeconds = static_cast<int64_t>(expiresAt);
    }
    if (payloadSize != value.size() - offset) {
        return { Status(StatusCode::FAILED, "invalid snapshot envelope payload size"), {}, {} };
    }
    auto payload = value.substr(offset);
    if (Sha256(payload) != metadata.sha256) {
        return { Status(StatusCode::FAILED, "snapshot envelope payload sha256 mismatch"), {}, {} };
    }
    return { Status::OK(), metadata, std::move(payload) };
}

Status WritePayload(const std::string &path, const std::string &payload)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(payload.data(), payload.size());
    return output.good() ? Status::OK() : Status(StatusCode::FAILED, "failed to write staging snapshot");
}

bool LooksNotFound(const Status &status)
{
    return status.StatusCode() == StatusCode::FILE_NOT_FOUND;
}

}  // namespace

namespace detail {

std::string DataSystemObjectKey(const std::string &logicalKey)
{
    return "yr-snapshot:" + Sha256(logicalKey);
}

Status MapDataSystemStatus(const datasystem::Status &status)
{
    if (status.IsOk()) {
        return Status::OK();
    }
    return status.GetCode() == datasystem::K_NOT_FOUND
               ? Status(StatusCode::FILE_NOT_FOUND, status.ToString())
               : Status(StatusCode::BP_DATASYSTEM_ERROR, status.ToString());
}

}  // namespace detail

DataSystemSnapshotStorage::DataSystemSnapshotStorage()
    : DataSystemSnapshotStorage(std::make_shared<KVDataSystemSnapshotClient>())
{
}

DataSystemSnapshotStorage::DataSystemSnapshotStorage(std::shared_ptr<DataSystemSnapshotClient> client,
                                                     std::shared_ptr<ActorWorker> worker)
    : client_(std::move(client)), worker_(worker == nullptr ? std::make_shared<ActorWorker>() : std::move(worker))
{
}

Status DataSystemSnapshotStorage::Create(const DataSystemSnapshotConfig &config,
                                         std::shared_ptr<DataSystemSnapshotStorage> &storage,
                                         std::shared_ptr<DataSystemSnapshotClient> client)
{
    storage.reset();
    constexpr int32_t MAX_PORT = 65535;
    if (config.host.empty() || config.port <= 0 || config.port > MAX_PORT) {
        return Status(StatusCode::ERR_PARAM_INVALID, "invalid DataSystem snapshot endpoint");
    }
    if (client == nullptr) {
        client = std::make_shared<DedicatedKVDataSystemSnapshotClient>();
    }
    auto status = client->Init(config);
    if (status.IsError()) {
        return status;
    }
    storage = std::make_shared<DataSystemSnapshotStorage>(std::move(client));
    return Status::OK();
}

litebus::Future<Status> DataSystemSnapshotStorage::PutTemporary(const std::string &temporaryKey,
                                                                const std::string &sourceFile,
                                                                const SnapshotObjectMetadata &metadata)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        const auto objectKey = detail::DataSystemObjectKey(temporaryKey);
        auto current = client->Get(objectKey);
        if (current.status.IsOk()) {
            auto decoded = DecodeEnvelope(current.value);
            return decoded.status.IsOk() && detail::MetadataEqual(decoded.metadata, metadata)
                       ? Status::OK()
                       : detail::Conflict("temporary snapshot metadata conflicts");
        }
        if (!LooksNotFound(current.status)) {
            return current.status;
        }
        std::string envelope;
        auto status = EncodeEnvelope(sourceFile, metadata, envelope);
        if (status.IsError()) {
            return status;
        }
        status = client->Put(objectKey, envelope);
        if (status.IsError()) {
            return status;
        }
        auto postcondition = client->Get(objectKey);
        if (postcondition.status.IsError()) {
            return postcondition.status;
        }
        auto verified = DecodeEnvelope(postcondition.value);
        return verified.status.IsOk() && detail::MetadataEqual(verified.metadata, metadata)
                   ? Status::OK()
                   : detail::Conflict("temporary snapshot failed postcondition verification");
    });
}

litebus::Future<SnapshotStat> DataSystemSnapshotStorage::Stat(const std::string &key)
{
    return detail::RunOnWorker<SnapshotStat>(worker_, [key, client = client_]() {
        const auto objectKey = detail::DataSystemObjectKey(key);
        auto result = client->Get(objectKey);
        if (result.status.IsError()) {
            const auto status = LooksNotFound(result.status) ? Status(StatusCode::FILE_NOT_FOUND) : result.status;
            return SnapshotStat{ status, {} };
        }
        auto decoded = DecodeEnvelope(result.value);
        if (decoded.status.IsOk() && detail::IsExpired(decoded.metadata)) {
            auto deleted = client->Delete(objectKey);
            if (deleted.IsError() && !LooksNotFound(deleted)) {
                return SnapshotStat{ deleted, {} };
            }
            return SnapshotStat{ Status(StatusCode::FILE_NOT_FOUND), {} };
        }
        return SnapshotStat{ decoded.status, decoded.metadata };
    });
}

litebus::Future<Status> DataSystemSnapshotStorage::Publish(const std::string &temporaryKey, const std::string &finalKey,
                                                           const SnapshotObjectMetadata &expected)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        const auto temporaryObjectKey = detail::DataSystemObjectKey(temporaryKey);
        const auto finalObjectKey = detail::DataSystemObjectKey(finalKey);
        auto final = client->Get(finalObjectKey);
        if (final.status.IsOk()) {
            auto decoded = DecodeEnvelope(final.value);
            return decoded.status.IsOk() && detail::MetadataEqual(decoded.metadata, expected) && expected.complete
                       ? Status::OK()
                       : detail::Conflict("published snapshot metadata conflicts");
        }
        if (!LooksNotFound(final.status)) {
            return final.status;
        }
        auto temporary = client->Get(temporaryObjectKey);
        if (temporary.status.IsError()) {
            return temporary.status;
        }
        auto decoded = DecodeEnvelope(temporary.value);
        if (decoded.status.IsError() || !expected.complete ||
            !detail::MetadataIdentityEqual(decoded.metadata, expected)) {
            return detail::Conflict("temporary snapshot does not match publish metadata");
        }
        std::string envelope;
        envelope.assign(ENVELOPE_MAGIC, MAGIC_SIZE);
        AppendUint64(envelope, expected.snapshotID.size());
        envelope.append(expected.snapshotID);
        AppendUint64(envelope, static_cast<uint64_t>(expected.sourceInstanceVersion));
        AppendUint64(envelope, expected.size);
        AppendUint64(envelope, expected.sha256.size());
        envelope.append(expected.sha256);
        envelope.push_back('\1');
        AppendUint64(envelope, static_cast<uint64_t>(expected.expiresAtUnixSeconds));
        envelope.append(decoded.payload);
        auto status = client->Put(finalObjectKey, envelope);
        if (status.IsError()) {
            return status;
        }
        auto postcondition = client->Get(finalObjectKey);
        if (postcondition.status.IsError()) {
            return postcondition.status;
        }
        auto verified = DecodeEnvelope(postcondition.value);
        return verified.status.IsOk() && detail::MetadataEqual(verified.metadata, expected)
                   ? Status::OK()
                   : detail::Conflict("published snapshot failed postcondition verification");
    });
}

litebus::Future<Status> DataSystemSnapshotStorage::Get(const std::string &finalKey,
                                                       const std::string &destinationFile)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        const auto objectKey = detail::DataSystemObjectKey(finalKey);
        auto result = client->Get(objectKey);
        if (result.status.IsError()) {
            return result.status;
        }
        auto decoded = DecodeEnvelope(result.value);
        if (decoded.status.IsError() || !decoded.metadata.complete) {
            return Status(StatusCode::FAILED, "snapshot is incomplete or corrupt");
        }
        if (detail::IsExpired(decoded.metadata)) {
            auto deleted = client->Delete(objectKey);
            return deleted.IsError() && !LooksNotFound(deleted)
                       ? deleted
                       : Status(StatusCode::FILE_NOT_FOUND);
        }
        detail::SecureDownloadTarget target;
        auto prepare = target.Prepare(destinationFile);
        if (prepare.IsError()) {
            return prepare;
        }
        auto write = WritePayload(target.StagingPath(), decoded.payload);
        if (write.IsError()) {
            target.Cleanup();
            return write;
        }
        return target.Commit(decoded.metadata);
    });
}

litebus::Future<Status> DataSystemSnapshotStorage::Delete(const std::string &key)
{
    return detail::RunOnWorker<Status>(worker_, [key, client = client_]() {
        auto status = client->Delete(detail::DataSystemObjectKey(key));
        return LooksNotFound(status) ? Status::OK() : status;
    });
}

}  // namespace functionsystem::snapshot_storage
