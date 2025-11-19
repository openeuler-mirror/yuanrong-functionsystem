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

#include "remote_deployer.h"

#include <sys/stat.h>

#include <utility>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "common/utils/exec_utils.h"
#include "common/utils/ssl_config.h"
#include "minizip/unzip.h"
#include "utils/os_utils.hpp"

namespace functionsystem::function_agent {

const int SHA256_VALUE_LEN = 32;
const int SHA512_VALUE_LEN = 64;
const int CHAR_TO_HEX_LEN = 2;
const size_t UNZIPINFO_HEADER_LEN = 2;
const size_t CMD_OUTPUT_MAX_LEN = 1024 * 1024 * 10;
const uint32_t ZIP_FILE_BUFFER = 200;

RemoteDeployer::RemoteDeployer(messages::CodePackageThresholds codePackageThresholds, bool enableSignatureValidation)
    : codePackageThresholds_(std::move(codePackageThresholds)), enableSignatureValidation_(enableSignatureValidation)
{
    unzipFileSizeMaxBytes_ = static_cast<uint64_t>(codePackageThresholds_.unzipfilesizemaxmb()) * SIZE_MEGA_BYTES;
    // process overflow, set to default
    if (unzipFileSizeMaxBytes_ / SIZE_MEGA_BYTES !=
        static_cast<uint64_t>(codePackageThresholds_.unzipfilesizemaxmb())) {
        unzipFileSizeMaxBytes_ = function_agent::UNZIP_FILE_SIZE_MAX_MB;
    }
}

std::string RemoteDeployer::GetDestination(const std::string &deployDir, const std::string &bucketID,
                                           const std::string &objectID)
{
    std::string layerDir = litebus::os::Join(deployDir, "layer");
    std::string funcDir = litebus::os::Join(layerDir, "func");

    std::string bucketDir = litebus::os::Join(funcDir, bucketID);
    return litebus::os::Join(bucketDir, TransMultiLevelDirToSingle(objectID));
}

bool RemoteDeployer::IsDeployed(const std::string &destination, bool isMonopoly)
{
    if (!litebus::os::ExistPath(destination)) {
        return false;
    }
    // if single-pod for multi-function and the directory exists, function has been deployed.
    if (!isMonopoly) {
        return true;
    }
    auto option = litebus::os::Ls(destination);
    if (option.IsSome() && !option.Get().empty()) {
        return true;
    }
    return false;
}

void RemoteDeployer::CollectFunctionInfo(const std::string &destFile,
                                         const std::shared_ptr<messages::DeployRequest> &request)
{
    const std::string &objectId = request->mutable_deploymentconfig()->objectid();
    struct stat statBuf {};
    if (stat(destFile.c_str(), &statBuf) == -1) {
        YRLOG_ERROR("failed to get function package info for object({}) with the path({})", objectId, destFile);
        return;
    }
    struct functionsystem::metrics::MeterData data {
        static_cast<double>(statBuf.st_size),
        {
            { "instanceId", request->instanceid() },
            {
                "objectId", objectId
            }
        }
    };

    struct functionsystem::metrics::MeterTitle functionPackageTitle {
        "yr_app_user_code_pkg_size", "function code package size in node", "Byte"
    };

    functionsystem::metrics::MetricsAdapter::GetInstance().ReportGauge(functionPackageTitle, data);
}

std::string RemoteDeployer::GetFileNameFromObjectID(const std::string &objectID)
{
    auto items = litebus::strings::Split(objectID, "/");
    if (items.empty()) {
        return objectID;
    }
    return items[items.size() - 1];
}

DeployResult RemoteDeployer::Deploy(const std::shared_ptr<messages::DeployRequest> &request)
{
    const ::messages::DeploymentConfig &config = request->deploymentconfig();
    YRLOG_DEBUG("remote deployer received Deploy request to directory {}, bucketID {} , objectID {}",
        config.deploydir(), config.bucketid(), config.objectid());

    DeployResult result;
    result.destination = config.deploydir();
    if (config.bucketid().empty() && config.objectid().empty()) {
        YRLOG_WARN("bucketID and objectID is empty, skip download");
        // 1. illegal bucket and object id, return ok and deploy directory.
        result.status = Status::OK();
        return result;
    }

    std::string objectDir = GetDestination(config.deploydir(), config.bucketid(), config.objectid());
    if (request->schedpolicyname() != MONOPOLY_SCHEDULE) {
        result.destination = objectDir;
    }

    // dcache/layer/func/bucket_id/object_id-tmp/ or home/layer/bucket_id/object_id-tmp/
    std::string objectTmpDir = objectDir + "-tmp";
    // When NONE, the directory is created successfully.
    if (!CheckIllegalChars(result.destination) || !CheckIllegalChars(objectTmpDir) ||
        !litebus::os::Mkdir(objectTmpDir).IsNone() || !litebus::os::Mkdir(result.destination).IsNone()) {
        YRLOG_ERROR("failed to create dir for object({}).", config.objectid());
        // 2. failed to create directory, return 0x111ad and object directory.
        result.status =
            Status(StatusCode::FUNC_AGENT_OBS_OPEN_FILE_ERROR,
                   "failed to create dir for object(" + config.objectid() + "), msg: +" + litebus::os::Strerror(errno));
        return result;
    }

    auto tmpDirRealpathOption = litebus::os::RealPath(objectTmpDir);
    if (tmpDirRealpathOption.IsNone()) {
        (void)litebus::os::Rmdir(objectTmpDir);
        YRLOG_ERROR("failed to get realpath of tmp dir for object({}).", config.objectid());
        // 3. failed to create tmp directory, return 0x111ad and object directory.
        result.status = Status(StatusCode::FUNC_AGENT_OBS_OPEN_FILE_ERROR,
                               "failed to create dir for object(" + config.objectid() + ").");
        return result;
    }
    const auto &objectTmpDirRealpath = tmpDirRealpathOption.Get();

    std::string objectFile = litebus::os::Join(objectTmpDirRealpath, GetFileNameFromObjectID(config.objectid()));
    Status downloadStatus = DownloadCode(objectFile, config);

    YRLOG_DEBUG(
        "codePackageThresholds: filecountsmax({}), zipfilesizemaxmb({}), unzipfilesizemaxmb({}), dirdepthmax({})",
        codePackageThresholds_.filecountsmax(), codePackageThresholds_.zipfilesizemaxmb(),
        codePackageThresholds_.unzipfilesizemaxmb(), codePackageThresholds_.dirdepthmax());

    if (downloadStatus.IsError()) {
        // Logs have been recorded in DownloadCode. Not need to record logs again.
        (void)litebus::os::Rmdir(objectTmpDirRealpath);
        // 4. download failed, return error and object directory.
        result.status = downloadStatus;
        return result;
    }

    Status contentStatus = PackageValidation(objectFile, config.sha512(), config.sha256());
    if (contentStatus.IsError()) {
        (void)litebus::os::Rmdir(objectTmpDirRealpath);
        // 5. consistency check failed, return error and object directory.
        result.status = contentStatus;
        return result;
    }

    CollectFunctionInfo(objectFile, request);

    Status unzipStatus = UnzipFile(result.destination, objectFile);
    if (unzipStatus.IsError()) {
        YRLOG_ERROR("failed to unzip code for object({}).", config.objectid());
        (void)litebus::os::Rmdir(objectTmpDirRealpath);
        // 6. unzip check failed, return error and object directory.
        result.status = unzipStatus;
        return result;
    }

    std::string cmd = "chmod -R 750 " + result.destination;
    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_WARN("failed to execute chmod cmd({}). code: {}", cmd, code);
    }

