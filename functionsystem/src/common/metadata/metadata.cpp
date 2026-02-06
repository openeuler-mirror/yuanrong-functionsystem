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

#include "metadata.h"

namespace functionsystem {

bool TransToInstanceInfoFromJson(InstanceInfo &instanceInfo, const std::string &jsonStr)
{
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    auto status = google::protobuf::util::JsonStringToMessage(jsonStr, &instanceInfo, jsonOpt);
    if (!status.ok()) {
        YRLOG_WARN("failed trans json to instance info, {}", status.ToString());
    }
    return status.ok();
}

bool TransToDebugInstanceInfoFromJson(messages::DebugInstanceInfo &debugInstInfo, const std::string &jsonStr)
{
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    auto status = google::protobuf::util::JsonStringToMessage(jsonStr, &debugInstInfo, jsonOpt);
    if (!status.ok()) {
        YRLOG_WARN("failed trans json to instance info, {}", status.ToString());
    }
    return status.ok();
}

bool TransToGroupInfoFromJson(messages::GroupInfo &groupInfo, const std::string &jsonStr)
{
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    auto status = google::protobuf::util::JsonStringToMessage(jsonStr, &groupInfo, jsonOpt);
    if (!status.ok()) {
        YRLOG_WARN("failed trans json to group info, {}", status.ToString());
    }
    return status.ok();
}

bool TransToJsonFromInstanceInfo(std::string &jsonStr, const InstanceInfo &instanceInfo)
{
    return google::protobuf::util::MessageToJsonString(instanceInfo, &jsonStr).ok();
}

bool TransToJsonFromGroupInfo(std::string &jsonStr, const messages::GroupInfo &groupInfo)
{
    return google::protobuf::util::MessageToJsonString(groupInfo, &jsonStr).ok();
}

bool TransToRouteInfoFromJson(resources::RouteInfo &routeInfo, const std::string &jsonStr)
{
    auto jsonOpt = google::protobuf::util::JsonParseOptions();
    jsonOpt.ignore_unknown_fields = true;
    auto status = google::protobuf::util::JsonStringToMessage(jsonStr, &routeInfo, jsonOpt);
    if (!status.ok()) {
        YRLOG_WARN("failed trans json to route info, {}", status.ToString());
    }
    return status.ok();
}

bool TransToJsonFromRouteInfo(std::string &jsonStr, const resources::RouteInfo &routeInfo)
{
    return google::protobuf::util::MessageToJsonString(routeInfo, &jsonStr).ok();
}

void TransToInstanceInfoFromRouteInfo(const resources::RouteInfo &routeInfo, InstanceInfo &instanceInfo)
{
    instanceInfo.set_instanceid(routeInfo.instanceid());
    instanceInfo.set_runtimeaddress(routeInfo.runtimeaddress());
    instanceInfo.set_functionagentid(routeInfo.functionagentid());
    instanceInfo.set_function(routeInfo.function());
    instanceInfo.set_functionproxyid(routeInfo.functionproxyid());
    instanceInfo.set_jobid(routeInfo.jobid());
    instanceInfo.set_parentid(routeInfo.parentid());
    instanceInfo.set_requestid(routeInfo.requestid());
    instanceInfo.set_tenantid(routeInfo.tenantid());
    instanceInfo.set_issystemfunc(routeInfo.issystemfunc());
    instanceInfo.set_version(routeInfo.version());
    instanceInfo.mutable_instancestatus()->CopyFrom(routeInfo.instancestatus());
}

bool IsLowReliabilityInstance(const resources::InstanceInfo &instanceInfo)
{
    const auto &createOpt = instanceInfo.createoptions();
    if (auto it = createOpt.find(RELIABILITY_TYPE); it != createOpt.end() && it->second == "low") {
        YRLOG_INFO("The 'ReliabilityType' exists and is 'low'.");
        // The 'ReliabilityType' exists and is 'low'
        return true;
    }
    // ReliabilityType's default value is "high"
    return false;
}

void TransToRouteInfoFromInstanceInfo(const InstanceInfo &instanceInfo, resources::RouteInfo &routeInfo)
{
    routeInfo.set_instanceid(instanceInfo.instanceid());
    routeInfo.set_runtimeaddress(instanceInfo.runtimeaddress());
    routeInfo.set_functionagentid(instanceInfo.functionagentid());
    routeInfo.set_function(instanceInfo.function());
    routeInfo.set_functionproxyid(instanceInfo.functionproxyid());
    routeInfo.set_jobid(instanceInfo.jobid());
    routeInfo.set_parentid(instanceInfo.parentid());
    routeInfo.set_requestid(instanceInfo.requestid());
    routeInfo.set_tenantid(instanceInfo.tenantid());
    routeInfo.set_issystemfunc(instanceInfo.issystemfunc());
    routeInfo.set_version(instanceInfo.version());
    routeInfo.mutable_instancestatus()->CopyFrom(instanceInfo.instancestatus());
    routeInfo.set_trafficreporttype(instanceInfo.trafficreporttype());
}

std::string GetDeployDir()
{
    auto currentDirOption = litebus::os::GetEnv("DEPLOY_DIR");
    if (currentDirOption.IsSome()) {
        return currentDirOption.Get();
    } else {
        YRLOG_WARN("env of DEPLOY_DIR is empty");
        return DEPLOY_DIR;
    }
}

void GetEntryFileAndHandler(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    const std::string deployPath = "/dcache/layer/func/";  // need to get from config and trans to absolute path
    const int32_t handlerItemLen = 2;

    if (j.find("handler") == j.end()) {
        YRLOG_WARN("handler is empty");
        return;
    }

    std::string handler = j.at("handler");

    if (ContainStr(funcMeta.funcMetaData.runtime, "java")) {
        auto strs = litebus::strings::Split(handler, "::");
        if (strs.size() == handlerItemLen) {
            funcMeta.funcMetaData.entryFile = strs[0];
            funcMeta.funcMetaData.handler = strs[1];
        } else {
            funcMeta.funcMetaData.entryFile = handler;
        }
    } else if (ContainStr(funcMeta.funcMetaData.runtime, "python")) {
        auto strs = litebus::strings::Split(handler, ".");
        if (strs.size() == handlerItemLen) {
            funcMeta.funcMetaData.entryFile = deployPath + strs[0] + ".py";
            funcMeta.funcMetaData.handler = strs[1];
        } else {
            funcMeta.funcMetaData.entryFile = deployPath + "handler.py";
            funcMeta.funcMetaData.handler = handler;
        }
    } else if (ContainStr(funcMeta.funcMetaData.runtime, "cpp")) {
        funcMeta.funcMetaData.entryFile = deployPath + handler;
    }
}

void GetFuncMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    if (j.find("funcMetaData") != j.end()) {
        nlohmann::json funcMetaData = j.at("funcMetaData");
        if (funcMetaData.find("functionVersionUrn") != funcMetaData.end()) {
            funcMeta.funcMetaData.urn = funcMetaData.at("functionVersionUrn");
        }
        if (funcMetaData.find("runtime") != funcMetaData.end()) {
            funcMeta.funcMetaData.runtime = funcMetaData.at("runtime");
        }
        if (funcMetaData.find("codeSha256") != funcMetaData.end()) {
            funcMeta.funcMetaData.codeSha256 = funcMetaData.at("codeSha256");
        }
        if (funcMetaData.find("codeSha512") != funcMetaData.end()) {
            funcMeta.funcMetaData.codeSha512 = funcMetaData.at("codeSha512");
        }
        if (funcMetaData.find("hookHandler") != funcMetaData.end()) {
            nlohmann::json hookHandler = funcMetaData.at("hookHandler");
            for (auto &item : hookHandler.items()) {
                funcMeta.funcMetaData.hookHandler[item.key()] = item.value();
            }
        }
        if (funcMetaData.find("name") != funcMetaData.end()) {
            funcMeta.funcMetaData.name = funcMetaData.at("name");
        }
        if (funcMetaData.find("version") != funcMetaData.end()) {
            funcMeta.funcMetaData.version = funcMetaData.at("version");
        }
        if (funcMetaData.find("tenantId") != funcMetaData.end()) {
            funcMeta.funcMetaData.tenantId = funcMetaData.at("tenantId");
        }
        if (funcMetaData.find("revisionId") != funcMetaData.end()) {
            funcMeta.funcMetaData.revisionId = funcMetaData.at("revisionId");
        }
        if (funcMetaData.find("timeout") != funcMetaData.end()) {
            funcMeta.funcMetaData.timeout = funcMetaData.at("timeout");
        }
        if (funcMetaData.find("handler") != funcMetaData.end()) {
            funcMeta.funcMetaData.staticHandler = funcMetaData.at("handler");
        }
        GetEntryFileAndHandler(funcMeta, funcMetaData);
    }
}

