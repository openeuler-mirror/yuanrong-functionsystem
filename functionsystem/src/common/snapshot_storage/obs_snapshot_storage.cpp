/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "common/snapshot_storage/obs_snapshot_storage.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>

#include "eSDKOBS.h"

namespace functionsystem::snapshot_storage {
namespace {

constexpr uint64_t MULTIPART_SIZE = 5ULL * 1024ULL * 1024ULL;
constexpr char META_SNAPSHOT_ID[] = "yr-snapshot-id";
constexpr char META_SOURCE_VERSION[] = "yr-source-instance-version";
constexpr char META_SHA256[] = "yr-sha256";
constexpr char META_COMPLETE[] = "yr-complete";
constexpr char META_EXPIRES_AT[] = "yr-expires-at-unix-seconds";

Status FromObsStatus(obs_status status, const std::string &operation)
{
    if (status == OBS_STATUS_OK) {
        return Status::OK();
    }
    if (status == OBS_STATUS_NoSuchKey || status == OBS_STATUS_NoSuchVersion
        || status == OBS_STATUS_HttpErrorNotFound) {
        return Status(StatusCode::FILE_NOT_FOUND, operation + ": " + obs_get_status_name(status));
    }
    if (status == OBS_STATUS_PreconditionFailed) {
        return detail::Conflict(operation + ": " + obs_get_status_name(status));
    }
    return Status(StatusCode::FAILED, operation + ": " + obs_get_status_name(status));
}

obs_status InitializeObsOnce()
{
    static std::once_flag once;
    static obs_status result = OBS_STATUS_BUTT;
    std::call_once(once, []() { result = obs_initialize(OBS_INIT_ALL); });
    return result;
}

void InitOptions(const ObsSnapshotConfig &config, obs_options &options)
{
    init_obs_options(&options);
    options.bucket_options.host_name = const_cast<char *>(config.endpoint.c_str());
    options.bucket_options.bucket_name = const_cast<char *>(config.bucket.c_str());
    options.bucket_options.access_key = const_cast<char *>(config.accessKey.c_str());
    options.bucket_options.secret_access_key = const_cast<char *>(config.secretKey.c_str());
    options.bucket_options.token =
        config.securityToken.empty() ? nullptr : const_cast<char *>(config.securityToken.c_str());
    options.bucket_options.protocol = config.useHttps ? OBS_PROTOCOL_HTTPS : OBS_PROTOCOL_HTTP;
    if (config.pathStyle) {
        options.bucket_options.uri_style = OBS_URI_STYLE_PATH;
    }
}

struct MetadataProperties {
    explicit MetadataProperties(const SnapshotObjectMetadata &metadata)
        : sourceVersion(std::to_string(metadata.sourceInstanceVersion)), complete(metadata.complete ? "true" : "false"),
          expiresAt(std::to_string(metadata.expiresAtUnixSeconds))
    {
        names = { META_SNAPSHOT_ID, META_SOURCE_VERSION, META_SHA256, META_COMPLETE, META_EXPIRES_AT };
        values = { metadata.snapshotID, sourceVersion, metadata.sha256, complete, expiresAt };
        entries.resize(names.size());
        for (size_t index = 0; index < names.size(); ++index) {
            entries[index] = { names[index].data(), values[index].data() };
        }
        init_put_properties(&properties);
        properties.meta_data_count = static_cast<int>(entries.size());
        properties.meta_data = entries.data();
    }

