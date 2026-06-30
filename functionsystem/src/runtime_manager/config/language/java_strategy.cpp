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

#include "java_strategy.h"

#include <unistd.h>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/path.h"
#include "runtime_manager/config/build.h"
#include "runtime_manager/utils/utils.h"

namespace functionsystem::runtime_manager {

namespace {
const std::string YR_JAVA_RUNTIME_PATH = "/java/yr-runtime-1.0.0.jar";
const std::string JAVA_MAIN_CLASS = "com.yuanrong.runtime.server.RuntimeServer";
const std::string JAVA_SYSTEM_PROPERTY_FILE = "-Dlog4j2.configurationFile=file:";
const std::string JAVA_SYSTEM_LIBRARY_PATH = "-Djava.library.path=";
const std::string JAVA_LOG_LEVEL = "-DlogLevel=";
const std::string JAVA_JOB_ID = "-DjobId=job-";
const std::string JAVA_CMD = "java";
const std::string DEFAULT_JAVA8_CMD = "/opt/buildtools/jdk8/bin/java";
const std::string RUNTIME_LAYER_DIR_NAME = "layer";
const std::string RUNTIME_FUNC_DIR_NAME = "func";
const std::string MONOPOLY_SCHEDULE = "monopoly";

bool IsUnsafePathComponent(const std::string &value)
{
    if (value.empty()) {
        return false;
    }
    return value.find("..") != std::string::npos || value.find("//") != std::string::npos || value[0] == '/';
}

void AppendJavaMemoryArgs(std::vector<std::string> *args, const messages::RuntimeInstanceInfo &info,
                          const RuntimeConfig &config)
{
    for (const auto &resource : info.runtimeconfig().resources().resources()) {
        if (resource.first != resource_view::MEMORY_RESOURCE_NAME) {
            continue;
        }
        auto memVal = resource.second.scalar().value();
        memVal = memVal > config.maxJvmMemory ? config.maxJvmMemory : memVal;
        if (memVal > 0) {
            args->emplace_back("-Xmx" + std::to_string(static_cast<int>(memVal)) + "m");
        }
        break;
    }
}

void AppendJavaLogConfig(std::vector<std::string> *args, const messages::RuntimeInstanceInfo &info,
                         const RuntimeConfig &config)
{
    if (auto it = info.runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD);
        it != info.runtimeconfig().posixenvs().end()) {
        args->emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config.javaSystemProperty + ',' + it->second
                           + "/config/log4j2.xml");
        YRLOG_INFO("{}|{}|append java log4j config from delegate: {}/config/log4j2.xml", info.traceid(),
                   info.requestid(), it->second);
        return;
    }
    args->emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config.javaSystemProperty);
}

std::pair<Status, std::string> BuildJavaClassPath(const messages::StartInstanceRequest &request,
                                                  const RuntimeConfig &config)
{
    const auto &info = request.runtimeinstanceinfo();
    std::string jarPath = info.deploymentconfig().deploydir();
    if (request.scheduleoption().schedpolicyname() == MONOPOLY_SCHEDULE) {
        return {Status::OK(), config.runtimePath + YR_JAVA_RUNTIME_PATH + ":" + jarPath};
    }

    std::string bucketID = info.deploymentconfig().bucketid();
    std::string objectID = info.deploymentconfig().objectid();
    if (IsUnsafePathComponent(bucketID) || IsUnsafePathComponent(objectID)) {
        YRLOG_ERROR("{}|{}|Invalid path component in bucketID or objectID (potential traversal attempt)",
                    info.traceid(), info.requestid());
        return {Status(StatusCode::PARAMETER_ERROR, "invalid deployment path"), ""};
    }

    jarPath += "/" + RUNTIME_LAYER_DIR_NAME + "/" + RUNTIME_FUNC_DIR_NAME + "/" + bucketID + "/" + objectID;
    return {Status::OK(), config.runtimePath + YR_JAVA_RUNTIME_PATH + ":" + jarPath};
}
}  // namespace

const std::vector<std::string> &JavaStrategy::SelectJvmArgs(
    const std::string &language, const RuntimeConfig &config) const
{
    if (language == JAVA11_LANGUAGE) {
        return config.jvmArgsForJava11;
    }
    if (language == JAVA17_LANGUAGE) {
        return config.jvmArgsForJava17;
    }
    if (language == JAVA21_LANGUAGE) {
        return config.jvmArgsForJava21;
    }
    // Default: java1.8 and any unrecognized Java version
    return config.jvmArgs;
}

std::pair<Status, CommandArgs> JavaStrategy::BuildArgs(
    const messages::StartInstanceRequest &request, const std::string &port, const RuntimeConfig &config) const
{
    const auto &info = request.runtimeinstanceinfo();
    const std::string &language = info.runtimeconfig().language();

    // Validate language parameter to ensure it's a recognized Java version
    if (language != JAVA_LANGUAGE && language != JAVA11_LANGUAGE && language != JAVA17_LANGUAGE &&
        language != JAVA21_LANGUAGE) {
        YRLOG_WARN("{}|{}|JavaCommandStrategy::BuildArgs: unrecognized Java language({}), using Java 1.8 defaults",
                   info.traceid(), info.requestid(), language);
    }

    YRLOG_DEBUG("{}|{}|JavaCommandStrategy::BuildArgs language({})", info.traceid(), info.requestid(), language);

    // Resolve java executable via PATH lookup
    std::string execPath;
    if (!execLookPath_) {
        execPath = language;
    } else {
        auto path = LookPath(language);
        if (path.IsNone() && language == JAVA_LANGUAGE) {
            path = LookPath(JAVA_CMD);
        }
        if (path.IsNone() && language == JAVA_LANGUAGE && access(DEFAULT_JAVA8_CMD.c_str(), X_OK) == 0) {
            execPath = DEFAULT_JAVA8_CMD;
        } else if (path.IsNone()) {
            YRLOG_ERROR("{}|{}|java LookPath failed for: {}", info.traceid(), info.requestid(), language);
            return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "java exec path not found"), {}};
        } else {
            execPath = path.Get();
        }
    }

    auto [classPathStatus, javaClassPath] = BuildJavaClassPath(request, config);
    if (classPathStatus.IsError()) {
        return {classPathStatus, {}};
    }
    std::string address = GetPosixAddress(config, port);
    std::string jobID = Utils::GetJobIDFromTraceID(info.traceid());

    // Start from base jvmArgs selected by language version
    std::vector<std::string> args = SelectJvmArgs(language, config);

    AppendJavaMemoryArgs(&args, info, config);

    args.emplace_back("-cp");
    args.emplace_back(javaClassPath);
    args.emplace_back(JAVA_LOG_LEVEL + config.runtimeLogLevel);
    AppendJavaLogConfig(&args, info, config);

    args.emplace_back(JAVA_SYSTEM_LIBRARY_PATH + config.javaSystemLibraryPath);
    args.emplace_back("-XX:ErrorFile=" + config.runtimeLogPath + "/exception/BackTrace_" + info.runtimeid() + ".log");
    args.emplace_back(JAVA_JOB_ID + jobID);
    args.emplace_back(JAVA_MAIN_CLASS);
    args.emplace_back(address);
    args.emplace_back(info.runtimeid());

    CommandArgs result;
    result.execPath = execPath;
    result.args = std::move(args);
    return {Status::OK(), std::move(result)};
}

}  // namespace functionsystem::runtime_manager