    (void)litebus::os::Rmdir(objectTmpDirRealpath);
    result.status = Status::OK();
    return result;
}

bool RemoteDeployer::Clear(const std::string &filePath, const std::string &objectKey)
{
    return ClearFile(filePath, objectKey);
}

Status RemoteDeployer::UnzipFile(const std::string &destDir, const std::string &destFile)
{
    // home/layer/func/bucket id/object_id-tmp(directory)/object_id(file)
    std::string cmd = "unzip -d " + destDir + " " + destFile;
    if (!CheckIllegalChars(cmd)) {
        return Status(StatusCode::PARAMETER_ERROR, "command has invalid characters");
    }

    if (auto code(std::system(cmd.c_str())); code) {
        YRLOG_ERROR("failed to execute unzip cmd({}). code: {}", cmd, code);
        return Status(StatusCode::FUNC_AGENT_OBS_OPEN_FILE_ERROR, "failed to unzip file");
    }

    // only delete object's file, not delete object's directory
    (void)litebus::os::Rmdir(destFile);
    return Status::OK();
}

Status RemoteDeployer::CheckZipFile(const std::string &path) const
{
    unzFile zip = unzOpen64(path.c_str());
    if (zip == nullptr) {
        return Status(StatusCode::FAILED, "check zip file failed, error: open file " + path + " failed");
    }

    unz_global_info64 zipInfo{};
    if (auto status = unzGetGlobalInfo64(zip, &zipInfo); status != UNZ_OK) {
        return Status(StatusCode::FAILED,
                      "check zip file failed, error: get global info failed, code: " + std::to_string(status));
    }

    uint64_t sum = 0;
    for (ZPOS64_T i = 0; i < zipInfo.number_entry;) {
        unz_file_info64 info;
        char name[200];
        if (auto status = unzGetCurrentFileInfo64(zip, &info, name, 200, nullptr, 0, nullptr, 0); status != UNZ_OK) {
            unzClose(zip);
            return Status(StatusCode::FAILED, "check zip file failed, error: get file(" + std::string(name) +
                                                  ") info failed, code: " + std::to_string(status));
        }

        if (auto status = unzOpenCurrentFile(zip); status != UNZ_OK) {
            unzClose(zip);
            return Status(StatusCode::FAILED, "check zip file failed, error: open file(" + std::string(name) +
                                                  ") failed, code: " + std::to_string(status));
        }

        uint64_t n;
        char buf[200];
        while ((n = static_cast<uint64_t>(unzReadCurrentFile(zip, buf, ZIP_FILE_BUFFER))) > 0) {
            sum += n;
            if (sum > unzipFileSizeMaxBytes_) {
                unzClose(zip);
                return Status(StatusCode::FAILED, "check zip file failed, error: file(" + std::string(name) +
                                                      ") is bigger than " + std::to_string(unzipFileSizeMaxBytes_));
            }
        }

        if (++i < zipInfo.number_entry) {
            if (auto status = unzGoToNextFile(zip); status != UNZ_OK) {
                unzClose(zip);
                return Status(StatusCode::FAILED,
                              "check zip file failed, error: go to next file failed, code: " + std::to_string(status));
            }
        }
    }
    unzClose(zip);
    return Status::OK();
}