    std::string sourceVersion;
    std::string complete;
    std::string expiresAt;
    std::vector<std::string> names;
    std::vector<std::string> values;
    std::vector<obs_name_value> entries;
    obs_put_properties properties{};
};

void CompleteCallback(obs_status status, const obs_error_details *, void *callbackData)
{
    *static_cast<obs_status *>(callbackData) = status;
}

obs_status IgnoreProperties(const obs_response_properties *, void *)
{
    return OBS_STATUS_OK;
}

void UploadFileCallback(obs_status status, char *, int, obs_upload_file_part_info *, void *callbackData)
{
    *static_cast<obs_status *>(callbackData) = status;
}

struct HeadCallbackData {
    obs_status status{ OBS_STATUS_BUTT };
    uint64_t size{ 0 };
    std::string etag;
    std::map<std::string, std::string> metadata;
};

obs_status HeadProperties(const obs_response_properties *properties, void *callbackData)
{
    auto *data = static_cast<HeadCallbackData *>(callbackData);
    data->size = properties->content_length;
    data->etag = properties->etag == nullptr ? "" : properties->etag;
    for (int index = 0; index < properties->meta_data_count; ++index) {
        data->metadata[properties->meta_data[index].name] = properties->meta_data[index].value;
    }
    return OBS_STATUS_OK;
}

void HeadComplete(obs_status status, const obs_error_details *, void *callbackData)
{
    static_cast<HeadCallbackData *>(callbackData)->status = status;
}

struct DownloadCallbackData {
    FILE *file{ nullptr };
    obs_status status{ OBS_STATUS_BUTT };
};

obs_status DownloadData(int size, const char *buffer, void *callbackData)
{
    auto *data = static_cast<DownloadCallbackData *>(callbackData);
    return fwrite(buffer, 1, static_cast<size_t>(size), data->file) == static_cast<size_t>(size)
               ? OBS_STATUS_OK
               : OBS_STATUS_AbortedByCallback;
}

void DownloadComplete(obs_status status, const obs_error_details *, void *callbackData)
{
    static_cast<DownloadCallbackData *>(callbackData)->status = status;
}

std::string MetadataValue(const HeadCallbackData &data, const std::string &name)
{
    auto iter = data.metadata.find(name);
    return iter == data.metadata.end() ? "" : iter->second;
}

}  // namespace

HuaweiObsSnapshotClient::HuaweiObsSnapshotClient(ObsSnapshotConfig config) : config_(std::move(config))
{
}

Status HuaweiObsSnapshotClient::MultipartUpload(const std::string &key, const std::string &sourceFile,
                                                 const SnapshotObjectMetadata &metadata)
{
    auto initialized = InitializeObsOnce();
    if (initialized != OBS_STATUS_OK) {
        return FromObsStatus(initialized, "initialize OBS");
    }
    obs_options options;
    InitOptions(config_, options);
    MetadataProperties metadataProperties(metadata);
    int pause = 0;
    obs_upload_file_configuration upload{};
    upload.upload_file = const_cast<char *>(sourceFile.c_str());
    upload.part_size = MULTIPART_SIZE;
    upload.enable_check_point = 0;
    upload.task_num = 1;
    upload.pause_upload_flag = &pause;
    upload.put_properties = &metadataProperties.properties;
    obs_upload_file_server_callback serverCallback;
    init_server_callback(&serverCallback);
    obs_status result = OBS_STATUS_BUTT;
    obs_upload_file_response_handler handler = { { IgnoreProperties, CompleteCallback }, UploadFileCallback, nullptr };
    upload_file(&options, const_cast<char *>(key.c_str()), nullptr, &upload, serverCallback, &handler, &result);
    return FromObsStatus(result, "multipart upload");
}

ObsHeadResult HuaweiObsSnapshotClient::Head(const std::string &key)
{
    auto initialized = InitializeObsOnce();
    if (initialized != OBS_STATUS_OK) {
        return { FromObsStatus(initialized, "initialize OBS"), {}, {} };
    }
    obs_options options;
    InitOptions(config_, options);
    obs_object_info object{ const_cast<char *>(key.c_str()), nullptr };
    HeadCallbackData data;
    obs_response_handler handler = { HeadProperties, HeadComplete };
    get_object_metadata(&options, &object, nullptr, &handler, &data);
    auto status = FromObsStatus(data.status, "HEAD snapshot");
    if (status.IsError()) {
        return { status, {}, {} };
    }
    SnapshotObjectMetadata metadata;
    metadata.snapshotID = MetadataValue(data, META_SNAPSHOT_ID);
    metadata.sha256 = MetadataValue(data, META_SHA256);
    metadata.size = data.size;
    auto version = MetadataValue(data, META_SOURCE_VERSION);
    auto complete = MetadataValue(data, META_COMPLETE);
    auto expiresAt = MetadataValue(data, META_EXPIRES_AT);
    try {
        metadata.sourceInstanceVersion = std::stoll(version);
        metadata.expiresAtUnixSeconds = expiresAt.empty() ? 0 : std::stoll(expiresAt);
    } catch (const std::exception &) {
        return { Status(StatusCode::FAILED, "invalid OBS snapshot version metadata"), {}, data.etag };
    }
    metadata.complete = complete == "true";
    if (metadata.snapshotID.empty() || metadata.sha256.empty() || (complete != "true" && complete != "false")) {
        return { Status(StatusCode::FAILED, "incomplete OBS snapshot metadata"), {}, data.etag };
    }
    return { Status::OK(), metadata, data.etag };
}

Status HuaweiObsSnapshotClient::ConditionalCopy(const std::string &temporaryKey, const std::string &finalKey,
                                                 const std::string &expectedTemporaryETag,
                                                 const SnapshotObjectMetadata &metadata)
{
    auto initialized = InitializeObsOnce();
    if (initialized != OBS_STATUS_OK) {
        return FromObsStatus(initialized, "initialize OBS");
    }
    obs_options options;
    InitOptions(config_, options);
    MetadataProperties metadataProperties(metadata);
    obs_get_conditions conditions;
    init_get_properties(&conditions);
    conditions.if_match_etag = const_cast<char *>(expectedTemporaryETag.c_str());
    metadataProperties.properties.get_conditions = &conditions;
    metadataProperties.properties.metadata_action = OBS_REPLACE;
    obs_copy_destination_object_info destination{};
    destination.destination_bucket = const_cast<char *>(config_.bucket.c_str());
    destination.destination_key = const_cast<char *>(finalKey.c_str());
    obs_status result = OBS_STATUS_BUTT;
    obs_response_handler handler = { IgnoreProperties, CompleteCallback };
    // A non-zero is_copy makes Huawei's C SDK clear meta_data_count and
    // silently inherit the temporary object's incomplete metadata.
    copy_object(&options, const_cast<char *>(temporaryKey.c_str()), nullptr, &destination, 0,
                &metadataProperties.properties, nullptr, &handler, &result);
    return FromObsStatus(result, "conditional snapshot copy");
}

Status HuaweiObsSnapshotClient::Download(const std::string &key, const std::string &destinationFile)
{
    auto initialized = InitializeObsOnce();
    if (initialized != OBS_STATUS_OK) {
        return FromObsStatus(initialized, "initialize OBS");
    }
    FILE *file = fopen(destinationFile.c_str(), "wb");
    if (file == nullptr) {
        return Status(StatusCode::FAILED, "failed to open OBS download staging file");
    }
    obs_options options;
    InitOptions(config_, options);
    obs_object_info object{ const_cast<char *>(key.c_str()), nullptr };
    obs_get_conditions conditions;
    init_get_properties(&conditions);
    DownloadCallbackData data{ file, OBS_STATUS_BUTT };
    obs_get_object_handler handler = { { IgnoreProperties, DownloadComplete }, DownloadData };
    get_object(&options, &object, &conditions, nullptr, &handler, &data);
    if (fclose(file) != 0 && data.status == OBS_STATUS_OK) {
        return Status(StatusCode::FAILED, "failed to close OBS download staging file");
    }
    return FromObsStatus(data.status, "download snapshot");
}

Status HuaweiObsSnapshotClient::Delete(const std::string &key)
{
    auto initialized = InitializeObsOnce();
    if (initialized != OBS_STATUS_OK) {
        return FromObsStatus(initialized, "initialize OBS");
    }
    obs_options options;
    InitOptions(config_, options);
    obs_object_info object{ const_cast<char *>(key.c_str()), nullptr };
    obs_status result = OBS_STATUS_BUTT;
    obs_response_handler handler = { IgnoreProperties, CompleteCallback };
    delete_object(&options, &object, &handler, &result);
    auto status = FromObsStatus(result, "delete snapshot");
    return status.StatusCode() == StatusCode::FILE_NOT_FOUND ? Status::OK() : status;
}

ObsSnapshotStorage::ObsSnapshotStorage(const ObsSnapshotConfig &config, std::shared_ptr<ActorWorker> worker)
    : ObsSnapshotStorage(std::make_shared<HuaweiObsSnapshotClient>(config), std::move(worker))
{
}

ObsSnapshotStorage::ObsSnapshotStorage(std::shared_ptr<ObsSnapshotClient> client, std::shared_ptr<ActorWorker> worker)
    : client_(std::move(client)), worker_(worker == nullptr ? std::make_shared<ActorWorker>() : std::move(worker))
{
}

litebus::Future<Status> ObsSnapshotStorage::PutTemporary(const std::string &temporaryKey,
                                                         const std::string &sourceFile,
                                                         const SnapshotObjectMetadata &metadata)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        auto current = client->Head(temporaryKey);
        if (current.status.IsOk()) {
            return detail::MetadataEqual(current.metadata, metadata)
                       ? Status::OK()
                       : detail::Conflict("temporary OBS snapshot metadata conflicts");
        }
        if (current.status.StatusCode() != StatusCode::FILE_NOT_FOUND) {
            return current.status;
        }
        auto validation = detail::ValidateFile(sourceFile, metadata);
        if (validation.IsError()) {
            return validation;
        }
        auto upload = client->MultipartUpload(temporaryKey, sourceFile, metadata);
        if (upload.IsError()) {
            return upload;
        }
        auto verified = client->Head(temporaryKey);
        return verified.status.IsOk() && detail::MetadataEqual(verified.metadata, metadata)
                   ? Status::OK()
                   : detail::Conflict("temporary OBS snapshot failed postcondition verification");
    });
}

