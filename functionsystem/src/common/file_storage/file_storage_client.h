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

#pragma once

#include <string>
#include "common/status/status.h"

namespace functionsystem::file_storage {

/**
 * FileStorageClient provides file upload/download functionality based on KVClient
 */
class FileStorageClient {
public:
    FileStorageClient() = default;
    ~FileStorageClient() = default;

    /**
     * Upload a file to storage with specified key
     * @param key Storage key for the file
     * @param filePath Local file path to upload
     * @return Status indicating success or failure
     */
    Status UploadFile(const std::string &key, const std::string &filePath);

    /**
     * Download a file from storage with specified key
     * @param key Storage key for the file
     * @param filePath Local file path to save downloaded content
     * @return Status indicating success or failure
     */
    Status DownloadFile(const std::string &key, const std::string &filePath);

    /**
     * Delete a file from storage with specified key
     * @param key Storage key for the file
     * @return Status indicating success or failure
     */
    Status DeleteFile(const std::string &key);

private:
    /**
     * Read file content into buffer
     * @param filePath File path to read
     * @param content Output buffer for file content
     * @return Status indicating success or failure
     */
    Status ReadFileContent(const std::string &filePath, std::string &content);

    /**
     * Write content to file
     * @param filePath File path to write
     * @param content Content to write
     * @return Status indicating success or failure
     */
    Status WriteFileContent(const std::string &filePath, const std::string &content);
};

}  // namespace functionsystem::file_storage