void GetLayers(FunctionMeta &funcMeta, const nlohmann::json &layers)
{
    for (const nlohmann::json &l : layers) {
        Layer layer{};
        if (l.find("appId") != l.end()) {
            layer.appID = l.at("appId");
        }
        if (l.find("bucketId") != l.end()) {
            layer.bucketID = l.at("bucketId");
        }
        if (l.find("objectId") != l.end()) {
            layer.objectID = l.at("objectId");
        }
        if (l.find("bucketUrl") != l.end()) {
            layer.bucketURL = l.at("bucketUrl");
        }
        if (l.find("sha256") != l.end()) {
            layer.sha256 = l.at("sha256");
        }
        funcMeta.codeMetaData.layers.push_back(layer);
    }
}

void GetCodeMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    if (j.find("codeMetaData") == j.end()) {
        YRLOG_ERROR("codeMetaData in funcMeta json is empty");
        return;
    }

    nlohmann::json codeMetaData = j.at("codeMetaData");
    if (codeMetaData.find("storage_type") != codeMetaData.end()) {
        funcMeta.codeMetaData.storageType = codeMetaData.at("storage_type");
    }

    if (funcMeta.codeMetaData.storageType == LOCAL_STORAGE_TYPE ||
        funcMeta.codeMetaData.storageType == COPY_STORAGE_TYPE) {  // code in local
        if (codeMetaData.find("code_path") != codeMetaData.end() && !codeMetaData.at("code_path").empty()) {
            funcMeta.codeMetaData.deployDir = codeMetaData.at("code_path");
        } else {
            funcMeta.codeMetaData.deployDir = "/home/sn/function/package/" + funcMeta.funcMetaData.name;
        }
    } else {  // code in S3 or NSP or woring_dir
        if (codeMetaData.find("bucketId") != codeMetaData.end()) {
            funcMeta.codeMetaData.bucketID = codeMetaData.at("bucketId");
        }
        if (codeMetaData.find("objectId") != codeMetaData.end()) {
            funcMeta.codeMetaData.objectID = codeMetaData.at("objectId");
        }
        if (codeMetaData.find("bucketUrl") != codeMetaData.end()) {
            funcMeta.codeMetaData.bucketUrl = codeMetaData.at("bucketUrl");
        }
        if (codeMetaData.find("appId") != codeMetaData.end()) {
            funcMeta.codeMetaData.appId = codeMetaData.at("appId");
        }

        if (j.find("funcMetaData") != j.end()) {
            funcMeta.codeMetaData.deployDir = GetDeployDir();

            nlohmann::json funcMetaData = j.at("funcMetaData");
            if (funcMetaData.find("layers") != funcMetaData.end()) {
                nlohmann::json layers = funcMetaData.at("layers");
                GetLayers(funcMeta, layers);
            }
        }
    }
}

void GetEnvMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    if (j.find("envMetaData") != j.end()) {
        nlohmann::json envMetaData = j.at("envMetaData");
        if (envMetaData.find("envKey") != envMetaData.end()) {
            funcMeta.envMetaData.envKey = envMetaData.at("envKey");
        }
        if (envMetaData.find("environment") != envMetaData.end()) {
            funcMeta.envMetaData.envInfo = envMetaData.at("environment");
        }
        if (envMetaData.find("encrypted_user_data") != envMetaData.end()) {
            funcMeta.envMetaData.encryptedUserData = envMetaData.at("encrypted_user_data");
        }
        if (envMetaData.find("cryptoAlgorithm") != envMetaData.end()) {
            funcMeta.envMetaData.cryptoAlgorithm = envMetaData.at("cryptoAlgorithm");
        }
    }
}

void GetResourceMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    double cpuValue = 0.0;
    double memoryValue = 0.0;
    if (j.find("resourceMetaData") != j.end()) {
        nlohmann::json resourceMetaData = j.at("resourceMetaData");
        if (resourceMetaData.find("cpu") != resourceMetaData.end()) {
            cpuValue = static_cast<double>(resourceMetaData.at("cpu"));
        }
        if (resourceMetaData.find("memory") != resourceMetaData.end()) {
            memoryValue = static_cast<double>(resourceMetaData.at("memory"));
        }
    }

    Resource resourceCPU;
    resourceCPU.set_name(CPU_RESOURCE_NAME);
    resourceCPU.set_type(ValueType::Value_Type_SCALAR);
    resourceCPU.mutable_scalar()->set_value(cpuValue);

    Resource resourceMemory;
    resourceMemory.set_name(MEMORY_RESOURCE_NAME);
    resourceMemory.set_type(ValueType::Value_Type_SCALAR);
    resourceMemory.mutable_scalar()->set_value(memoryValue);

    resource_view::Resources resources;
    auto resourcesMap = resources.mutable_resources();
    (*resourcesMap)[CPU_RESOURCE_NAME] = resourceCPU;
    (*resourcesMap)[MEMORY_RESOURCE_NAME] = resourceMemory;

    funcMeta.resources = resources;
}