Status RemoteDeployer::PackageValidation(const std::string &destFile, const std::string &sha512Str,
                                         const std::string &sha256Str)
{
    Status result;
    // check signature
    if (enableSignatureValidation_) {
        if (!sha512Str.empty()) {
            result = CheckSha512(destFile, sha512Str);
        } else {
            result = CheckSha256(destFile, sha256Str);
        }
    }
    if (result.IsError()) {
        return result;
    }

    // check zip bomb
    result = CheckZipFile(destFile);
    if (result.IsError()) {
        return result;
    }

    // check file content
    result = CheckFileContent(destFile);
    if (result.IsError()) {
        return result;
    }

    return Status::OK();
}

Status RemoteDeployer::CheckSha256(const std::string &destFile, const std::string &sha256Str)
{
    unsigned char sha256Value[SHA256_VALUE_LEN];
    auto result = Sha256CalculateFile(destFile.c_str(), sha256Value, SHA256_VALUE_LEN);
    if (result != 0) {
        YRLOG_ERROR("openssl sha256 failed");
        return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, openssl calculate sha256 failed");
    }
    std::stringstream ss;
    for (int i = 0; i < SHA256_VALUE_LEN; i++) {
        ss << std::hex << std::setw(CHAR_TO_HEX_LEN) << std::setfill('0') << static_cast<int>(sha256Value[i]);
    }

    YRLOG_DEBUG("sha256 from request is ({}), sha256 from file calculation is ({})", sha256Str, ss.str());
    if (sha256Str.empty() || std::strcmp(sha256Str.c_str(), ss.str().c_str()) != 0) {
        YRLOG_ERROR("Check download package validation failed. Signature sha256({}) doesn't match with request ({})",
                    ss.str(), sha256Str);
        return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, package signature doesn't match");
    }
    return Status::OK();
}

Status RemoteDeployer::CheckSha512(const std::string &destFile, const std::string &sha512Str)
{
    unsigned char sha512Value[SHA512_VALUE_LEN];
    auto result = Sha512CalculateFile(destFile.c_str(), sha512Value, SHA512_VALUE_LEN);
    if (result != 0) {
        YRLOG_ERROR("openssl sha512 failed");
        return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, openssl calculate sha512 failed");
    }
    std::stringstream ss;
    for (int i = 0; i < SHA512_VALUE_LEN; i++) {
        ss << std::hex << std::setw(CHAR_TO_HEX_LEN) << std::setfill('0') << static_cast<int>(sha512Value[i]);
    }

    YRLOG_DEBUG("sha512 from request is ({}), sha512 from file calculation is ({})", sha512Str, ss.str());
    if (sha512Str.empty() || std::strcmp(sha512Str.c_str(), ss.str().c_str()) != 0) {
        YRLOG_ERROR("Check download package validation failed. Signature sha512({}) doesn't match with request ({})",
                    ss.str(), sha512Str);
        return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, package signature doesn't match");
    }
    return Status::OK();
}

