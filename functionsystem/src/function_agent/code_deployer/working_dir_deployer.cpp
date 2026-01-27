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

#include "working_dir_deployer.h"

#include <fstream>
#include <iostream>

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/exec_utils.h"
#include "common/utils/hash_util.h"
#include "function_agent/common/kv_client.h"
#include "function_agent/flags/function_agent_flags.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

const std::string FILE_SCHEME = "file://";
const std::string PATH_SCHEME = "path://";
const std::string FTP_SCHEME = "ftp://";
const std::string DS_SCHEME = "ds://";
const std::string APP_FOLDER_PREFIX = "app";
const std::string WORKING_DIR_FOLDER_PREFIX = "working_dir";

bool endsWith(const std::string &str, const std::string &suffix)
{
    if (suffix.size() > str.size())
        return false;
    return str.substr(str.size() - suffix.size()) == suffix;
}

std::string GetDirectoryPath(const std::string &path)
{
    if (path.empty()) {
        return ".";
    }
    auto pos = path.find_last_of("/");
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

// implement it for different schema, like 'file://', 'ftp://', 'http://'
class ResourceAccessor {
public:
    virtual std::pair<Status, std::string> GetResource(std::string dst) = 0;
    virtual std::string GetHash() = 0;
    virtual std::string GetWorkingDir(std::string dst) = 0;
    virtual ~ResourceAccessor()
    {
    }
};

// 'file://' local
class FileResourceAccessor : public ResourceAccessor {
public:
    explicit FileResourceAccessor(const std::string &uri) : filePath_(uri)
    {
    }

    std::pair<Status, std::string> GetResource(std::string dst) override
    {
        std::string realFilePath = (filePath_.compare(0, FILE_SCHEME.length(), FILE_SCHEME) == 0)
                                       ? filePath_.substr(FILE_SCHEME.length())
                                       : filePath_;

        if (endsWith(realFilePath, ".img")) {
            return std::make_pair(Status::OK(), realFilePath);
        } else if (endsWith(realFilePath, ".zip")) {
            Status unzipStatus = UnzipFile(dst, realFilePath);
            return std::make_pair(unzipStatus, dst);
        } else {
            return std::make_pair(
                Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "format not support " + realFilePath), "");
        }
    }

    std::string GetWorkingDir(std::string dst) override
    {
        return dst;
    }

    std::string GetHash() override
    {
        return CalculateFileMD5(filePath_);
    }

private:
    std::string filePath_;
};

// 'path://' local
class PathResourceAccessor : public ResourceAccessor {
public:
    explicit PathResourceAccessor(const std::string &uri) : filePath_(uri)
    {
    }

    std::pair<Status, std::string> GetResource(std::string dst) override
    {
        std::string realFilePath = (filePath_.compare(0, PATH_SCHEME.length(), PATH_SCHEME) == 0)
                                       ? filePath_.substr(PATH_SCHEME.length())
                                       : filePath_;
        return std::make_pair(Status::OK(), realFilePath);
    }

    std::string GetWorkingDir(std::string dst) override
    {
        return filePath_;
    }

    std::string GetHash() override
    {
        return CalculateFileMD5(filePath_);
    }

private:
    std::string filePath_;
};

// 'ds://'
class DSAccessor : public ResourceAccessor {
public:
    explicit DSAccessor(const std::string &uri) : dsKey_(uri)
    {
    }