void GetFuncMounts(MountConfig &mountConfig, const nlohmann::json &funcMounts)
{
    for (const nlohmann::json &m : funcMounts) {
        FuncMount mount{};
        if (m.find(FUNC_MOUNT_TYPE) != m.end()) {
            mount.mountType = m.at(FUNC_MOUNT_TYPE);
        }
        if (m.find(FUNC_MOUNT_RESOURCE) != m.end()) {
            mount.mountResource = m.at(FUNC_MOUNT_RESOURCE);
        }
        if (m.find(FUNC_MOUNT_SHARE_PATH) != m.end()) {
            mount.mountSharePath = m.at(FUNC_MOUNT_SHARE_PATH);
        }
        if (m.find(FUNC_MOUNT_LOCAL_MOUNT_PATH) != m.end()) {
            mount.localMountPath = m.at(FUNC_MOUNT_LOCAL_MOUNT_PATH);
        }
        if (m.find(FUNC_MOUNT_STATUS) != m.end()) {
            mount.status = m.at(FUNC_MOUNT_STATUS);
        }

        mountConfig.funcMounts.push_back(mount);
    }
}

void GetNamedFunctionMetaData(FunctionMeta &funcMeta, nlohmann::json &extendedMetaData)
{
    nlohmann::json deviceMetaData = extendedMetaData.at("device");

    DeviceMetaData nf{};
    if (deviceMetaData.find("model") != deviceMetaData.end()) {
        nf.model = deviceMetaData.at("model");
    }
    if (deviceMetaData.find("hbm") != deviceMetaData.end()) {
        nf.hbm = deviceMetaData.at("hbm");
    }
    if (deviceMetaData.find("type") != deviceMetaData.end()) {
        nf.type = deviceMetaData.at("type");
    }
    if (deviceMetaData.find("count") != deviceMetaData.end()) {
        nf.count = static_cast<uint32_t>(deviceMetaData.at("count"));
    }

    if (deviceMetaData.find("latency") != deviceMetaData.end()) {
        nf.latency = deviceMetaData.at("latency");
    }
    if (deviceMetaData.find("stream") != deviceMetaData.end()) {
        nf.stream = static_cast<uint32_t>(deviceMetaData.at("stream"));
    }
    funcMeta.extendedMetaData.deviceMetaData = nf;
}