Status RemoteDeployer::CheckFileContent(const std::string &destFile)
{
    std::vector<std::string> lines;
    std::string line;
    int32_t fileCount = 0;
    uint64_t uncompressedSize = 0;
    int32_t depth = 0;
    std::string temp;

    // home/layer/func/bucket id/object_id-tmp(directory)/object_id(file)
    auto cmdResult = ExecuteCommandByPopen(std::string("unzip -Z -l " + destFile), CMD_OUTPUT_MAX_LEN);
    if (cmdResult.empty()) {
        return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, unzip command failed");
    }

    std::stringstream cmdStream(cmdResult);
    while (std::getline(cmdStream, line)) {
        lines.push_back(line);
    }

    std::stringstream fileInfoStream(lines.back());

    if (fileInfoStream >> fileCount && fileCount >= std::numeric_limits<int32_t>::max()) {
        YRLOG_ERROR("Check download package validation failed. Number of files exceeds int32_t numeric limits");
        return Status(StatusCode::ERR_USER_CODE_LOAD,
                      "package validation failed, number of files exceeds int32_t numeric limits");
    }

    fileInfoStream >> temp;

    if (fileInfoStream >> uncompressedSize && uncompressedSize >= std::numeric_limits<uint64_t>::max()) {
        YRLOG_ERROR("Check download package validation failed. Unzip file size exceeds uint64_t numeric limits");
        return Status(StatusCode::ERR_USER_CODE_LOAD,
                      "package validation failed, unzip file size exceeds uint64_t numeric limits");
    }

    for (size_t i = UNZIPINFO_HEADER_LEN; i < lines.size() - 1; i++) {
        auto filePath = lines.at(i).substr(lines.at(i).find_last_of(" "), lines.at(i).length());
        // check relative path
        if (filePath.find("../") != std::string::npos) {
            YRLOG_ERROR("Check download package validation failed. Package contains relative path");
            return Status(StatusCode::ERR_USER_CODE_LOAD, "package validation failed, package contains relative path");
        }
        // remove dir in file count
        if (filePath.find_last_of("/") == filePath.length() - 1) {
            fileCount--;
            continue;
        }
        // count max dir depth
        auto currDepth = std::count(filePath.begin(), filePath.end(), '/');
        if (currDepth > depth) {
            depth = currDepth;
        }
    }

    if (fileCount > codePackageThresholds_.filecountsmax()) {
        YRLOG_ERROR("Check download package validation failed. Number of files({}) exceeds maximum limit({})",
                    fileCount, codePackageThresholds_.filecountsmax());
        return Status(StatusCode::ERR_USER_CODE_LOAD,
                      "package validation failed, the number of files exceeds maximum limit");
    }

    if (uncompressedSize > static_cast<uint64_t>(codePackageThresholds_.unzipfilesizemaxmb()) * SIZE_MEGA_BYTES) {
        YRLOG_ERROR(
            "Check download package validation failed. "
            "Unzip file size({} bytes) exceeds maximum limit({} bytes)",
            uncompressedSize, static_cast<uint64_t>(codePackageThresholds_.unzipfilesizemaxmb()) * SIZE_MEGA_BYTES);
        return Status(StatusCode::ERR_USER_CODE_LOAD,
                      "package validation failed, unzip file size exceeds maximum limit");
    }

    if (depth > codePackageThresholds_.dirdepthmax()) {
        YRLOG_ERROR("Check download package validation failed. The depth of dir({}) exceeds maximum limit({})", depth,
                    codePackageThresholds_.dirdepthmax());
        return Status(StatusCode::ERR_USER_CODE_LOAD,
                      "package validation failed, the depth of dir exceeds maximum limit");
    }

    YRLOG_DEBUG("zipFileInfo: fileCount({}), uncompressedSize({}), dirDepth({})", fileCount, uncompressedSize, depth);
    return Status::OK();
}
}  // namespace functionsystem::function_agent
