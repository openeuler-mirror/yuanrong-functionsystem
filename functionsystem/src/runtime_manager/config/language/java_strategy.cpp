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
const std::string RUNTIME_LAYER_DIR_NAME = "layer";
const std::string RUNTIME_FUNC_DIR_NAME = "func";
const std::string MONOPOLY_SCHEDULE = "monopoly";
}  // namespace

const std::vector<std::string> &JavaCommandStrategy::SelectJvmArgs(const std::string &language,
                                                                     const RuntimeConfig &config) const
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

std::pair<Status, CommandArgs> JavaCommandStrategy::BuildArgs(const messages::StartInstanceRequest &request,
                                                               const std::string &port,
                                                               const RuntimeConfig &config) const
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
        if (path.IsNone()) {
            YRLOG_ERROR("{}|{}|java LookPath failed for: {}", info.traceid(), info.requestid(), language);
            return {Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "java exec path not found"), {}};
        }
        execPath = path.Get();
    }

    // Build class path
    std::string deployDir = info.deploymentconfig().deploydir();
    std::string jarPath = deployDir;
    if (request.scheduleoption().schedpolicyname() != MONOPOLY_SCHEDULE) {
        std::string bucketID = info.deploymentconfig().bucketid();
        std::string objectID = info.deploymentconfig().objectid();
        
        // Validate path components to prevent directory traversal attacks
        // Reject paths containing "..", "//", or absolute paths
        if (bucketID.find("..") != std::string::npos || bucketID.find("//") != std::string::npos ||
            bucketID[0] == '/' || objectID.find("..") != std::string::npos || objectID.find("//") != std::string::npos ||
            objectID[0] == '/') {
            YRLOG_ERROR("{}|{}|Invalid path component in bucketID or objectID (potential traversal attempt)", 
                       info.traceid(), info.requestid());
            return {Status(StatusCode::PARAMETER_ERROR, "invalid deployment path"), {}};
        }
        
        jarPath = deployDir + "/" + RUNTIME_LAYER_DIR_NAME + "/" + RUNTIME_FUNC_DIR_NAME + "/" + bucketID + "/"
                  + objectID;
    }
    std::string javaClassPath = config.runtimePath + YR_JAVA_RUNTIME_PATH + ":" + jarPath;
    std::string address = GetPosixAddress(config, port);
    std::string jobID = Utils::GetJobIDFromTraceID(info.traceid());

    // Start from base jvmArgs selected by language version
    std::vector<std::string> args = SelectJvmArgs(language, config);

    // Append heap size from resource config
    auto resources = info.runtimeconfig().resources().resources();
    for (const auto &resource : resources) {
        if (resource.first == resource_view::MEMORY_RESOURCE_NAME) {
            auto memVal = resource.second.scalar().value();
            memVal = memVal > config.maxJvmMemory ? config.maxJvmMemory : memVal;
            if (memVal > 0) {
                args.emplace_back("-Xmx" + std::to_string(static_cast<int>(memVal)) + "m");
            }
            break;
        }
    }

    args.emplace_back("-cp");
    args.emplace_back(javaClassPath);
    args.emplace_back(JAVA_LOG_LEVEL + config.runtimeLogLevel);

    if (auto it = info.runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD);
        it != info.runtimeconfig().posixenvs().end()) {
        args.emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config.javaSystemProperty + ',' + it->second
                          + "/config/log4j2.xml");
        YRLOG_INFO("{}|{}|append java log4j config from delegate: {}/config/log4j2.xml", info.traceid(),
                   info.requestid(), it->second);
    } else {
        args.emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config.javaSystemProperty);
    }

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