inline void GetInitializer(FunctionMeta &funcMeta, nlohmann::json &extendedMetaData)
{
    nlohmann::json initializer = extendedMetaData.at("initializer");
    Initializer init{};
    if (initializer.find("initializer_handler") != initializer.end()) {
        init.handler = initializer.at("initializer_handler");
    }
    if (initializer.find("initializer_timeout") != initializer.end()) {
        init.timeout = initializer.at("initializer_timeout");
    }
    funcMeta.extendedMetaData.initializer = init;
}

inline void GetUserAgency(FunctionMeta &funcMeta, nlohmann::json &extendedMetaData)
{
    nlohmann::json userAgency = extendedMetaData.at("user_agency");
    UserAgency ua{};
    if (userAgency.find("accessKey") != userAgency.end()) {
        ua.accessKey = userAgency.at("accessKey");
    }
    if (userAgency.find("secretKey") != userAgency.end()) {
        ua.secretKey = userAgency.at("secretKey");
    }
    if (userAgency.find("token") != userAgency.end()) {
        ua.token = userAgency.at("token");
    }
    if (userAgency.find("securityAk") != userAgency.end()) {
        ua.securityAk = userAgency.at("securityAk");
    }
    if (userAgency.find("securitySk") != userAgency.end()) {
        ua.securitySk = userAgency.at("securitySk");
    }
    if (userAgency.find("securityToken") != userAgency.end()) {
        ua.securityToken = userAgency.at("securityToken");
    }
    funcMeta.extendedMetaData.userAgency = ua;
}

void GetExtendedMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    if (j.find("extendedMetaData") == j.end()) {
        YRLOG_ERROR("extendedMetaData in funcMeta json is empty");
        return;
    }
    nlohmann::json extendedMetaData = j.at("extendedMetaData");
    if (extendedMetaData.find("instance_meta_data") != extendedMetaData.end()) {
        nlohmann::json instanceMetaData = extendedMetaData.at("instance_meta_data");
        if (instanceMetaData.find("maxInstance") != instanceMetaData.end()) {
            funcMeta.extendedMetaData.instanceMetaData.maxInstance = instanceMetaData.at("maxInstance");
        }
        if (instanceMetaData.find("minInstance") != instanceMetaData.end()) {
            funcMeta.extendedMetaData.instanceMetaData.minInstance = instanceMetaData.at("minInstance");
        }
        if (instanceMetaData.find("concurrentNum") != instanceMetaData.end()) {
            funcMeta.extendedMetaData.instanceMetaData.concurrentNum = instanceMetaData.at("concurrentNum");
        }
        if (instanceMetaData.find("cacheInstance") != instanceMetaData.end()) {
            funcMeta.extendedMetaData.instanceMetaData.cacheInstance = instanceMetaData.at("cacheInstance");
        }
    }

    if (extendedMetaData.find("mount_config") != extendedMetaData.end()) {
        nlohmann::json mountConfigMetaData = extendedMetaData.at("mount_config");
        if (mountConfigMetaData.find(MOUNT_USER) != mountConfigMetaData.end()) {
            nlohmann::json mountUserMetaData = mountConfigMetaData.at(MOUNT_USER);
            if (mountUserMetaData.find(MOUNT_USER_ID) != mountUserMetaData.end()) {
                funcMeta.extendedMetaData.mountConfig.mountUser.userID = mountUserMetaData.at(MOUNT_USER_ID);
            } else {
                funcMeta.extendedMetaData.mountConfig.mountUser.userID = DEFAULT_USER_ID;
            }
            if (mountUserMetaData.find(MOUNT_USER_GROUP_ID) != mountUserMetaData.end()) {
                funcMeta.extendedMetaData.mountConfig.mountUser.groupID = mountUserMetaData.at(MOUNT_USER_GROUP_ID);
            } else {
                funcMeta.extendedMetaData.mountConfig.mountUser.groupID = DEFAULT_GROUP_ID;
            }
        }
        if (mountConfigMetaData.find(FUNC_MOUNTS) != mountConfigMetaData.end()) {
            nlohmann::json funcMountsMetaData = mountConfigMetaData.at(FUNC_MOUNTS);
            GetFuncMounts(funcMeta.extendedMetaData.mountConfig, funcMountsMetaData);
        }
    }

    if (extendedMetaData.find("device") != extendedMetaData.end()) {
        GetNamedFunctionMetaData(funcMeta, extendedMetaData);
    }

    if (extendedMetaData.find("initializer") != extendedMetaData.end()) {
        GetInitializer(funcMeta, extendedMetaData);
    }
    if (extendedMetaData.find("user_agency") != extendedMetaData.end()) {
        GetUserAgency(funcMeta, extendedMetaData);
    }
    if (extendedMetaData.find("runtime_graceful_shutdown") != extendedMetaData.end()) {
        nlohmann::json gracefulShutdown = extendedMetaData.at("runtime_graceful_shutdown");
        if (gracefulShutdown.find("maxShutdownTimeout") != gracefulShutdown.end()) {
            funcMeta.extendedMetaData.customGracefulShutdown.maxShutdownTimeout =
                gracefulShutdown.at("maxShutdownTimeout");
        }
    }
}

