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

#ifndef COMMON_SERVICE_JSON_SERVICE_INFO_H
#define COMMON_SERVICE_JSON_SERVICE_INFO_H

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/metadata/metadata.h"

namespace functionsystem::service_json {

const int64_t CUSTOM_LIMIT_CPU = 4000;
const int64_t CUSTOM_REQUEST_CPU = 0;
const int64_t CUSTOM_LIMIT_MEM = 16000;
const int64_t CUSTOM_REQUEST_MEM = 0;

const int32_t DEFAULT_MIN_INSTANCE = 0;
const int32_t DEFAULT_MAX_INSTANCE = 100;
const int64_t DEFAULT_CPU = 0;
const int64_t DEFAULT_MEMORY = 0;
const int64_t DEFAULT_CONCURRENT_NUM = 100;
const int64_t DEFAULT_TIME_OUT_MS = 900;

const int64_t ENV_LENGTH_LIMIT = 4 * (1 << 10);

const int32_t MAX_LAYERS_SIZE = 5;
const int32_t REFERENCE_LAYER_SPLIT_SIZE = 2;

const int32_t MAX_LAYER_VERSION = 1000000;

const int64_t MAX_MAX_INSTANCE = 1000;

const int32_t MAX_CONCURRENT_NUM = 100;

const std::string SERVICE_NAME_REGEX = "^[0-9a-z]{1,16}$";
const std::string FUNCTION_NAME_REGEX = "^[a-z][a-z0-9-]{0,126}[a-z0-9]$|^[a-z]$";
const std::string LAYER_NAME_REGEX = "^[a-z][0-9a-z-]{0,30}[0-9a-z]$";

const std::string DEFAULT_HANDLER_REGEX = "^[A-Za-z0-9-_]+\\.[A-Za-z0-9-_]+$";
const int32_t DEFAULT_HANDLER_MAX_LENGTH = 64;

const std::string JAVA_HANDLER_REGEX =
    "^([a-zA-Z_][0-9a-zA-Z_]{0,23}\\.){0,8}[a-zA-Z_][0-9a-zA-Z_]{0,23}(::[a-zA-Z_][0-9a-zA-Z_]{0,23})?$";

const int32_t CPP_HANDLER_MAX_LENGTH = 256;
const int32_t JAVA_HANDLER_MAX_LENGTH = 256;

const std::string INIT_HANDLER = "init";
const std::string CALL_HANDLER = "call";
const std::string CHECK_POINT_HANDLER = "checkpoint";
const std::string RECOVER_HANDLER = "recover";
const std::string SHUTDOWN_HANDLER = "shutdown";
const std::string SIGNAL_HANDLER = "signal";
const std::string HEALTH_HANDLER = "health";

const std::string CPP_RUNTIME_VERSION = "cpp11";
const std::string JAVA_RUNTIME_VERSION = "java1.8";
const std::string JAVA11_RUNTIME_VERSION = "java11";
const std::string JAVA17_RUNTIME_VERSION = "java17";
const std::string JAVA21_RUNTIME_VERSION = "java21";
const std::string PYTHON_RUNTIME_VERSION = "python";
const std::string PYTHON3_RUNTIME_VERSION = "python3";
const std::string PYTHON36_RUNTIME_VERSION = "python3.6";
const std::string PYTHON37_RUNTIME_VERSION = "python3.7";
const std::string PYTHON38_RUNTIME_VERSION = "python3.8";
const std::string PYTHON39_RUNTIME_VERSION = "python3.9";
const std::string PYTHON310_RUNTIME_VERSION = "python3.10";
const std::string PYTHON311_RUNTIME_VERSION = "python3.11";
const std::string GO_RUNTIME_VERSION = "go1.13";
const std::string POSIX_CUSTOM_RUNTIME_VERSION = "posix-custom-runtime";

const std::string LATEST_VERSION = "$latest";

const std::string DEFAULT_TENANT_ID = "12345678901234561234567890123456";
const std::string DEFAULT_STORAGE_TYPE = "local";

const std::string FAAS = "faas";
const std::string YR_LIB = "yrlib";
const std::string CUSTOM = "custom";
const std::string POSIX_RUNTIME_CUSTOM = "posix-runtime-custom";

struct FunctionHookHandlerConfig {
    std::string initHandler;
    std::string callHandler;
    std::string checkpointHandler;
    std::string recoverHandler;
    std::string shutdownHandler;
    std::string signalHandler;
    std::string healthHandler;
};

struct FunctionConfig {
    int32_t minInstance{ DEFAULT_MIN_INSTANCE };
    int32_t maxInstance{ DEFAULT_MAX_INSTANCE };
    int32_t concurrentNum{ DEFAULT_CONCURRENT_NUM };
    std::string handler;
    std::string initializer;
    int32_t initializerTimeout{ 0 };
    std::string prestop;
    int32_t preStopTimeout{ 0 };
    std::string description;
    std::unordered_map<std::string, std::string> environment;
    std::string encryptedEnvStr;
    std::unordered_map<std::string, std::string> customResources;
    std::string runtime;
    int64_t memory{ DEFAULT_MEMORY };
    int64_t timeout{ DEFAULT_TIME_OUT_MS };
    std::vector<std::string> layers;
    int64_t cpu{ DEFAULT_CPU };
    std::string storageType{ DEFAULT_STORAGE_TYPE };
    std::string codePath;
    int32_t cacheInstance{ 0 };
    FunctionHookHandlerConfig functionHookHandlerConfig;
    DeviceMetaData device;
};

struct ServiceInfo {
    std::string service;
    std::string kind;
    std::string description;
    std::unordered_map<std::string, FunctionConfig> functions;
};

}  // namespace functionsystem::service_json

#endif  // COMMON_SERVICE_JSON_SERVICE_INFO_H