    std::pair<Status, std::string> GetResource(std::string dst) override
    {
        auto filename = dsKey_.substr(DS_SCHEME.length());
        auto splits = litebus::strings::Split(filename, ".");
        auto [s, buffer] = function_agent::KVClient::GetInstance().Get(splits[0]);
        if (!s.OK()) {
            YRLOG_WARN("failed to get dsKey {}, err: {}", filename, s.ToString());
            return std::make_pair(s, "");
        }
        if (buffer.GetSize() == 0) {
            YRLOG_WARN("{} buffer size is 0", filename);
            return std::make_pair(
                Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "invalid package size with " + dsKey_), "");
        }
        auto destinationPath = dst;
        if (endsWith(dst, ".img")) {
            destinationPath = GetDirectoryPath(dst);
        }
        std::string fullpath = litebus::os::Join(destinationPath, filename);
        std::ofstream file(fullpath, std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            YRLOG_WARN("failed to open {}", filename);
            return std::make_pair(
                Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "Failed to open file: " + fullpath), "");
        }
        file.write(static_cast<const char *>(buffer.ImmutableData()), buffer.GetSize());
        if (file.fail()) {
            YRLOG_WARN("failed to write {}", filename);
            return std::make_pair(
                Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "Failed to write file: " + fullpath), "");
        }
        file.close();

        if (endsWith(dsKey_, ".img")) {
            return std::make_pair(Status::OK(), fullpath);
        } else if (endsWith(dsKey_, ".zip")) {
            Status unzipStatus = UnzipFile(dst, fullpath);
            return std::make_pair(unzipStatus, dst);
        } else {
            YRLOG_WARN("format not support {}", dsKey_);
            return std::make_pair(
                Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "format not support " + dsKey_), "");
        }
        return std::make_pair(Status::OK(), fullpath);
    }

    std::string GetWorkingDir(std::string dst) override
    {
        if (endsWith(dsKey_, ".img")) {
            return litebus::os::Join(dst, dsKey_.substr(DS_SCHEME.length()));
        }
        return dst;
    }

    std::string GetHash() override
    {
        return GetHashString(dsKey_);
    }

private:
    std::string dsKey_;
};

class ResourceAccessorFactory {
public:
    // auto choose ResourceAccessor based on user input
    static std::shared_ptr<ResourceAccessor> CreateAccessor(const std::string &uri)
    {
        if (uri.find(FTP_SCHEME) == 0) {
            // not support yet
            return nullptr;
        }
        if (uri.find(DS_SCHEME) == 0) {
            return std::make_shared<DSAccessor>(uri);
        }
        if (uri.find(FILE_SCHEME) == 0) {
            return std::make_shared<FileResourceAccessor>(uri);
        }
        if (uri.find(PATH_SCHEME) == 0) {
            return std::make_shared<PathResourceAccessor>(uri);
        }
        if (IsDir(uri)) {
            return std::make_shared<PathResourceAccessor>(uri);
        }
        return nullptr;
    }
};

WorkingDirDeployer::WorkingDirDeployer()
{
    auto baseDir = GetDeployDir();
    std::string appDir = litebus::os::Join(baseDir, APP_FOLDER_PREFIX);
    std::string workingDir = litebus::os::Join(appDir, WORKING_DIR_FOLDER_PREFIX);
    baseDeployDir_ = workingDir;
}

std::string WorkingDirDeployer::GetDestination(const std::string &deployDir, const std::string &uriFile,
                                               const std::string &appID)
{
    if (appID.empty() && uriFile.empty()) {
        return "";
    }

    std::shared_ptr<ResourceAccessor> accessor = ResourceAccessorFactory::CreateAccessor(uriFile);
    if (!accessor) {
        YRLOG_WARN("Unsupported working_dir schema: {}", uriFile);
        return "";
    }

    std::string workingDir;
    if (!deployDir.empty()) {
        std::string appDir = litebus::os::Join(deployDir, APP_FOLDER_PREFIX);
        workingDir = litebus::os::Join(appDir, WORKING_DIR_FOLDER_PREFIX);
    } else {
        workingDir = baseDeployDir_;
    }

    // baseDir + /app/working_dir/${md5 working_dir uri file}/
    std::string hash = accessor->GetHash();
    YRLOG_DEBUG("md5 of workingDirZipFile({}): {}", uriFile, hash);
    if (hash.empty()) {
        return hash;
    }
    auto res = litebus::os::Join(workingDir, hash);
    res = accessor->GetWorkingDir(res);
    YRLOG_DEBUG("{}|working dir deployer destination: {}", appID, res);
    return res;
}