inline void GetInstanceMetaData(FunctionMeta &funcMeta, const nlohmann::json &j)
{
    if (j.find("instanceMetaData") == j.end()) {
        YRLOG_ERROR("instanceMetaData in funcMeta json is empty");
        return;
    }
    nlohmann::json instanceMetaData = j.at("instanceMetaData");
    if (instanceMetaData.find("maxInstance") != instanceMetaData.end()) {
        funcMeta.instanceMetaData.maxInstance = instanceMetaData.at("maxInstance");
    }
    if (instanceMetaData.find("minInstance") != instanceMetaData.end()) {
        funcMeta.instanceMetaData.minInstance = instanceMetaData.at("minInstance");
    }
    if (instanceMetaData.find("concurrentNum") != instanceMetaData.end()) {
        funcMeta.instanceMetaData.concurrentNum = instanceMetaData.at("concurrentNum");
    }
    if (instanceMetaData.find("diskLimit") != instanceMetaData.end()) {
        funcMeta.instanceMetaData.diskLimit = instanceMetaData.at("diskLimit");
    }
    if (instanceMetaData.find("scalePolicy") != instanceMetaData.end()) {
        funcMeta.instanceMetaData.scalePolicy = instanceMetaData.at("scalePolicy");
    }
}

inline void GetRoofsMetaData(FunctionMeta &funcMeta, const nlohmann::json &h)
{
    YRLOG_INFO("debug::{}", h.dump());
    if (h.find("warmup") != h.end()) {
        funcMeta.warmup = StringToWarmupType(h.at("warmup"));
    }

    if (h.find("rootfs") == h.end()) {
        return;
    }
    nlohmann::json rf = h.at("rootfs");
    if (rf.find("runtime") != rf.end()) {
        funcMeta.rootfs.runtime = rf.at("runtime");
    }
    if (rf.find("type") != rf.end()) {
        funcMeta.rootfs.type = StringToRootfsSrcType(rf.at("type"));
    }
    if (rf.find("imageurl") != rf.end()) {
        funcMeta.rootfs.imageurl = rf.at("imageurl");
    }
    if (rf.find("mountpoint") != rf.end()) {
        funcMeta.rootfs.mountpoint = rf.at("mountpoint");
    }
    if (rf.find("readonly") != rf.end()) {
        if (rf.at("readonly").is_boolean()) {
            funcMeta.rootfs.readonly = rf.at("readonly");
        } else if (rf.at("readonly").is_string()) {
            std::string str = rf.at("readonly");
            funcMeta.rootfs.readonly = (str == "true" || str == "1");
        }
    }
    if (rf.find("storageInfo") != rf.end()) {
        nlohmann::json storage = rf.at("storageInfo");
        if (storage.find("endpoint") != storage.end()) {
            funcMeta.rootfs.storageInfo.endpoint = storage.at("endpoint");
        }
        if (storage.find("bucket") != storage.end()) {
            funcMeta.rootfs.storageInfo.bucket = storage.at("bucket");
        }
        if (storage.find("object") != storage.end()) {
            funcMeta.rootfs.storageInfo.object = storage.at("object");
        }
        if (storage.find("accessKey") != storage.end()) {
            funcMeta.rootfs.storageInfo.accessKey = storage.at("accessKey");
        }
        if (storage.find("secretKey") != storage.end()) {
            funcMeta.rootfs.storageInfo.secretKey = storage.at("secretKey");
        }
    }
}

