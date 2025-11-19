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


#include "static_function_util.h"

namespace functionsystem::function_agent {
const std::string CONCURRENT_NUM_KEY = "ConcurrentNum";
const std::string CALL_TIMEOUT_KEY = "call_timeout";
const std::string CUSTOM_CONTAINER_RUNTIME_TYPE = "custom image";
const std::string HTTP_RUNTIME_TYPE = "http";
const std::string DEFAULT_DIRECTORY_LIMIT = "512";
const uint32_t INCREMENT_TIMEOUT = 5;
const std::string STORAGE_TYPE_KEY = "storage_type";
const std::string GRACEFUL_SHUTDOWN_TIME_KEY = "GRACEFUL_SHUTDOWN_TIME";
const uint32_t MAX_SHUTDOWN_TIMEOUT = 900;
const std::string INSTANCE_TYPE_NOTE_KEY = "INSTANCE_TYPE_NOTE";
const std::string INSTANCE_LABEL_NOTE_KEY = "INSTANCE_LABEL_NOTE";
const std::string FUNCTION_KEY_NOTE_KEY = "FUNCTION_KEY_NOTE";
const std::string FUNCTION_KEY_SPLIT = "/";
const std::string RESOURCE_SPEC_NOTE_KEY = "RESOURCE_SPEC_NOTE";
const std::string FUNCTION_SIGNATURE_KEY = "FUNCTION_SIGNATURE";
const size_t INSTANCE_ARGS_SIZE = 6;
const size_t LIBRUNTIME_ARGS_INDEX = 0;
const size_t FUNCTION_META_ARGS_INDEX = 1;
const size_t CREATE_PARAM_ARGS_INDEX = 2;
const size_t SCHEDLER_ARGS_INDEX = 3;
const size_t CREATE_EVENT_ARGS_INDEX = 4;
const size_t GLOBAL_CONFIG_ARGS_INDEX = 5;
const size_t PREFIX_EMPTY_SIZE = 16;

const uint16_t HTTP_FUNC_PORT = 8000;
const std::string HTTP_CALL_ROUTE = "invoke";

FunctionLanguageInfo GetLanguageInfo(const FunctionMeta &funcMeta)
{
    std::string runtime = funcMeta.funcMetaData.runtime;
    std::string executorFormat = "12345678901234561234567890123456/0-system-faasExecutor{}/$latest";
    if (runtime.find("python3.6") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Python3.6"), resources::LanguageType::Python };
    }
    if (runtime.find("python3.8") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Python3.8"), resources::LanguageType::Python };
    }
    if (runtime.find("python3.9") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Python3.9"), resources::LanguageType::Python };
    }
    if (runtime.find("python3.11") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Python3.11"), resources::LanguageType::Python };
    }
    if (runtime.find("go") != std::string::npos || runtime.find("http") != std::string::npos
        || runtime.find("custom image") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Go1.x"), resources::LanguageType::Golang };
    }
    if (runtime.find("java8") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Java8"), resources::LanguageType::Java };
    }
    if (runtime.find("java11") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Java11"), resources::LanguageType::Java };
    }
    if (runtime.find("java17") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Java17"), resources::LanguageType::Java };
    }
    if (runtime.find("java21") != std::string::npos) {
        return FunctionLanguageInfo{ fmt::format(executorFormat, "Java21"), resources::LanguageType::Java };
    }
    return {};
}

std::string LayerToDelegateDownloadInfo(const std::vector<Layer> &layers)
{
    nlohmann::json info;
    for (const auto &layer : layers) {
        nlohmann::json json;
        json["appId"] = layer.appID;
        json["bucketId"] = layer.bucketID;
        json["objectId"] = layer.objectID;
        json["hostName"] = layer.hostName;
        json["securityToken"] = layer.securityToken;
        json["temporaryAccessKey"] = layer.temporaryAccessKey;
        json["temporarySecretKey"] = layer.temporarySecretKey;
        json["sha256"] = layer.sha256;
        json["sha512"] = layer.sha512;
        json["storage_type"] = layer.storageType;
        json["code_path"] = layer.codePath;
        info.push_back(json);
    }
    return info.dump();
}

void BuildMetaConfig(resources::MetaConfig &config, const FunctionLanguageInfo &info, const FunctionMeta &meta)
{
    int recycleTime = 2;
    int maxTaskInstanceNum = -1;
    int maxConcurrencyCreateNum = 100;
    uint32_t threadPoolSize = 0;
    uint32_t localThreadPoolSize = 0;

    config.set_jobid("");
    config.set_recycletime(recycleTime);
    config.set_maxtaskinstancenum(maxTaskInstanceNum);
    config.set_maxconcurrencycreatenum(maxConcurrencyCreateNum);
    config.set_threadpoolsize(threadPoolSize);
    config.set_localthreadpoolsize(localThreadPoolSize);
    config.set_enablemetrics(true);

    config.add_codepaths(meta.extendedMetaData.initializer.handler);
    config.add_codepaths(meta.funcMetaData.staticHandler);

    auto funcId = config.add_functionids();
    funcId->set_language(info.languageType);
    funcId->set_functionid(info.functionId);
}

void BuildCreateOption(const std::shared_ptr<messages::ScheduleRequest> &request, const FunctionMeta &meta,
                       const StaticFunctionConfig &config)
{
    const auto createOption = request->mutable_instance()->mutable_createoptions();
    (*createOption)[CONCURRENT_NUM_KEY] = std::to_string(meta.instanceMetaData.concurrentNum);

    (*createOption)[DELEGATE_DIRECTORY_INFO] = config.delegateDirectoryInfo;
    if (meta.instanceMetaData.diskLimit == 0) {
        (*createOption)[DELEGATE_DIRECTORY_QUOTA] = DEFAULT_DIRECTORY_LIMIT;
    } else {
        (*createOption)[DELEGATE_DIRECTORY_QUOTA] = std::to_string(meta.instanceMetaData.diskLimit);
    }

    (*createOption)[INIT_CALL_TIMEOUT] = std::to_string(meta.extendedMetaData.initializer.timeout + INCREMENT_TIMEOUT);
    (*createOption)[CALL_TIMEOUT_KEY] = std::to_string(meta.funcMetaData.timeout);
    if (meta.extendedMetaData.customGracefulShutdown.maxShutdownTimeout > 0) {
        (*createOption)[GRACEFUL_SHUTDOWN_TIME_KEY] =
            std::to_string(meta.extendedMetaData.customGracefulShutdown.maxShutdownTimeout);
        request->mutable_instance()->set_gracefulshutdowntime(
            meta.extendedMetaData.customGracefulShutdown.maxShutdownTimeout);
    } else {
        (*createOption)[GRACEFUL_SHUTDOWN_TIME_KEY] = std::to_string(MAX_SHUTDOWN_TIMEOUT);
        request->mutable_instance()->set_gracefulshutdowntime(MAX_SHUTDOWN_TIMEOUT);
    }

    (*createOption)[FUNCTION_KEY_NOTE_KEY] = meta.funcMetaData.tenantId + FUNCTION_KEY_SPLIT + meta.funcMetaData.name
                                             + FUNCTION_KEY_SPLIT + meta.funcMetaData.version;
    nlohmann::json resourceNoteJson;
    if (meta.resources.resources().contains(CPU_RESOURCE_NAME)) {
        resourceNoteJson["cpu"] =
            static_cast<uint64_t>(meta.resources.resources().at(CPU_RESOURCE_NAME).scalar().value());
    }
    if (meta.resources.resources().contains(MEMORY_RESOURCE_NAME)) {
        resourceNoteJson["memory"] =
            static_cast<uint64_t>(meta.resources.resources().at(MEMORY_RESOURCE_NAME).scalar().value());
    }
    resourceNoteJson["invokeLabels"] = config.invokeLabels;
    (*createOption)[RESOURCE_SPEC_NOTE_KEY] = resourceNoteJson.dump();
    (*createOption)[INSTANCE_TYPE_NOTE_KEY] = config.instanceTypeNote;
    (*createOption)[INSTANCE_LABEL_NOTE_KEY] = config.invokeLabels;
    (*createOption)[FUNCTION_SIGNATURE_KEY] = config.functionSignature;
    (*createOption)[TENANT_ID] = meta.funcMetaData.tenantId;
    (*createOption)["lifecycle"] = "detached";

    // udf 通过 createOption.DELEGATE_DOWNLOAD 实现下载和访问
    nlohmann::json containerJson;
    containerJson[STORAGE_TYPE_KEY] = meta.codeMetaData.storageType;
    containerJson["sha512"] = meta.funcMetaData.codeSha512;
    containerJson["appId"] = meta.codeMetaData.appId;
    containerJson["bucketId"] = meta.codeMetaData.bucketID;
    containerJson["objectId"] = meta.codeMetaData.objectID;
    if (meta.funcMetaData.runtime != CUSTOM_CONTAINER_RUNTIME_TYPE) {
        (*createOption)[DELEGATE_DOWNLOAD] = containerJson.dump();
    }
    if (!meta.codeMetaData.layers.empty()) {
        (*createOption)[DELEGATE_DOWNLOAD] = LayerToDelegateDownloadInfo(meta.codeMetaData.layers);
    }

    // createOption.DELEGATE_DECRYPT
    const nlohmann::json encryptJson = { { "secretKey", meta.extendedMetaData.userAgency.secretKey },
                                         { "accessKey", meta.extendedMetaData.userAgency.accessKey },
                                         { "authToken", meta.extendedMetaData.userAgency.token },
                                         { "securityAk", meta.extendedMetaData.userAgency.securityAk },
                                         { "securitySk", meta.extendedMetaData.userAgency.securitySk },
                                         { "securityToken", meta.extendedMetaData.userAgency.securityToken },
                                         { "environment", meta.envMetaData.envInfo },
                                         { "encrypted_user_data", meta.envMetaData.encryptedUserData },
                                         { "envKey", meta.envMetaData.envKey },
                                         { "cryptoAlgorithm", meta.envMetaData.cryptoAlgorithm } };
    (*createOption)[DELEGATE_DECRYPT] = encryptJson.dump();
}

void BuildCreateArgs(const std::shared_ptr<messages::ScheduleRequest> &request, const FunctionMeta &meta,
                     const StaticFunctionConfig &config)
{
    auto args = request->mutable_instance()->mutable_args();
    std::string prefixStr(PREFIX_EMPTY_SIZE, '\0');
    for (size_t i = 0; i < INSTANCE_ARGS_SIZE; i++) {
        ::common::Arg arg{};
        arg.set_type(::common::Arg_ArgType_VALUE);
        args->Add(std::move(arg));
    }
    // args0 libruntime meta
    ::resources::MetaData metaData;
    metaData.set_invoketype(resources::InvokeType::CreateInstance);
    auto functionMeta = metaData.mutable_functionmeta();
    functionMeta->set_apitype(resources::ApiType::Faas);
    auto functionInfo = GetLanguageInfo(meta);
    functionMeta->set_functionid(functionInfo.functionId);
    auto metaConfig = metaData.mutable_config();
    BuildMetaConfig(*metaConfig, functionInfo, meta);
    args->at(LIBRUNTIME_ARGS_INDEX).set_value(metaData.SerializeAsString());

    // args1 faas function meta
    args->at(FUNCTION_META_ARGS_INDEX).set_value(prefixStr + meta.rawJsonStr);

    // args2 instanceLabel + handle
    nlohmann::json createParamsData = {};
    if (!config.invokeLabels.empty()) {
        createParamsData["instanceLabel"] = config.invokeLabels;
    }
    if (meta.funcMetaData.runtime == HTTP_RUNTIME_TYPE || meta.funcMetaData.runtime == CUSTOM_CONTAINER_RUNTIME_TYPE) {
        createParamsData["port"] = HTTP_FUNC_PORT;
        createParamsData["callRoute"] = HTTP_CALL_ROUTE;
    } else {
        if (!meta.extendedMetaData.initializer.handler.empty()) {
            createParamsData["userInitEntry"] = meta.extendedMetaData.initializer.handler;
        }
        createParamsData["userCallEntry"] = meta.funcMetaData.staticHandler;
    }
    args->at(CREATE_PARAM_ARGS_INDEX).set_value(prefixStr + createParamsData.dump());

    // args3 faas scheduler
    args->at(SCHEDLER_ARGS_INDEX).set_value(prefixStr + R"({"schedulerFuncKey":"","schedulerIDList":[]})");
    // args4 create event
    args->at(CREATE_EVENT_ARGS_INDEX).set_value(prefixStr + "{}");
    // args5 custom image
    if (meta.funcMetaData.runtime != CUSTOM_CONTAINER_RUNTIME_TYPE) {
        args->RemoveLast();
    } else {
        args->at(GLOBAL_CONFIG_ARGS_INDEX).set_value(prefixStr + "{}");
    }
}

void BuildScheduleRequest(const std::shared_ptr<messages::ScheduleRequest> &request, const FunctionMeta &meta,
                          const StaticFunctionConfig &config)
{
    request->set_initrequest("");
    request->mutable_instance()->mutable_resources()->CopyFrom(meta.resources);

    BuildCreateOption(request, meta, config);
    const auto &createOptions = request->instance().createoptions();
    if (const auto iter(createOptions.find(INIT_CALL_TIMEOUT)); iter != createOptions.end()) {
        uint32_t timeout = 0;
        std::stringstream ss(iter->second);
        ss >> timeout;
        request->mutable_instance()->mutable_scheduleoption()->set_initcalltimeout(timeout);
    }

    BuildCreateArgs(request, meta, config);
}
}  // namespace functionsystem::function_agent