litebus::Future<SnapshotStat> ObsSnapshotStorage::Stat(const std::string &key)
{
    return detail::RunOnWorker<SnapshotStat>(worker_, [key, client = client_]() {
        auto result = client->Head(key);
        if (result.status.IsOk() && detail::IsExpired(result.metadata)) {
            auto deleted = client->Delete(key);
            if (deleted.IsError() && deleted.StatusCode() != StatusCode::FILE_NOT_FOUND) {
                return SnapshotStat{ deleted, {} };
            }
            return SnapshotStat{ Status(StatusCode::FILE_NOT_FOUND), {} };
        }
        return SnapshotStat{ result.status, result.metadata };
    });
}

litebus::Future<Status> ObsSnapshotStorage::Publish(const std::string &temporaryKey, const std::string &finalKey,
                                                    const SnapshotObjectMetadata &expected)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        auto final = client->Head(finalKey);
        if (final.status.IsOk()) {
            return expected.complete && detail::MetadataEqual(final.metadata, expected)
                       ? Status::OK()
                       : detail::Conflict("published OBS snapshot metadata conflicts");
        }
        if (final.status.StatusCode() != StatusCode::FILE_NOT_FOUND) {
            return final.status;
        }
        auto temporary = client->Head(temporaryKey);
        if (temporary.status.IsError()) {
            return temporary.status;
        }
        if (!expected.complete || !detail::MetadataIdentityEqual(temporary.metadata, expected)) {
            return detail::Conflict("temporary OBS snapshot does not match publish metadata");
        }
        auto copy = client->ConditionalCopy(temporaryKey, finalKey, temporary.etag, expected);
        if (copy.IsError()) {
            return copy;
        }
        auto verified = client->Head(finalKey);
        return verified.status.IsOk() && detail::MetadataEqual(verified.metadata, expected)
                   ? Status::OK()
                   : detail::Conflict("published OBS snapshot failed postcondition verification");
    });
}