inline void GetLanguageMetaData(FunctionMeta &funcMeta, const nlohmann::json &h)
{
    if (h.find("language") == h.end()) {
        return;
    }
    nlohmann::json lang = h.at("language");
    if (lang.find("name") != lang.end()) {
        funcMeta.language.name = lang.at("name");
    }
    if (lang.find("type") != lang.end()) {
        funcMeta.language.type = lang.at("type");
    }
    if (lang.find("root") != lang.end()) {
        funcMeta.language.root = lang.at("root");
    }
    if (lang.find("entrypoint") != lang.end()) {
        funcMeta.language.entrypoint = lang.at("entrypoint");
    }
    if (lang.find("executor") != lang.end()) {
        funcMeta.language.executor = lang.at("executor");
    }
    if (lang.find("version") != lang.end()) {
        funcMeta.language.version = lang.at("version");
    }
    if (lang.find("env") != lang.end()) {
        nlohmann::json envs = lang.at("env");
        for (const auto &env : envs.items()) {
            funcMeta.language.env[env.key()] = env.value();
        }
    }
}

FunctionMeta GetFuncMetaFromJson(const std::string &jsonStr)
{
    FunctionMeta funcMeta{};
    try {
        auto j = nlohmann::json::parse(jsonStr);

        // funcMetaData
        GetFuncMetaData(funcMeta, j);

        // codeMetaData
        GetCodeMetaData(funcMeta, j);

        // envMetaData
        GetEnvMetaData(funcMeta, j);

        // resource meta data
        GetResourceMetaData(funcMeta, j);

        // instanceMetaData
        GetInstanceMetaData(funcMeta, j);

        // extendedMetaData
        GetExtendedMetaData(funcMeta, j);

        GetRoofsMetaData(funcMeta, j);

        // languageMetaData
        GetLanguageMetaData(funcMeta, j);
    } catch (std::exception &e) {
        YRLOG_ERROR("parse funcMeta json failed, error: {}", e.what());
    }
    return funcMeta;
}

litebus::Option<std::string> GetFuncName(const std::string &name, const std::string &version,
                                         const std::string &tenantId)
{
    if (name.empty() || version.empty() || tenantId.empty()) {
        return litebus::None();
    }
    return litebus::os::Join(litebus::os::Join(tenantId, name), version);
}

litebus::Option<FunctionMeta> GetFuncMeta(const std::string &funcKey,
                                          const std::unordered_map<std::string, FunctionMeta> &funcMetaMap,
                                          const std::unordered_map<std::string, FunctionMeta> &systemFuncMetaMap)
{
    if (funcMetaMap.find(funcKey) != funcMetaMap.end()) {
        auto funcMeta = funcMetaMap.at(funcKey);
        funcMeta.funcMetaData.isSystemFunc = false;
        return funcMeta;
    }

    if (systemFuncMetaMap.find(funcKey) != systemFuncMetaMap.end()) {
        auto funcMeta = systemFuncMetaMap.at(funcKey);
        funcMeta.funcMetaData.isSystemFunc = true;
        return funcMeta;
    }

    YRLOG_WARN("no function meta of funcKey: {}", funcKey);
    return litebus::None();
}

