/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "file_storage_client.h"

#include <fstream>
#include <sstream>

#include "common/kv_client/kv_client.h"
#include "common/logs/logging.h"
#include "datasystem/datasystem.h"

namespace functionsystem::file_storage {

Status FileStorageClient::UploadFile(const std::string &key, const std::string &filePath)
{
    YRLOG_INFO("uploading file: {} with key: {}", filePath, key);

    // Read file content
    std::string content;
    Status status = ReadFileContent(filePath, content);
    if (!status.IsOk()) {
        YRLOG_ERROR("failed to read file content from: {}", filePath);
        return status;
    }

    // Upload to KV storage
    status = KVClient::GetInstance().Put(key, content);
    if (!status.IsOk()) {
        YRLOG_ERROR("failed to upload file to KV storage with key: {}, error: {}", key, status.GetMessage());
        return status;
    }

    YRLOG_INFO("successfully uploaded file: {} with key: {}, size: {} bytes", filePath, key, content.size());
    return Status::OK();
}

Status FileStorageClient::DownloadFile(const std::string &key, const std::string &filePath)
{
    YRLOG_INFO("downloading file with key: {} to: {}", key, filePath);

    // Download from KV storage
    auto [status, buffer] = KVClient::GetInstance().Get(key);
    if (!status.IsOk()) {
        YRLOG_ERROR("failed to download file from KV storage with key: {}, error: {}", key, status.GetMessage());
        return status;
    }

    // Write to file
    std::string content(static_cast<const char *>(buffer.ImmutableData()), buffer.GetSize());
    status = WriteFileContent(filePath, content);
    if (!status.IsOk()) {
        YRLOG_ERROR("failed to write file content to: {}", filePath);
        return status;
    }

    YRLOG_INFO("successfully downloaded file with key: {} to: {}, size: {} bytes", key, filePath, buffer.GetSize());
    return Status::OK();
}

Status FileStorageClient::DeleteFile(const std::string &key)
{
    YRLOG_INFO("deleting file with key: {}", key);

    Status status = KVClient::GetInstance().Delete(key);
    if (!status.IsOk()) {
        YRLOG_ERROR("failed to delete file from KV storage with key: {}, error: {}", key, status.GetMessage());
        return status;
    }

    YRLOG_INFO("successfully deleted file with key: {}", key);
    return Status::OK();
}

Status FileStorageClient::ReadFileContent(const std::string &filePath, std::string &content)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Status(StatusCode::FAILED, "failed to open file: " + filePath);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    content.resize(size);
    if (!file.read(content.data(), size)) {
        return Status(StatusCode::FAILED, "failed to read file content: " + filePath);
    }

    file.close();
    return Status::OK();
}

Status FileStorageClient::WriteFileContent(const std::string &filePath, const std::string &content)
{
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Status(StatusCode::FAILED, "failed to open file for writing: " + filePath);
    }

    if (!file.write(content.data(), content.size())) {
        file.close();
        return Status(StatusCode::FAILED, "failed to write file content: " + filePath);
    }

    file.close();
    return Status::OK();
}

}  // namespace functionsystem::file_storage