litebus::Future<Status> ObsSnapshotStorage::Get(const std::string &finalKey, const std::string &destinationFile)
{
    return detail::RunOnWorker<Status>(worker_, [=, client = client_]() {
        auto object = client->Head(finalKey);
        if (object.status.IsError()) {
            return object.status;
        }
        if (detail::IsExpired(object.metadata)) {
            auto deleted = client->Delete(finalKey);
            return deleted.IsError() && deleted.StatusCode() != StatusCode::FILE_NOT_FOUND
                       ? deleted
                       : Status(StatusCode::FILE_NOT_FOUND);
        }
        if (!object.metadata.complete) {
            return Status(StatusCode::FAILED, "OBS snapshot is incomplete");
        }
        detail::SecureDownloadTarget target;
        auto prepare = target.Prepare(destinationFile);
        if (prepare.IsError()) {
            return prepare;
        }
        auto download = client->Download(finalKey, target.StagingPath());
        if (download.IsError()) {
            target.Cleanup();
            return download;
        }
        return target.Commit(object.metadata);
    });
}

litebus::Future<Status> ObsSnapshotStorage::Delete(const std::string &key)
{
    return detail::RunOnWorker<Status>(worker_, [key, client = client_]() {
        auto status = client->Delete(key);
        return status.StatusCode() == StatusCode::FILE_NOT_FOUND ? Status::OK() : status;
    });
}

}  // namespace functionsystem::snapshot_storage
