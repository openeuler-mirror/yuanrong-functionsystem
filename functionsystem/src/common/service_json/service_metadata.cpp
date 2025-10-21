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

#include "service_metadata.h"

#include <libgen.h>

namespace functionsystem::service_json {

const std::string ENV_PREFIX = "func-";

std::string GetEnvironmentText(const std::unordered_map<std::string, std::string> &funcEnv)
{
    std::unordered_map<std::string, std::string> envMap;
    if (funcEnv.size() != 0) {
        for (const auto &env : funcEnv) {
            envMap[ENV_PREFIX + env.first] = env.second;
        }
    }

    nlohmann::json obj(envMap);
    try {
        return obj.dump();
    } catch (std::exception &e) {
        YRLOG_ERROR("dump envMap json failed");
        return std::string();
    }
}

litebus::Option<EnvMetaData> BuildEnv(const FunctionConfig &functionConfig)
{
    EnvMetaData env;
    if (!functionConfig.encryptedEnvStr.empty()) {
        YRLOG_ERROR("(service_json)doesn't support encryption");
        return {};
    } else {
        env.envInfo = GetEnvironmentText(functionConfig.environment);
        env.envKey = "";
        env.cryptoAlgorithm = "NO_CRYPTO";
    }

    return env;
}

std::string BuildFuncName(const std::string &serviceName, const std::string &functionName)
{
    return "0-" + serviceName + "-" + functionName;
}

std::string BuildFunctionURN(const std::string &serviceName, const std::string &functionName)
{
    return "sn:cn:yrk:12345678901234561234567890123456:function:" + BuildFuncName(serviceName, functionName);
}

FuncMetaData BuildFuncMetaData(const ServiceInfo &serviceInfo, const FunctionConfig &functionConfig,
                               const std::string &functionName, const std::shared_ptr<BuildHandlerMap> &mapBuilder)
{
    return FuncMetaData{
        urn : BuildFunctionURN(serviceInfo.service, functionName),
        runtime : functionConfig.runtime,
        handler : mapBuilder->Handler(),
        codeSha256 : std::string(),
        codeSha512 : std::string(),
        entryFile : std::string(),
        hookHandler : mapBuilder->HookHandler(functionConfig),
        name : BuildFuncName(serviceInfo.service, functionName),
        version : LATEST_VERSION,
        tenantId : DEFAULT_TENANT_ID,
    };
}

bool IsAbs(const std::string &path)
{
    return (path.length() > 0) && (path[0] == '/');
}

std::string ParseCodePath(const std::string &codePath, const std::string &yamlPath)
{
    if (IsAbs(codePath)) {
        return codePath;
    }
    YRLOG_WARN("codePath is not abs path {}", codePath);
    std::string tempPath = yamlPath;
    std::string yamlDir = dirname(tempPath.data());
    auto yamlDirAbsOpt = litebus::os::RealPath(yamlDir);
    if (yamlDirAbsOpt.IsNone()) {
        YRLOG_ERROR("parseCodePath err: yaml path: {}, yaml dir: {}", yamlPath, yamlDir);
        return codePath;
    }
    return litebus::os::Join(yamlDirAbsOpt.Get(), codePath);
}
}  // namespace functionsystem::service_json