void LoadLocalFuncMeta(std::unordered_map<std::string, FunctionMeta> &map, const std::string &path)
{
    if (!litebus::os::ExistPath(path)) {
        YRLOG_WARN("{} is not exist", path);
        return;
    }

    auto filesOption = litebus::os::Ls(path);
    if (filesOption.IsNone() || filesOption.Get().empty()) {
        YRLOG_WARN("no function meta file in {}", path);
        return;
    }

    auto files = filesOption.Get();
    for (const auto &file : files) {
        auto filePath = litebus::os::Join(path, file, '/');
        if (!IsFile(filePath)) {
            YRLOG_WARN("filePath {} is not file.", filePath);
            continue;
        }

        YRLOG_INFO("Read function meta file {}", filePath);
        auto content = litebus::os::Read(filePath);
        if (content.IsNone() || content.Get().empty()) {
            YRLOG_WARN("no function meta information in {}", filePath);
            continue;
        }

        try {
            auto funcMeta = GetFuncMetaFromJson(content.Get());
            auto funcKey =
                GetFuncName(funcMeta.funcMetaData.name, funcMeta.funcMetaData.version, funcMeta.funcMetaData.tenantId);
            if (funcKey.IsNone()) {
                YRLOG_WARN("funcMetaData urn: {} is invalid", funcMeta.funcMetaData.urn);
                continue;
            }
            map[funcKey.Get()] = funcMeta;
        } catch (std::exception &e) {
            YRLOG_WARN("function metadata is invalid in {}, error: {}", filePath, e.what());
            continue;
        }
    }

    YRLOG_INFO("load system function from path({}) successfully", path);
}

Layer ParseDelegateDownloadInfo(const nlohmann::json &parser)
{
    Layer layer;
    if (parser.find("appId") != parser.end()) {
        layer.appID = parser.at("appId");
    }

    if (parser.find("bucketId") != parser.end()) {
        layer.bucketID = parser.at("bucketId");
    }

    if (parser.find("objectId") != parser.end()) {
        layer.objectID = parser.at("objectId");
    }

    if (parser.find("hostName") != parser.end()) {
        layer.hostName = parser.at("hostName");
    }

    if (parser.find("securityToken") != parser.end()) {
        layer.securityToken = parser.at("securityToken");
    }

    if (parser.find("temporaryAccessKey") != parser.end()) {
        layer.temporaryAccessKey = parser.at("temporaryAccessKey");
    }

    if (parser.find("temporarySecretKey") != parser.end()) {
        layer.temporarySecretKey = parser.at("temporarySecretKey");
    }

    if (parser.find("sha256") != parser.end()) {
        layer.sha256 = parser.at("sha256");
    }

    if (parser.find("sha512") != parser.end()) {
        layer.sha512 = parser.at("sha512");
    }

    if (parser.find("storage_type") != parser.end() && !parser.at("storage_type").empty()) {
        layer.storageType = parser.at("storage_type");
        if (layer.storageType.empty()) {
            layer.storageType = S3_STORAGE_TYPE;
        }
    } else {
        layer.storageType = S3_STORAGE_TYPE;
    }

    if (parser.find("code_path") != parser.end()) {
        layer.codePath = parser.at("code_path");
    }

    return layer;
}

litebus::Option<Layer> ParseDelegateDownloadInfoByStr(const std::string &str)
{
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(str);
    } catch (std::exception &error) {
        YRLOG_WARN("parse delegate download info {} failed, error: {}", str, error.what());
        return litebus::None();
    }

    return ParseDelegateDownloadInfo(parser);
}

std::vector<Layer> ParseDelegateDownloadInfos(const std::string &str)
{
    std::vector<Layer> layers;
    nlohmann::json parser;
    try {
        parser = nlohmann::json::parse(str);
    } catch (std::exception &error) {
        YRLOG_WARN("parse delegate download infos {} failed, error: {}", str, error.what());
        return layers;
    }

    for (auto &j : parser) {
        layers.push_back(ParseDelegateDownloadInfo(j));
    }
    return layers;
}

}  // namespace functionsystem