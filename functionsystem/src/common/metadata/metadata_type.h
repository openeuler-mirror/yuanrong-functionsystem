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
#ifndef METADATA_TYPE_H
#define METADATA_TYPE_H

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utils/os_utils.hpp>
#include <vector>

#include "common/constants/constants.h"
#include "common/resource_view/resource_type.h"

#include "common/status/status.h"
#include "meta_store_client/meta_store_struct.h"

namespace functionsystem {
using namespace functionsystem::resource_view;

const std::string LOCAL_STORAGE_TYPE = "local";
const std::string S3_STORAGE_TYPE = "s3";
const std::string COPY_STORAGE_TYPE = "copy";
const std::string WORKING_DIR_STORAGE_TYPE = "working_dir";
const std::string SHARED_DIR_STORAGE_TYPE = "shared_dir";
const std::string DEPLOY_DIR = "/dcache";
const uint8_t VERSION_POSITION = 1;       // function version position from back to front
const uint8_t FUNCTIONNAME_POSITION = 2;  // function name position from back to front
const uint8_t TENANT_POSITION = 3;        // function tenant position from back to front

/**
 * Specifies the instance reliability type. The instance status persistence is optimized.
 * high: persistent all statuses of instance. default value.
 * low: persistent partial status of instance.
 */
const std::string RELIABILITY_TYPE = "ReliabilityType";  // NOLINT

struct ProxyMeta {
    std::string node;
    std::string aid;
    std::string ak;
};

struct InstanceResource {
    std::string cpu;
    std::string memory;
    std::map<std::string, std::string> customResources;
};

struct InstanceLimitResource {
    uint64_t minCpu = DEFAULT_MIN_INSTANCE_CPU_SIZE;
    uint64_t minMemory = DEFAULT_MIN_INSTANCE_MEMORY_SIZE;
    uint64_t maxCpu = DEFAULT_MAX_INSTANCE_CPU_SIZE;
    uint64_t maxMemory = DEFAULT_MAX_INSTANCE_MEMORY_SIZE;
};

struct FuncMetaData {
    std::string urn;
    std::string runtime;  // Language
    std::string handler;
    std::string codeSha256;
    std::string codeSha512;
    std::string entryFile;
    std::map<std::string, std::string> hookHandler;
    std::string name;
    std::string version;
    std::string tenantId;
    bool isSystemFunc{ false };
    // static function add
    uint32_t timeout = 0;
    std::string staticHandler = "";
};

struct Layer {
    std::string appID;
    std::string bucketID;
    std::string objectID;
    std::string bucketURL;
    std::string sha256;
    std::string sha512;
    std::string hostName;
    std::string securityToken;
    std::string temporaryAccessKey;
    std::string temporarySecretKey;
    std::string storageType;
    std::string codePath;
};

struct CodeMetaData {
    std::string storageType;
    std::string bucketID;
    std::string objectID;
    std::string bucketUrl;
    std::vector<Layer> layers;
    std::string deployDir;
    std::string sha512;
    std::string appId;
};

struct EnvMetaData {
    std::string envKey;
    std::string envInfo;  // environment
    std::string encryptedUserData;
    std::string cryptoAlgorithm;
};

struct InstanceMetaData {
    int32_t maxInstance;
    int32_t minInstance;
    int32_t concurrentNum;
    int32_t cacheInstance;
    int32_t diskLimit;
    std::string scalePolicy;
};

struct MountUser {
    int32_t userID;
    int32_t groupID;
};

struct FuncMount {
    std::string mountType;
    std::string mountResource;
    std::string mountSharePath;
    std::string localMountPath;
    std::string status;
};

struct MountConfig {
    MountUser mountUser;
    std::vector<FuncMount> funcMounts;
};

struct DeviceMetaData {
    float hbm = 0;
    float latency = 0;
    uint32_t stream = 0;
    uint32_t count = 0;
    std::string model;
    std::string type;
};

struct Initializer {
    std::string handler;
    uint32_t timeout;
};

struct UserAgency {
    std::string accessKey;
    std::string secretKey;
    std::string token;
    std::string securityAk;
    std::string securitySk;
    std::string securityToken;
};

struct CustomGracefulShutdown {
    uint32_t maxShutdownTimeout;
};

struct ExtendedMetaData {
    InstanceMetaData instanceMetaData;
    MountConfig mountConfig;
    DeviceMetaData deviceMetaData;
    Initializer initializer;
    UserAgency userAgency;
    CustomGracefulShutdown customGracefulShutdown;
};

struct FunctionMeta {
    FuncMetaData funcMetaData;
    CodeMetaData codeMetaData;
    EnvMetaData envMetaData;
    resource_view::Resources resources;
    ExtendedMetaData extendedMetaData;
    InstanceMetaData instanceMetaData;
    std::string rawJsonStr;
};

struct StoreInfo {
    std::string key;
    std::string value;

    StoreInfo(const std::string &k, const std::string &v) : key(k), value(v)
    {
    }

    StoreInfo()
    {
    }
};

struct SyncResult {
    Status status;
};

using SyncerFunction = std::function<litebus::Future<SyncResult>(const std::shared_ptr<GetResponse> &)>;
using ResponseHandler = std::function<litebus::Future<Status>(const std::shared_ptr<GetResponse> &)>;

}  // namespace functionsystem
#endif // METADATA_TYPE_H