bool WorkingDirDeployer::IsDeployed(const std::string &destination, [[maybe_unused]] bool isMonopoly)
{
    if (!litebus::os::ExistPath(destination)) {
        return false;
    }
    if (endsWith(destination, ".img")) {
        return true;
    }
    auto option = litebus::os::Ls(destination);
    if (option.IsSome() && !option.Get().empty()) {
        return true;
    }
    return false;
}

DeployResult WorkingDirDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    // 'working_dir' storage type objectid (src appID = instanceID)
    //                            bucketid (src codePath, working dir zip file or delegated working dir)
    auto &config = request->deploymentconfig();
    DeployResult result;
    result.destination = GetDestination(config.deploydir(), config.bucketid(), config.objectid());
    YRLOG_DEBUG(
        "WorkingDir deployer received Deploy request to directory({}), workingDirZipFile({}), appID({}), "
        "destination({})",
        config.deploydir(), config.bucketid(), config.objectid(), result.destination);

    if (result.destination == config.bucketid()) {
        result.status = Status::OK();
        return result;
    }

    // 1. verify input user params
    std::shared_ptr<ResourceAccessor> accessor =
        ResourceAccessorFactory::CreateAccessor(config.bucketid());  // like: "file:///home/xxx/xxy.zip"
    if (!accessor) {
        YRLOG_WARN("Unsupported working_dir schema: {}", config.bucketid());
        result.status = Status(StatusCode::FUNC_AGENT_UNSUPPORTED_WORKING_DIR_SCHEMA,
                               "Unsupported working_dir schema: " + config.objectid());
        return result;
    }
    auto dst = result.destination;
    if (endsWith(dst, ".img")) {
        dst = GetDirectoryPath(dst);
    }
    // 2. create dest working dir
    if (!CheckIllegalChars(dst) || !litebus::os::Mkdir(dst).IsNone()) {
        YRLOG_ERROR("failed to create dir for workingDir({}).", dst);
        // failed to create directory, return 0x111ad and object directory.
        result.status =
            Status(StatusCode::FUNC_AGENT_MKDIR_DEST_WORKING_DIR_ERROR,
                   "failed to create dest working dir for " + dst + ", msg: +" + litebus::os::Strerror(errno));
        return result;
    }
    std::string cmd = "chmod -R 750 " + dst;
    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_WARN("failed to execute chmod cmd({}). code: {}", cmd, code);
    }
    auto [status, workingDirZipFile] = accessor->GetResource(result.destination);
    if (!status.IsOk()) {
        result.status = status;
        return result;
    }

    YRLOG_DEBUG("working dir: {}", workingDirZipFile);

    result.status = Status::OK();
    return result;
}

bool WorkingDirDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    YRLOG_DEBUG("Clear filePath({}), objectKey({})", filePath, objectKey);
    std::string needsClearPath = filePath;
    if (endsWith(filePath, ".img")) {
        needsClearPath = GetDirectoryPath(filePath);
    }
    return ClearFile(needsClearPath, objectKey);
}

Status WorkingDirDeployer::UnzipFile(const std::string &destDir, const std::string &workingDirZipFile)
{
    if (!IsFile(workingDirZipFile)) {
        return Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "working_dir file is invalid");
    }
    // baseDir + /app/working_dir/${hash working_dir uri file}/
    std::string cmd = "unzip -d " + destDir + " " + workingDirZipFile;
    if (!CheckIllegalChars(cmd)) {
        return Status(StatusCode::PARAMETER_ERROR, "command has invalid characters");
    }

    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_ERROR("failed to execute unzip working_dir cmd({}). code: {}", cmd, code);
        return Status(StatusCode::FUNC_AGENT_INVALID_WORKING_DIR_FILE, "failed to unzip working_dir file");
    }
    // keep origin workingDirZipFile
    return Status::OK();
}

}  // namespace functionsystem::function_agent