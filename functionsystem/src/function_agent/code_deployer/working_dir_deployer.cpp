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

#include "async/uuid_generator.hpp"
#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/utils/exec_utils.h"
#include "common/utils/hash_util.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

const std::string FILE_SCHEME = "file://";
const std::string FTP_SCHEME = "ftp://";
const std::string APP_FOLDER_PREFIX = "app";
const std::string WORKING_DIR_FOLDER_PREFIX = "working_dir";

// implement it for different schema, like 'file://', 'ftp://', 'http://'
class ResourceAccessor {
public:
    virtual std::string GetResource() = 0;
    virtual std::string GetHash() = 0;
    virtual ~ResourceAccessor() {}
};

// 'file://' local
class FileResourceAccessor : public ResourceAccessor {
public:
    explicit FileResourceAccessor(const std::string &uri) : filePath_(uri)
    {
    }
    std::string GetResource() override
    {
        return (filePath_.compare(0, FILE_SCHEME.length(), FILE_SCHEME) == 0) ? filePath_.substr(FILE_SCHEME.length())
                                                                              : filePath_;
    }
    std::string GetHash() override
    {
        return CalculateFileMD5(GetResource());
    }

private:
    std::string filePath_;
};

class ResourceAccessorFactory {
public:
    // auto choose ResourceAccessor based on user input
    static std::shared_ptr<ResourceAccessor> CreateAccessor(const std::string& uri)
    {
        if (uri.find(FTP_SCHEME) == 0) {
            // not support yet
            return nullptr;
        }
        return std::make_shared<FileResourceAccessor>(uri);
    }
};

WorkingDirDeployer::WorkingDirDeployer()
{
    auto baseDir = GetDeployDir();
    std::string appDir = litebus::os::Join(baseDir, APP_FOLDER_PREFIX);
    std::string workingDir = litebus::os::Join(appDir, WORKING_DIR_FOLDER_PREFIX);
    baseDeployDir_ = workingDir;
}

std::string WorkingDirDeployer::GetDestination(
    const std::string &deployDir, const std::string &uriFile, const std::string &appID)
{
    if (appID.empty() && uriFile.empty()) {
        return "";
    }

    if (IsDir(uriFile)) {
        YRLOG_DEBUG("{}|delegate working dir is a path, use it as destination: {}", appID, uriFile);
        return uriFile;
    }

    std::shared_ptr<ResourceAccessor> accessor = ResourceAccessorFactory::CreateAccessor(uriFile);
    if (!accessor) {
        YRLOG_WARN("Unsupported working_dir schema: {}", uriFile);
        return "";
    }

    if (!IsFile(accessor->GetResource()) && uriFile.find(FILE_SCHEME) == std::string::npos
        && litebus::os::Mkdir(uriFile).IsNone()) {
        YRLOG_DEBUG("{}|delegate working dir is a non-exist path, mkdir and use it as destination: {}", appID, uriFile);
        return uriFile;
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
    YRLOG_DEBUG("{}|working dir deployer destination: {}", appID, res);
    return res;
}

bool WorkingDirDeployer::IsDeployed(const std::string &destination, [[maybe_unused]] bool isMonopoly)
{
    if (!litebus::os::ExistPath(destination)) {
        return false;
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
    std::string workingDirZipFile = accessor->GetResource();

    if (config.bucketid().empty() || config.objectid().empty()) {
        YRLOG_WARN("bucketID/codePath({}) or objectID/appID({}) is empty, skip deploy workingDir.", config.bucketid(),
                   config.objectid());
        // illegal bucket and object id, return ok and deploy directory
        result.status = Status::OK();
        return result;
    }

    // 2. create dest working dir
    if (!CheckIllegalChars(result.destination) || !litebus::os::Mkdir(result.destination).IsNone()) {
        YRLOG_ERROR("failed to create dir for workingDir({}).", result.destination);
        // failed to create directory, return 0x111ad and object directory.
        result.status = Status(StatusCode::FUNC_AGENT_MKDIR_DEST_WORKING_DIR_ERROR,
                               "failed to create dest working dir for object/appID(" + config.objectid() + "), msg: +"
                                   + litebus::os::Strerror(errno));
        return result;
    }

    // 3. unzip working dir file
    Status unzipStatus = UnzipFile(result.destination, workingDirZipFile);
    if (unzipStatus.IsError()) {
        YRLOG_ERROR("failed to unzip code for workingDirZipFile({}).", workingDirZipFile);
        // unzip failed, return error and destination directory
        result.status = unzipStatus;
        return result;
    }

    std::string cmd = "chmod -R 750 " + result.destination;
    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_WARN("failed to execute chmod cmd({}). code: {}", cmd, code);
    }
    result.status = Status::OK();
    return result;
}

bool WorkingDirDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    YRLOG_DEBUG("Clear filePath({}), objectKey({})", filePath, objectKey);
    return ClearFile(filePath, objectKey);
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