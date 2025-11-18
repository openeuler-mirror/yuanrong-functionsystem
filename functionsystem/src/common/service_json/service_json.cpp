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

#include "service_json.h"

#include "common/metadata/metadata.h"
#include "common/resource_view/resource_tool.h"
#include "common/service_json/service_handler.h"
#include "common/service_json/service_metadata.h"
#include "common/utils/files.h"
#include "common/yaml_tool/yaml_tool.h"

namespace functionsystem::service_json {
static const uint32_t SERVICE_NAME_MAX_LEN = 16;

static std::unordered_set<std::string> runtimeEnum = {
    CPP_RUNTIME_VERSION,         JAVA_RUNTIME_VERSION,      JAVA11_RUNTIME_VERSION,    PYTHON_RUNTIME_VERSION,
    PYTHON3_RUNTIME_VERSION,     PYTHON36_RUNTIME_VERSION,  PYTHON37_RUNTIME_VERSION,  PYTHON38_RUNTIME_VERSION,
    PYTHON39_RUNTIME_VERSION,    PYTHON310_RUNTIME_VERSION, PYTHON311_RUNTIME_VERSION, GO_RUNTIME_VERSION,
    POSIX_CUSTOM_RUNTIME_VERSION
};

bool NameMatch(const std::string &str, const std::string &regex)
{
    return std::regex_match(str, std::regex(regex));
}

bool CheckName(const std::string &name, const std::string regex, uint32_t minLen, uint32_t maxLen)
{
    if (name.empty()) {
        YRLOG_ERROR("(funcMeta)name is empty.");
        return false;
    }

    if (!NameMatch(name, regex)) {
        YRLOG_ERROR("(funcMeta)name({}) is invalid. regex: {}", name, regex);
        return false;
    }

    if (name.size() < minLen || name.size() > maxLen) {
        YRLOG_ERROR("(funcMeta)len of name{{}) is invalid, min len: {}, max len: {}", name, minLen, maxLen);
        return false;
    }
    return true;
}

bool CheckServiceName(const std::string &serviceName)
{
    if (!CheckName(serviceName, SERVICE_NAME_REGEX, 1, SERVICE_NAME_MAX_LEN)) {
        YRLOG_ERROR("(funcMeta)service name({}) is invalid", serviceName);
        return false;
    }
    return true;
}

bool CheckKind(const std::string &kind)
{
    static std::unordered_map<std::string, bool> kindMap = {
        { FAAS, true }, { YR_LIB, true }, { CUSTOM, true }, { POSIX_RUNTIME_CUSTOM, true }
    };
    if (auto iter = kindMap.find(kind); iter == kindMap.end() || !iter->second) {
        YRLOG_ERROR("(funcMeta)The kind({}) is not supported", kind);
        return false;
    }
    return true;
}

bool CheckServiceInfo(const service_json::ServiceInfo &serviceInfo)
{
    return CheckServiceName(serviceInfo.service) && CheckKind(serviceInfo.kind);
}

bool CheckFunctionName(const std::string &functionName)
{
    if (!CheckName(functionName, FUNCTION_NAME_REGEX, 1, UINT32_MAX)) {
        YRLOG_ERROR("(funcMeta)service name({}) is invalid", functionName);
        return false;
    }

    return true;
}

bool CheckRuntime(const std::string &runtime)
{
    if (runtimeEnum.find(runtime) == runtimeEnum.end()) {
        YRLOG_ERROR("(funcMeta)the runtime({}) isn't supported", runtime);
        return false;
    }
    return true;
}

bool CheckCPUAndMemorySize(int64_t cpu, int64_t memory)
{
    if (cpu < CUSTOM_REQUEST_CPU || cpu > CUSTOM_LIMIT_CPU) {
        YRLOG_ERROR("(funcMeta)CPU of custom pool not in range [{}, {}]", CUSTOM_REQUEST_CPU, CUSTOM_LIMIT_CPU);
        return false;
    }
    if (memory < CUSTOM_REQUEST_MEM || memory > CUSTOM_LIMIT_MEM) {
        YRLOG_ERROR("(funcMeta)memory of custom pool not in in range [{}, {}]", CUSTOM_REQUEST_MEM, CUSTOM_LIMIT_MEM);
        return false;
    }
    return true;
}

bool CheckEnv(const std::unordered_map<std::string, std::string> &envs)
{
    static std::unordered_set<std::string> systemReservedFieldsEnum = {
        "FAAS_FUNCTION_NAME",        "FAAS_FUNCTION_VERSION",         "FAAS_FUNCTION_BUSINESS",
        "FAAS_FUNCTION_TENANTID",    "FAAS_FUNCTION_USER_FILE_PATH",  "FAAS_FUNCTION_USER_PATH_LIMITS",
        "FAAS_FUNCTION_DEPLOY_DIR",  "FAAS_LAYER_DEPLOY_DIR",         "FAAS_FUNCTION_TIMEOUT",
        "FAAS_FUNCTION_MEMORY",      "FAAS_FUNCTION_REGION",          "FAAS_FUNCTION_TIMEZONE",
        "FAAS_FUNCTION_LANGUAGE",    "FAAS_FUNCTION_LD_LIBRARY_PATH", "FAAS_FUNCTION_NODE_PATH",
        "FAAS_FUNCTION_PYTHON_PATH", "FAAS_FUNCTION_JAVA_PATH",
    };
    uint64_t currSize = 0;
    for (const auto &env : envs) {
        if (env.second.empty()) {
            YRLOG_WARN("(funcMeta)environment {} does not contain the value or the value is empty", env.first);
        }
        if (systemReservedFieldsEnum.find(env.first) != systemReservedFieldsEnum.end()) {
            YRLOG_ERROR("(funcMeta)env: {} is system reserved fields", env.first);
            return false;
        }
        currSize += env.first.size() + env.second.size();
        if (currSize > ENV_LENGTH_LIMIT) {
            YRLOG_ERROR("(funcMeta)env: total size reach limit of {} bytes", ENV_LENGTH_LIMIT);
            return false;
        }
    }
    return true;
}

bool CheckLayerName(const std::string &layerName)
{
    if (layerName.empty()) {
        YRLOG_ERROR("(funcMeta)layer name is empty");
        return false;
    }

    if (!std::regex_match(layerName, std::regex(LAYER_NAME_REGEX))) {
        YRLOG_ERROR("(funcMeta)layer name({}) is invalid.", layerName);
        return false;
    }
    return true;
}

bool ParseAndCheckLayerVersion(const std::string &layerVersion)
{
    int32_t versionNum = -1;
    try {
        versionNum = std::stoi(layerVersion);
    } catch (std::invalid_argument const &ex) {
        YRLOG_ERROR("(funcMeta)the type of version's value({}) is not INT", layerVersion);
        return false;
    } catch (std::out_of_range const &ex) {
        YRLOG_ERROR("(funcMeta)the value({}) of version should be less than or equal to {}", layerVersion,
                    MAX_LAYER_VERSION);
        return false;
    }
    if (versionNum <= 0) {
        YRLOG_ERROR("(funcMeta)the value of version should be greater than 0");
        return false;
    }
    if (versionNum > MAX_LAYER_VERSION) {
        YRLOG_ERROR("(funcMeta)the value of version should be less than or equal to 1000000");
        return false;
    }
    return true;
}

bool CheckFunctionRefLayer(const std::string &refLayer)
{
    auto split = litebus::strings::Split(refLayer, ":");
    if (split.size() != REFERENCE_LAYER_SPLIT_SIZE) {
        YRLOG_ERROR("(funcMeta)incorrect format of the function reference layer {}. Standard format layerName:version",
                    refLayer);
        return false;
    }
    auto layerName = split[0];
    if (!CheckLayerName(layerName)) {
        return false;
    }
    if (!ParseAndCheckLayerVersion(split[1])) {
        return false;
    }
    return true;
}

bool CheckFunctionLayers(const std::vector<std::string> &layers)
{
    if (layers.size() == 0) {
        return true;
    }

    if (layers.size() > MAX_LAYERS_SIZE) {
        YRLOG_ERROR("(funcMeta)the number of function reference layers cannot exceed {}", MAX_LAYERS_SIZE);
        return false;
    }

    for (const auto &layer : layers) {
        if (!CheckFunctionRefLayer(layer)) {
            return false;
        }
    }
    return true;
}

bool CheckMinInstance(int64_t minInstance)
{
    if (minInstance < 0) {
        YRLOG_ERROR("(funcMeta)minInstance must be at least 0");
        return false;
    }
    return true;
}

bool CheckMaxInstance(int64_t maxInstance)
{
    if (maxInstance < 1) {
        YRLOG_ERROR("(funcMeta)maxInstance must be at least 1");
        return false;
    }
    if (maxInstance > MAX_MAX_INSTANCE) {
        YRLOG_ERROR("(funcMeta)maxInstance must be less than or equal to {}", maxInstance);
        return false;
    }
    return true;
}

bool IsMinInstanceLargeThanMaxInstance(int64_t minInstance, int64_t maxInstance)
{
    if (minInstance > maxInstance) {
        YRLOG_ERROR("(funcMeta)minInstance({}) is greater than maxInstance({})", minInstance, maxInstance);
        return false;
    }
    return true;
}

bool CheckConcurrentNum(int32_t concurrentNum)
{
    if (concurrentNum < 1) {
        YRLOG_ERROR("(funcMeta)concurrentNum must be at least 1");
        return false;
    }
    if (concurrentNum > MAX_CONCURRENT_NUM) {
        YRLOG_ERROR("(funcMeta)concurrentNum must be less than or equal to {}", MAX_CONCURRENT_NUM);
        return false;
    }
    return true;
}

bool CheckWorkerConfig(const FunctionConfig &function)
{
    return CheckMinInstance(function.minInstance) && CheckMaxInstance(function.maxInstance) &&
           IsMinInstanceLargeThanMaxInstance(function.minInstance, function.maxInstance) &&
           CheckConcurrentNum(function.concurrentNum);
}

std::unordered_map<std::string, std::string> PackHookHandler(FunctionHookHandlerConfig functionHookHandlerConfig)
{
    std::unordered_map<std::string, std::string> hookHandler = {
        { INIT_HANDLER, functionHookHandlerConfig.initHandler },
        { CALL_HANDLER, functionHookHandlerConfig.callHandler },
        { CHECK_POINT_HANDLER, functionHookHandlerConfig.checkpointHandler },
        { RECOVER_HANDLER, functionHookHandlerConfig.recoverHandler },
        { SHUTDOWN_HANDLER, functionHookHandlerConfig.shutdownHandler },
        { SIGNAL_HANDLER, functionHookHandlerConfig.signalHandler },
        { HEALTH_HANDLER, functionHookHandlerConfig.healthHandler }
    };
    for (auto iter = hookHandler.begin(); iter != hookHandler.end();) {
        if (iter->second == "") {
            (void)hookHandler.erase(iter++);
        } else {
            (void)iter++;
        }
    }
    return hookHandler;
}

struct HandlerRegexInfo {
    std::string regex;
    uint32_t maxLen;
};

bool CheckHookHandlerRegularization(const std::string &handler, const std::string &runtime)
{
    static std::unordered_map<std::string, HandlerRegexInfo> handlerRegexInfos = {
        { CPP_RUNTIME_VERSION, { "", CPP_HANDLER_MAX_LENGTH } },
        { PYTHON_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON3_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON37_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON38_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON39_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON310_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { PYTHON311_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { GO_RUNTIME_VERSION, { DEFAULT_HANDLER_REGEX, DEFAULT_HANDLER_MAX_LENGTH } },
        { JAVA_RUNTIME_VERSION, { JAVA_HANDLER_REGEX, JAVA_HANDLER_MAX_LENGTH } }
    };

    auto iter = handlerRegexInfos.find(runtime);
    if (iter == handlerRegexInfos.end()) {
        YRLOG_ERROR("(funcMeta)handler can not support this runtime({})", runtime);
        return false;
    }
    if (handler.size() > iter->second.maxLen) {
        YRLOG_ERROR("funcMeta len({}) of handler({}) is too long, runtime: {}, max len: {}", handler.size(), handler,
                    runtime, iter->second.maxLen);
        return false;
    }

    if (iter->second.regex != "") {
        if (!std::regex_match(handler, std::regex(iter->second.regex))) {
            YRLOG_ERROR("(funcMeta)handler({}) is invalid.", handler);
            return false;
        }
    }
    return true;
}

bool CheckHookHandler(const FunctionHookHandlerConfig &functionHookHandlerConfig, const std::string &runtime)
{
    auto handlerMap = PackHookHandler(functionHookHandlerConfig);
    if (handlerMap.size() == 0) {
        return true;
    }
    if (handlerMap.find(CHECK_POINT_HANDLER) != handlerMap.end() &&
        handlerMap.find(RECOVER_HANDLER) != handlerMap.end()) {
        YRLOG_ERROR("(funcMeta)checkpoint and recover must exist at the same time");
        return false;
    }
    for (auto &iter : handlerMap) {
        if (!CheckHookHandlerRegularization(iter.second, runtime)) {
            return false;
        }
    }

    return true;
}

bool CheckFunctionConfig(const FunctionConfig &functionConfig)
{
    return CheckRuntime(functionConfig.runtime) && CheckCPUAndMemorySize(functionConfig.cpu, functionConfig.memory) &&
           CheckEnv(functionConfig.environment) && CheckFunctionLayers(functionConfig.layers) &&
           CheckWorkerConfig(functionConfig) &&
           CheckHookHandler(functionConfig.functionHookHandlerConfig, functionConfig.runtime);
}

bool CheckFunction(const std::string &functionName, const FunctionConfig &functionConfig)
{
    return CheckFunctionName(functionName) && CheckFunctionConfig(functionConfig);
}

bool CheckServiceInfos(std::vector<service_json::ServiceInfo> &serviceInfos)
{
    for (auto &serviceInfo : serviceInfos) {
        if (!CheckServiceInfo(serviceInfo)) {
            return false;
        }
        for (auto &iter : serviceInfo.functions) {
            if (!CheckFunction(iter.first, iter.second)) {
                return false;
            }
        }
    }
    return true;
}

void ParseFunctionHookHandlerConfig(service_json::FunctionHookHandlerConfig &functionHookHandlerConfig,
                                    const nlohmann::json &h)

{
    if (h.find("initHandler") != h.end()) {
        functionHookHandlerConfig.initHandler = h.at("initHandler");
    }
    if (h.find("callHandler") != h.end()) {
        functionHookHandlerConfig.callHandler = h.at("callHandler");
    }
    if (h.find("checkpointHandler") != h.end()) {
        functionHookHandlerConfig.checkpointHandler = h.at("checkpointHandler");
    }
    if (h.find("recoverHandler") != h.end()) {
        functionHookHandlerConfig.recoverHandler = h.at("recoverHandler");
    }
    if (h.find("shutdownHandler") != h.end()) {
        functionHookHandlerConfig.shutdownHandler = h.at("shutdownHandler");
    }
    if (h.find("signalHandler") != h.end()) {
        functionHookHandlerConfig.signalHandler = h.at("signalHandler");
    }
    if (h.find("healthHandler") != h.end()) {
        functionHookHandlerConfig.healthHandler = h.at("healthHandler");
    }
}

void ParseCodeMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f)
{
    if (f.find("layers") != f.end()) {
        nlohmann::json layers = f.at("layers");
        for (uint32_t i = 0; i < layers.size(); ++i) {
            functionConfig.layers.push_back(layers[i]);
        }
    }

    if (f.find("storageType") != f.end()) {
        functionConfig.storageType = f.at("storageType");
    }
    if (f.find("codePath") != f.end()) {
        functionConfig.codePath = f.at("codePath");
    }
}

void ParseEnvMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f)
{
    if (f.find("environment") != f.end()) {
        nlohmann::json envs = f.at("environment");
        for (const auto &env : envs.items()) {
            functionConfig.environment[env.key()] = env.value();
        }
    }
    if (f.find("encryptedEnvStr") != f.end()) {
        functionConfig.encryptedEnvStr = f.at("encryptedEnvStr");
    }
}

void ParseInstMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f)
{
    if (f.find("minInstance") != f.end()) {
        try {
            auto minInstance = std::stoi(f.at("minInstance").get<std::string>());
            if (minInstance > 0) {
                functionConfig.minInstance = minInstance;
            }
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse minInstance, e:{}", e.what());
        }
    }
    if (f.find("maxInstance") != f.end()) {
        try {
            auto maxInstance = std::stoi(f.at("maxInstance").get<std::string>());
            if (maxInstance > 0) {
                functionConfig.maxInstance = maxInstance;
            }
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse maxInstance, e:{}", e.what());
        }
    }
    if (f.find("concurrentNum") != f.end()) {
        try {
            auto concurrentNum = std::stoi(f.at("concurrentNum").get<std::string>());
            if (concurrentNum > 0) {
                functionConfig.concurrentNum = concurrentNum;
            }
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse concurrentNum, e:{}", e.what());
        }
    }
    if (f.find("cacheInstance") != f.end()) {
        try {
            functionConfig.cacheInstance = std::stoi(f.at("cacheInstance").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse cacheInstance, e:{}", e.what());
        }
    }
}

void ParseRes(service_json::FunctionConfig &functionConfig, const nlohmann::json &f)
{
    if (f.find("cpu") != f.end()) {
        try {
            auto cpuVal = std::stol(f.at("cpu").get<std::string>());
            if (cpuVal > 0) {
                functionConfig.cpu = cpuVal;
            }
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse cpu, e:{}", e.what());
        }
    }
    if (f.find("memory") != f.end()) {
        try {
            auto memVal = std::stol(f.at("memory").get<std::string>());
            if (memVal > 0) {
                functionConfig.memory = memVal;
            }
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse memory, e:{}", e.what());
        }
    }

    if (f.find("customResources") != f.end()) {
        YRLOG_WARN("doesn't support custom resources");
    }
}

void Parsefunction(service_json::FunctionConfig &functionConfig, const nlohmann::json &f)
{
    ParseInstMeta(functionConfig, f);

    if (f.find("handler") != f.end()) {
        functionConfig.handler = f.at("handler");
    }
    if (f.find("initializer") != f.end()) {
        functionConfig.initializer = f.at("initializer");
    }
    if (f.find("initializerTimeout") != f.end()) {
        try {
            functionConfig.initializerTimeout = std::stoi(f.at("initializerTimeout").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse initializerTimeout, e:{}", e.what());
        }
    }
    if (f.find("preStopHandler") != f.end()) {
        functionConfig.prestop = f.at("preStopHandler");
    }
    if (f.find("preStopTimeout") != f.end()) {
        try {
            functionConfig.preStopTimeout = std::stoi(f.at("preStopTimeout").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse preStopTimeout, e:{}", e.what());
        }
    }
    if (f.find("description") != f.end()) {
        functionConfig.description = f.at("description");
    }

    if (f.find("runtime") != f.end()) {
        functionConfig.runtime = f.at("runtime");
    }

    if (f.find("timeout") != f.end()) {
        try {
            functionConfig.timeout = std::stoi(f.at("timeout").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse timeout, e:{}", e.what());
        }
    }

    ParseCodeMeta(functionConfig, f);

    ParseEnvMeta(functionConfig, f);

    ParseRes(functionConfig, f);

    ParseFunctionHookHandlerConfig(functionConfig.functionHookHandlerConfig, f);

    ParseDeviceInfo(functionConfig.device, f);
}

void ParseDeviceInfo(DeviceMetaData &device, const nlohmann::json &h)
{
    if (h.find("device") == h.end()) {
        return;
    }
    nlohmann::json dev = h.at("device");
    if (dev.find("model") != dev.end()) {
        device.model = dev.at("model");
    }
    if (dev.find("hbm") != dev.end()) {
        try {
            device.hbm = std::stof(dev.at("hbm").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse hbm, e:{}", e.what());
        }
    }
    if (dev.find("count") != dev.end()) {
        try {
            device.count = std::stoul(dev.at("count").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse count, e:{}", e.what());
        }
    }
    if (dev.find("stream") != dev.end()) {
        try {
            device.stream = std::stoul(dev.at("stream").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse stream, e:{}", e.what());
        }
    }
    if (dev.find("latency") != dev.end()) {
        try {
            device.latency = std::stof(dev.at("latency").get<std::string>());
        } catch (std::exception &e) {
            YRLOG_WARN("failed to parse latency, e:{}", e.what());
        }
    }
    if (dev.find("type") != dev.end()) {
        device.type = dev.at("type");
    }
}

void ParseServiceInfo(std::vector<service_json::ServiceInfo> &serviceInfos, const nlohmann::json &j)
{
    for (uint32_t i = 0; i < j.size(); i++) {
        nlohmann::json s = j[i];
        service_json::ServiceInfo serviceInfo;
        if (s.find("service") != s.end()) {
            serviceInfo.service = s.at("service");
        }

        if (s.find("kind") != s.end()) {
            serviceInfo.kind = s.at("kind");
        }

        if (s.find("description") != s.end()) {
            serviceInfo.description = s.at("description");
        }

        if (s.find("functions") != s.end()) {
            nlohmann::json functions = s.at("functions");
            for (auto &func : functions.items()) {
                service_json::FunctionConfig functionConfig;
                Parsefunction(functionConfig, func.value());
                (void)serviceInfo.functions.emplace(func.key(), functionConfig);
            }
        }
        (void)serviceInfos.emplace_back(serviceInfo);
    }
}

litebus::Option<std::vector<service_json::ServiceInfo>> GetServiceInfosFromJson(const std::string &jsonStr)
{
    std::vector<service_json::ServiceInfo> serviceInfos;
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse service info, error: {}", error.what());
        return {};
    }
    ParseServiceInfo(serviceInfos, j);

    if (!CheckServiceInfos(serviceInfos)) {
        YRLOG_WARN("failed to check service infos");
        return {};
    }

    return serviceInfos;
}

litebus::Option<FunctionMeta> BuildFunctionMeta(const ServiceInfo &serviceInfo, const FunctionConfig &functionConfig,
                                                const std::string &functionName, const std::string &yamlPath)
{
    auto mapBuilder = GetBuilder(serviceInfo.kind, functionConfig.runtime);
    if (mapBuilder == nullptr) {
        return {};
    }
    auto env = BuildEnv(functionConfig);
    if (env.IsNone()) {
        return {};
    }
    auto resources = BuildResources(functionConfig.cpu, functionConfig.memory);
    return FunctionMeta{ .funcMetaData = BuildFuncMetaData(serviceInfo, functionConfig, functionName, mapBuilder),
                         .codeMetaData = CodeMetaData{ .storageType = "local",
                                                       .bucketID = "",
                                                       .objectID = "",
                                                       .bucketUrl = "",
                                                       .layers = {},
                                                       .deployDir = ParseCodePath(functionConfig.codePath, yamlPath),
                                                       .sha512 = "",
                                                       .appId = ""},
                         .envMetaData = env.Get(),
                         .resources = resources,
                         .extendedMetaData = ExtendedMetaData{
                             .instanceMetaData = InstanceMetaData{ .maxInstance = functionConfig.minInstance,
                                                                   .minInstance = functionConfig.maxInstance,
                                                                   .concurrentNum = functionConfig.concurrentNum,
                                                                   .cacheInstance = functionConfig.cacheInstance,
                                                                   .diskLimit = 0,
                                                                   .scalePolicy = "" },
                             .mountConfig = {},
                             .deviceMetaData = { .hbm = functionConfig.device.hbm,
                                                .latency = functionConfig.device.latency,
                                                .stream = functionConfig.device.stream,
                                                .count = functionConfig.device.count,
                                                .model = functionConfig.device.model,
                                                .type = functionConfig.device.type },
                             .initializer = {}, .userAgency = {}, .customGracefulShutdown = {} },
                         .instanceMetaData = {},
                         .rawJsonStr = ""};
}

litebus::Option<std::vector<FunctionMeta>> ConvertFunctionMeta(const std::vector<ServiceInfo> &serviceInfos,
                                                               const std::string &yamlPath)
{
    std::vector<FunctionMeta> data;

    for (auto &serviceInfo : serviceInfos) {
        for (const auto &function : serviceInfo.functions) {
            auto meta = BuildFunctionMeta(serviceInfo, function.second, function.first, yamlPath);
            if (meta.IsNone()) {
                return {};
            }
            (void)data.emplace_back(meta.Get());
        }
    }
    return data;
}

litebus::Option<std::vector<FunctionMeta>> GetFuncMetaFromServiceYaml(const std::string &filePath,
                                                                      const std::string &libPath)
{
    auto realFilePathOpt = litebus::os::RealPath(filePath);
    if (realFilePathOpt.IsNone()) {
        YRLOG_WARN("(funcMeta)failed to get real path of file({})", filePath);
        return {};
    }
    auto realFilePath = realFilePathOpt.Get();
    if (!litebus::os::ExistPath(realFilePath)) {
        YRLOG_WARN("(funcMeta)file({}) is not exist", realFilePath);
        return {};
    }

    auto realLibPathOpt = litebus::os::RealPath(libPath);
    if (realLibPathOpt.IsNone()) {
        YRLOG_WARN("(funcMeta)failed to get real path of lib({})", libPath);
        return {};
    }
    auto realLibPath = realLibPathOpt.Get();
    if (!litebus::os::ExistPath(realLibPath)) {
        YRLOG_WARN("(funcMeta)lib({}) is not exit", realLibPath);
        return {};
    }

    auto data = Read(realFilePath);
    if (data.empty()) {
        YRLOG_WARN("(funcMeta)no function meta information in {}", realFilePath);
        return {};
    }

    void *handle = dlopen(realLibPath.c_str(), RTLD_NOW);
    if (handle == nullptr) {
        YRLOG_ERROR("(funcMeta)handler of lib is nullptr");
        return {};
    }

    auto yamlToJsonFunc = reinterpret_cast<YamlToJsonFunc>(dlsym(handle, "YamlToJson"));
    if (yamlToJsonFunc == nullptr) {
        YRLOG_ERROR("(funcMeta)func of lib is nullptr");
        (void)dlclose(handle);
        return {};
    }

    auto jsonStr = yamlToJsonFunc(data);

    (void)dlclose(handle);

    auto serviceInfosOpt = GetServiceInfosFromJson(jsonStr);
    if (serviceInfosOpt.IsNone()) {
        YRLOG_ERROR("(funcMeta)failed to get services info");
        return {};
    }
    return ConvertFunctionMeta(serviceInfosOpt.Get(), realFilePath);
}

const std::string YAML_LIB_NAME = "libyaml_tool.so";

void LoadFuncMetaFromServiceYaml(std::unordered_map<std::string, FunctionMeta> &map, const std::string &filePath,
                                 const std::string &libPath)
{
    if (filePath.empty()) {
        YRLOG_WARN("(funcMeta)file({}) is empty", filePath);
        return;
    }

    if (libPath.empty()) {
        YRLOG_WARN("(funcMeta)lib({}) is empty", libPath);
        return;
    }

    try {
        auto functionMeta =
            service_json::GetFuncMetaFromServiceYaml(filePath, litebus::os::Join(libPath, YAML_LIB_NAME));
        if (functionMeta.IsNone()) {
            YRLOG_ERROR("(funcMeta)failed to read function meta");
            return;
        }

        for (const auto &meta : functionMeta.Get()) {
            auto funcKey = GetFuncName(meta.funcMetaData.name, meta.funcMetaData.version, meta.funcMetaData.tenantId);
            if (funcKey.IsNone()) {
                YRLOG_ERROR("(funcMeta)failed to get func name , name: {}, version: {}, tenantID: {}",
                            meta.funcMetaData.name, meta.funcMetaData.version, meta.funcMetaData.tenantId);
                return;
            }
            map[funcKey.Get()] = meta;
            YRLOG_INFO("(funcMeta)load function meta ({})", funcKey.Get());
        }
    } catch (std::exception &e) {
        YRLOG_WARN("(funcMeta)function metadata is invalid, filePath: {}, libPath: {}, error: {}", filePath, libPath,
                   e.what());
    } catch (...) {
        YRLOG_WARN("(funcMeta)function metadata is invalid, filePath: {}, libPath: {}", filePath, libPath);
    }
    YRLOG_INFO("(funcMeta)load local function meta from service yaml successfully");
}
}  // namespace functionsystem::service_json