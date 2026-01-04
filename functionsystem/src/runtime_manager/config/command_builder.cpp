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

#include "command_builder.h"

#include <unordered_set>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/path.h"
#include "utils/os_utils.hpp"
#include "utils/utils.h"

namespace functionsystem::runtime_manager {

const std::vector<std::string> languages = {
    CPP_LANGUAGE,       GO_LANGUAGE,        JAVA_LANGUAGE,        JAVA11_LANGUAGE,
    JAVA17_LANGUAGE,    JAVA21_LANGUAGE,    PYTHON_LANGUAGE,      PYTHON3_LANGUAGE,
    PYTHON36_LANGUAGE,  PYTHON37_LANGUAGE,  PYTHON38_LANGUAGE,    PYTHON39_LANGUAGE,
    PYTHON310_LANGUAGE, PYTHON311_LANGUAGE, POSIX_CUSTOM_RUNTIME, NODE_JS
};

const std::string RUNTIME_DIR = "RUNTIME_DIR";
const std::string CPP_NEW_EXEC_PATH = "/cpp/bin/runtime";
const std::string GO_NEW_EXEC_PATH = "/go/bin/goruntime";
const std::string GLOG_LOG_DIR = "GLOG_log_dir";
const std::string YR_LOG_LEVEL = "YR_LOG_LEVEL";
const std::string PYTHON_PATH = "PYTHONPATH";
const std::string PATH = "PATH";
const std::string PYTHON_LOG_CONFIG_PATH = "PYTHON_LOG_CONFIG";
const std::string BASH_PATH = "/bin/bash";
const std::string MAX_LOG_SIZE_MB_ENV = "YR_MAX_LOG_SIZE_MB";
const std::string MAX_LOG_FILE_NUM_ENV = "YR_MAX_LOG_FILE_NUM";
const std::string CPP_PROGRAM_NAME = "cppruntime";
const std::string GO_PROGRAM_NAME = "goruntime";
const std::string RUNTIME_DS_CONNECT_TIMEOUT_ENV = "DS_CONNECT_TIMEOUT_SEC";

const std::string RUNTIME_ID_ARG_PREFIX = "-runtimeId=";
const std::string INSTANCE_ID_ARG_PREFIX = "-instanceId=";
const std::string LOG_LEVEL_PREFIX = "-logLevel=";
const std::string GRPC_ADDRESS_PREFIX = "-grpcAddress=";
const std::string CONFIG_PATH_PREFIX = "-runtimeConfigPath=";
const std::string JOB_ID_PREFIX = "-jobId=job-";
const std::string PYTHON_JOB_ID_PREFIX = "job-";
const std::string RUNTIME_LAYER_DIR_NAME = "layer";
const std::string RUNTIME_FUNC_DIR_NAME = "func";
const std::string PYTHON_PRESTART_DEPLOY_DIR = "/dcache";
const std::string JAVA_SYSTEM_PROPERTY_FILE = "-Dlog4j2.configurationFile=file:";
const std::string JAVA_SYSTEM_LIBRARY_PATH = "-Djava.library.path=";
const std::string JAVA_LOG_LEVEL = "-DlogLevel=";
const std::string JAVA_JOB_ID = "-DjobId=job-";
const std::string JAVA_MAIN_CLASS = "com.yuanrong.runtime.server.RuntimeServer";
const std::string PYTHON_NEW_SERVER_PATH = "/python/fnruntime/server.py";
const std::string YR_JAVA_RUNTIME_PATH = "/java/yr-runtime-1.0.0.jar";

const std::string INSTANCE_WORK_DIR_ENV = "INSTANCE_WORK_DIR";
const std::string YR_NOSET_ASCEND_RT_VISIBLE_DEVICES = "YR_NOSET_ASCEND_RT_VISIBLE_DEVICES";
const static std::string ASCEND_RT_VISIBLE_DEVICES = "ASCEND_RT_VISIBLE_DEVICES";

const std::string CONDA_PROGRAM_NAME = "conda";
const std::string CONDA_ENV_FILE = "env.yaml";
const std::string WHITE_LIST_ENV_PREFIX = "YR_";

const std::string CHDIR_PATH_CONFIG = "CHDIR_PATH";

// Exclude environment variables passed to the runtime
const std::vector<std::string> EXCLUDE_ENV_KEYS_PASSED_TO_RUNTIME = {
    UNZIPPED_WORKING_DIR
};  // job working_dir file unzipped path

std::string CommandBuilder::GetExecPath(const std::string &language) const
{
    std::string languageArg = GetLanguageArg(language);
    std::string languageCmd = language;
    YRLOG_DEBUG("ready to GetExecPath, language: {}, languageArg: {}", language, languageArg);
    if (languageArg == CPP_LANGUAGE) {
        return config_.runtimePath + CPP_NEW_EXEC_PATH;
    } else if (languageArg == GO_LANGUAGE) {
        return config_.runtimePath + GO_NEW_EXEC_PATH;
    } else if (languageArg == POSIX_CUSTOM_RUNTIME) {
        return BASH_PATH;
    } else if (languageArg == NODE_JS) {
        languageCmd = NODE_JS_CMD;
    } else if (languageArg == JAVA_LANGUAGE) {
        languageCmd = JAVA_LANGUAGE;
    } else if (languageArg == JAVA11_LANGUAGE) {
        languageCmd = JAVA11_LANGUAGE;
    } else if (languageArg == JAVA17_LANGUAGE) {
        languageCmd = JAVA17_LANGUAGE;
    } else if (languageArg == JAVA21_LANGUAGE) {
        languageCmd = JAVA21_LANGUAGE;
    }
    if (!execLookPath_) {
        YRLOG_INFO("GetExecPath, execPath: {}", languageCmd);
        return languageCmd;
    }
    auto path = LookPath(languageCmd);
    if (path.IsNone()) {
        YRLOG_ERROR("GetExecPath failed, path is null");
        return "";
    }
    YRLOG_INFO("GetExecPath, execPath: {}", path.Get());
    return path.Get();
}

std::string CommandBuilder::GetExecPathFromRuntimeConfig(const messages::RuntimeConfig &config) const
{
    const std::string &language = config.language();
    if (language == POSIX_CUSTOM_RUNTIME) {
        // custom-runtime Case1: compatible with job entrypoint, like "python script.py"
        auto workingDirIter = config.posixenvs().find(UNZIPPED_WORKING_DIR);
        if (workingDirIter != config.posixenvs().end() && !workingDirIter->second.empty()) {
            std::string entrypoint = config.entryfile();
            if (entrypoint.empty()) {
                YRLOG_ERROR("empty job entrypoint is invalid");
                return "";
            }
            YRLOG_DEBUG("job entrypoint: {}", entrypoint);
            return entrypoint;
        }

        // custom-runtime Case2: C++ FaaS entrypoint, like "start.sh"
        auto delegateBootstrapIter = config.posixenvs().find(ENV_DELEGATE_BOOTSTRAP);
        auto delegateDownloadIter = config.posixenvs().find(ENV_DELEGATE_DOWNLOAD);
        if (delegateBootstrapIter != config.posixenvs().end() && delegateDownloadIter != config.posixenvs().end()) {
            std::stringstream ss;
            ss << delegateDownloadIter->second << "/" << delegateBootstrapIter->second;
            YRLOG_DEBUG("posix custom runtime entry file : {}", ss.str());
            return ss.str();
        }
        // custom-runtime Cases
        return BASH_PATH;
    }
    return GetExecPath(language);
}

std::string CommandBuilder::GetLanguageArg(const std::string &language) const
{
    std::string res = language;
    for (const auto &lang : languages) {
        if (language.find(lang) != std::string::npos) {
            YRLOG_DEBUG("GetLanguageArg find lang: {}", lang);
            res = lang;
            return res;
        }
    }
    YRLOG_DEBUG("cannot support this language: {}", res);
    return res;
}

std::map<std::string, std::string> CommandBuilder::CombineEnvs(const Envs &envs) const
{
    auto posixEnvs = envs.posixEnvs;
    auto customEnvs = envs.customResourceEnvs;
    auto userEnvs = envs.userEnvs;
    std::map<std::string, std::string> combineEnvs = posixEnvs;
    combineEnvs.insert(customEnvs.begin(), customEnvs.end());
    // userEnvs and override posixEnvs and customEnvs
    for (auto &pair : std::as_const(userEnvs)) {
        auto iter = combineEnvs.find(pair.first);
        if (iter == combineEnvs.end()) {
            combineEnvs.insert(pair);
            continue;
        }

        if (pair.first == LD_LIBRARY_PATH) {
            combineEnvs[pair.first] = iter->second + ":" + pair.second;
            continue;
        }
        combineEnvs[pair.first] = pair.second;
    }
    // framework envs needed by runtime override userEnvs
    combineEnvs[YR_LOG_LEVEL] = config_.runtimeLogLevel;
    combineEnvs[GLOG_LOG_DIR] = config_.runtimeLogPath;
    combineEnvs[PYTHON_LOG_CONFIG_PATH] = config_.pythonLogConfigPath;
    combineEnvs[MAX_LOG_SIZE_MB_ENV] = std::to_string(config_.runtimeMaxLogSize);
    combineEnvs[MAX_LOG_FILE_NUM_ENV] = std::to_string(config_.runtimeMaxLogFileNum);
    std::string pythonPath = config_.runtimePath;
    if (!config_.pythonDependencyPath.empty()) {
        (void)pythonPath.append(":" + config_.pythonDependencyPath);
    }

    // python job working dir after unzip
    auto workingDirIter = combineEnvs.find(UNZIPPED_WORKING_DIR);
    if (workingDirIter != combineEnvs.end() && !workingDirIter->second.empty()) {
        (void)pythonPath.append(":" + workingDirIter->second);
    }
    if (combineEnvs.find(PYTHON_PATH) != combineEnvs.end()) {
        pythonPath.append(":" + combineEnvs[PYTHON_PATH]);
    }
    combineEnvs[PYTHON_PATH] = pythonPath;

    // exclude envs to runtime process
    for (const std::string &str : EXCLUDE_ENV_KEYS_PASSED_TO_RUNTIME) {
        combineEnvs.erase(str);
    }

    // add runtime ds-client connection timeout env
    combineEnvs[RUNTIME_DS_CONNECT_TIMEOUT_ENV] = std::to_string(config_.runtimeDsConnectTimeout);

    InheritEnv(combineEnvs);
    return combineEnvs;
}

void CommandBuilder::InheritEnv(std::map<std::string, std::string> &combineEnvs) const
{
    char **env = environ;
    for (; *env; ++env) {
        std::string envStr = *env;
        auto equalPos = envStr.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }
        auto key = envStr.substr(0, equalPos);
        auto val = envStr.substr(equalPos + 1);
        if (litebus::strings::StartsWithPrefix(key, WHITE_LIST_ENV_PREFIX)) {
            if (combineEnvs.find(key) == combineEnvs.end()) {
                combineEnvs[key] = val;
            }
            continue;
        }
        if (config_.inheritEnv) {
            if (key == PATH) {
                combineEnvs[key] = (combineEnvs[key].empty() ? "" : combineEnvs[key] + ":") + val;
                continue;
            }
            if (combineEnvs.find(key) != combineEnvs.end()) {
                continue;
            }
            combineEnvs[key] = val;
        }
    }

    // if set YR_NOSET_ASCEND_RT_VISIBLE_DEVICES , ASCEND_RT_VISIBLE_DEVICES will not set
    if (combineEnvs.find(YR_NOSET_ASCEND_RT_VISIBLE_DEVICES) != combineEnvs.end()) {
        (void)combineEnvs.erase(ASCEND_RT_VISIBLE_DEVICES);
    }
}

Status CommandBuilder::GetBuildArgs(const std::string &language, const std::string &port,
                                     const std::shared_ptr<messages::StartInstanceRequest> &request,
                                     std::vector<std::string> &args)
{
    auto info = request->runtimeinstanceinfo();
    if (chdir(config_.runtimePath.c_str()) != 0) {
        YRLOG_WARN("{}|{}|enter runtimePath failed, path: {}", info.traceid(), info.requestid(), config_.runtimePath);
    }
    std::string langArg = GetLanguageArg(language);
    if (buildArgsFunc_.find(langArg) == buildArgsFunc_.end()) {
        YRLOG_ERROR("{}|{}|CommandBuilder does not support this language: {}", info.traceid(), info.requestid(),
                    langArg);
        return Status(StatusCode::PARAMETER_ERROR, "runtimeExecutor does not support this language: " + langArg);
    }

    YRLOG_DEBUG("{}|{}|find buildArgsFunc for lang: {}", info.traceid(), info.requestid(), language);
    auto langBuild = buildArgsFunc_[langArg];
    auto getBuildArgs = std::bind(langBuild, this, std::placeholders::_1, std::placeholders::_2);
    auto [status, args_local] = getBuildArgs(port, request);
    args = std::move(args_local);
    return status;
}


std::pair<Status, std::vector<std::string>> CommandBuilder::GetCppBuildArgs(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    YRLOG_DEBUG("{}|{}|GetCppBuildArgs start", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid());
    std::string address = config_.ip + ":" + port;
    auto confPath = litebus::os::Join(config_.runtimeConfigPath, "runtime.json");

    auto resultPair = HandleWorkingDirectory(request, request->runtimeinstanceinfo());
    if (resultPair.first.IsError()) {
        return { resultPair.first, {} };
    }

    return { Status::OK(),
             { CPP_PROGRAM_NAME, RUNTIME_ID_ARG_PREFIX + request->runtimeinstanceinfo().runtimeid(),
               LOG_LEVEL_PREFIX + config_.runtimeLogLevel,
               JOB_ID_PREFIX + Utils::GetJobIDFromTraceID(request->runtimeinstanceinfo().traceid()),
               GRPC_ADDRESS_PREFIX + address, CONFIG_PATH_PREFIX + confPath } };
}


std::pair<Status, std::vector<std::string>> CommandBuilder::GetGoBuildArgs(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    YRLOG_DEBUG("{}|{}|GetGoBuildArgs start, instance({}), runtime({})", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid(), request->runtimeinstanceinfo().instanceid(),
                request->runtimeinstanceinfo().runtimeid());
    std::string address = config_.ip + ":" + port;
    return { Status::OK(),
             { GO_PROGRAM_NAME, RUNTIME_ID_ARG_PREFIX + request->runtimeinstanceinfo().runtimeid(),
               INSTANCE_ID_ARG_PREFIX + request->runtimeinstanceinfo().instanceid(),
               LOG_LEVEL_PREFIX + config_.runtimeLogLevel, GRPC_ADDRESS_PREFIX + address } };
}

inline bool IsEnableConda(const google::protobuf::Map<std::string, std::string> &deployOptions)
{
    return deployOptions.count(CONDA_PREFIX) && deployOptions.count(CONDA_DEFAULT_ENV);
}

std::pair<Status, std::string> CommandBuilder::GetPythonExecPath(
    const google::protobuf::Map<std::string, std::string> &deployOptions,
    const messages::RuntimeInstanceInfo &info) const
{
    if (!IsEnableConda(deployOptions)) {
        return { Status::OK(), GetExecPath(info.runtimeconfig().language()) };
    }

    const auto &condaPrefix = deployOptions.at(CONDA_PREFIX);
    const auto &condaEnv = deployOptions.at(CONDA_DEFAULT_ENV);
    const std::string execPath = litebus::os::Join(
        litebus::os::Join(litebus::os::Join(litebus::os::Join(condaPrefix, "envs"), condaEnv), "bin"), "python");

    YRLOG_INFO("{}|{}|conda python env's execPath: {}", info.traceid(), info.requestid(), execPath);
    return { Status::OK(), execPath };
}

bool endsWith(const std::string &str, const std::string &suffix)
{
    if (suffix.size() > str.size())
        return false;
    return str.substr(str.size() - suffix.size()) == suffix;
}

std::pair<Status, std::string> CommandBuilder::HandleWorkingDirectory(
    const std::shared_ptr<messages::StartInstanceRequest> &request, const messages::RuntimeInstanceInfo &info) const
{
    const auto &posixEnvs = info.runtimeconfig().posixenvs();
    auto workingDirIter = posixEnvs.find(UNZIPPED_WORKING_DIR);
    auto fileIter = posixEnvs.find(YR_WORKING_DIR);
    if (workingDirIter == posixEnvs.end() || fileIter == posixEnvs.end()) {
        return { Status::OK(), info.deploymentconfig().deploydir() };
    }
    if (workingDirIter->second.empty() || fileIter->second.empty()) {
        YRLOG_ERROR("{}|{}|params working dir({}) or unzipped dir({}) is empty", info.traceid(), info.requestid(),
                    fileIter->second, workingDirIter->second);
        return { Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND,
                        "params working dir or unzipped dir is empty"),
                 "" };
    }

    if (endsWith(workingDirIter->second, ".img")) {
        (*request->mutable_runtimeinstanceinfo()
              ->mutable_deploymentconfig()
              ->mutable_deployoptions())[CHDIR_PATH_CONFIG] = request->runtimeinstanceinfo().container().mountpoint();
        YRLOG_DEBUG("change working dir to container mount point: {}",
                    request->runtimeinstanceinfo().container().mountpoint());
        return { Status::OK(), request->runtimeinstanceinfo().container().mountpoint() };
    }

    char canonicalPath[PATH_MAX];
    if (realpath(workingDirIter->second.c_str(), canonicalPath) == nullptr) {
        return { Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "cannot resolve path"), "" };
    }

    if (access(canonicalPath, R_OK | W_OK | X_OK) != 0) {
        return { Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "insufficient directory permissions"),
                 "" };
    }

    (*request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions())[CHDIR_PATH_CONFIG] =
        workingDirIter->second;
    YRLOG_DEBUG("change working dir to {}", workingDirIter->second);
    return { Status::OK(), workingDirIter->second };
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetPythonBuildArgs(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    const auto &info = request->runtimeinstanceinfo();
    const auto &deployOptions = info.deploymentconfig().deployoptions();

    auto [execPathStatus, execPath] = GetPythonExecPath(deployOptions, info);
    if (execPathStatus.IsError()) {
        return { execPathStatus, {} };
    }

    auto [workDirStatus, deployDir] = HandleWorkingDirectory(request, info);
    if (workDirStatus.IsError()) {
        return { workDirStatus, {} };
    }

    if (deployDir.empty()) {
        YRLOG_ERROR("{}|{}|python deploy dir is empty, cannot set build args", info.traceid(), info.requestid());
        return { Status(StatusCode::RUNTIME_MANAGER_DEPLOY_DIR_IS_EMPTY, "deploy dir is empty"), {} };
    }

    YRLOG_DEBUG("{}|{}|python deploy dir: {}", info.traceid(), info.requestid(), deployDir);
    if (!litebus::os::ExistPath(deployDir)) {
        if (!litebus::os::Mkdir(deployDir).IsNone()) {
            YRLOG_WARN("{}|{}|failed to make dir deployDir({}), msg: {}", request->runtimeinstanceinfo().traceid(),
                       request->runtimeinstanceinfo().requestid(), deployDir, litebus::os::Strerror(errno));
            return { Status(StatusCode::RUNTIME_MANAGER_CONDA_PARAMS_INVALID, "failed to make dir deployDir"), {} };
        }
    }
    // conda create is removed which should build in executor
    return PythonBuildFinalArgs(port, execPath, deployDir, info);
}

std::pair<Status, std::vector<std::string>> CommandBuilder::PythonBuildFinalArgs(
    const std::string &port, const std::string &execPath, const std::string &deployDir,
    const messages::RuntimeInstanceInfo &info) const
{
    std::string jobID = PYTHON_JOB_ID_PREFIX + Utils::GetJobIDFromTraceID(info.traceid());
    std::string address = config_.ip + ":" + port;

    return { Status::OK(),
             { execPath, "-u", config_.runtimePath + PYTHON_NEW_SERVER_PATH, "--rt_server_address", address,
               "--deploy_dir", deployDir, "--runtime_id", info.runtimeid(), "--job_id", jobID, "--log_level",
               config_.runtimeLogLevel } };
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetJavaBuildArgs(
    const std::string &port, const std::vector<std::string> &jvmArgs,
    const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    YRLOG_DEBUG("{}|{}|GetJavaBuildArgs start", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid());
    std::string deployDir = request->runtimeinstanceinfo().deploymentconfig().deploydir();
    std::string jarPath = deployDir;
    if (request->scheduleoption().schedpolicyname() != MONOPOLY_SCHEDULE) {
        std::string bucketID = request->runtimeinstanceinfo().deploymentconfig().bucketid();
        std::string objectID = request->runtimeinstanceinfo().deploymentconfig().objectid();
        jarPath =
            deployDir + "/" + RUNTIME_LAYER_DIR_NAME + "/" + RUNTIME_FUNC_DIR_NAME + "/" + bucketID + "/" + objectID;
    }
    std::string javaClassPath = config_.runtimePath + YR_JAVA_RUNTIME_PATH + ":" + jarPath;
    std::string address = config_.ip + ":" + port;
    std::vector<std::string> args = jvmArgs;
    auto resources = request->runtimeinstanceinfo().runtimeconfig().resources().resources();
    for (auto resource : resources) {
        if (resource.first == resource_view::MEMORY_RESOURCE_NAME) {
            auto memVal = resource.second.mutable_scalar()->value();
            memVal = memVal > config_.maxJvmMemory ? config_.maxJvmMemory : memVal;
            if (memVal > 0) {
                // use memory value(defined in metadata or scheduling options) to set java heap memory: Xmx
                std::string memStr = std::to_string(int(memVal));
                (void)args.emplace_back("-Xmx" + memStr + "m");
            }
            break;
        }
    }
    std::string jobID = Utils::GetJobIDFromTraceID(request->runtimeinstanceinfo().traceid());
    (void)args.emplace_back("-cp");
    (void)args.emplace_back(javaClassPath);
    (void)args.emplace_back(JAVA_LOG_LEVEL + config_.runtimeLogLevel);
    if (auto path = request->runtimeinstanceinfo().runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD);
        path != request->runtimeinstanceinfo().runtimeconfig().posixenvs().end()) {
        (void)args.emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config_.javaSystemProperty + ',' + path->second
                                + "/config/log4j2.xml");
        YRLOG_INFO("append java system property file: {}/config/log4j2.xml", path->second);
    } else {
        (void)args.emplace_back(JAVA_SYSTEM_PROPERTY_FILE + config_.javaSystemProperty);
    }
    (void)args.emplace_back(JAVA_SYSTEM_LIBRARY_PATH + config_.javaSystemLibraryPath);
    (void)args.emplace_back("-XX:ErrorFile=" + config_.runtimeLogPath + "/exception/BackTrace_"
                            + request->runtimeinstanceinfo().runtimeid() + ".log");
    (void)args.emplace_back(JAVA_JOB_ID + jobID);
    (void)args.emplace_back(JAVA_MAIN_CLASS);
    (void)args.emplace_back(address);
    (void)args.emplace_back(request->runtimeinstanceinfo().runtimeid());
    return { Status::OK(), args };
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetJavaBuildArgsDefault(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    return GetJavaBuildArgs(port, config_.jvmArgs, request);
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetJavaBuildArgsForJava11(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    return GetJavaBuildArgs(port, config_.jvmArgsForJava11, request);
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetJavaBuildArgsForJava17(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    return GetJavaBuildArgs(port, config_.jvmArgsForJava17, request);
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetJavaBuildArgsForJava21(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    return GetJavaBuildArgs(port, config_.jvmArgsForJava21, request);
}


std::pair<Status, std::vector<std::string>> CommandBuilder::GetNodejsBuildArgs(
    const std::string &port, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    std::string memorySize = "";
    std::string address = config_.ip + ":" + port;
    auto resources = request->runtimeinstanceinfo().runtimeconfig().resources().resources();
    for (auto resource : resources) {
        if (resource.first == resource_view::MEMORY_RESOURCE_NAME && resource.second.mutable_scalar()->value() > 0) {
            if (resource.second.mutable_scalar()->value() >= std::numeric_limits<int>::max()) {
                YRLOG_DEBUG("{} scalar exceeds max int value", resource_view::MEMORY_RESOURCE_NAME);
                continue;
            }
            memorySize = "--max-old-space-size=" + std::to_string(int(resource.second.mutable_scalar()->value()));
            break;
        }
    }

    if (memorySize != "") {
        return { Status::OK(),
                 { memorySize, "/home/snuser/runtime/nodejs/wrapper.js", "--rt_server_address=" + address,
                   "--runtime_id=" + request->runtimeinstanceinfo().runtimeid(),
                   "--job_id=" + Utils::GetJobIDFromTraceID(request->runtimeinstanceinfo().traceid()),
                   "--log_level=" + config_.runtimeLogLevel } };
    }
    return { Status::OK(),
             { "/home/snuser/runtime/nodejs/wrapper.js", "--rt_server_address=" + address,
               "--runtime_id=" + request->runtimeinstanceinfo().runtimeid(),
               "--job_id=" + Utils::GetJobIDFromTraceID(request->runtimeinstanceinfo().traceid()),
               "--log_level=" + config_.runtimeLogLevel } };
}

std::pair<Status, std::vector<std::string>> CommandBuilder::GetPosixCustomBuildArgs(
    const std::string &, const std::shared_ptr<messages::StartInstanceRequest> &request) const
{
    YRLOG_DEBUG("{}|{}|GetPosixCustomBuildArgs start", request->runtimeinstanceinfo().traceid(),
                request->runtimeinstanceinfo().requestid());

    // entry script case
    if (request->runtimeinstanceinfo().runtimeconfig().posixenvs().find(ENV_DELEGATE_BOOTSTRAP)
            != request->runtimeinstanceinfo().runtimeconfig().posixenvs().end()
        && request->runtimeinstanceinfo().runtimeconfig().posixenvs().find(ENV_DELEGATE_DOWNLOAD)
               != request->runtimeinstanceinfo().runtimeconfig().posixenvs().end()) {
        YRLOG_DEBUG("posix custom runtime will use user define entry file");
        return { Status::OK(), {} };
    }

    // job working dir case
    auto iter = request->runtimeinstanceinfo().runtimeconfig().posixenvs().find(UNZIPPED_WORKING_DIR);
    auto fileIter = request->runtimeinstanceinfo().runtimeconfig().posixenvs().find(YR_WORKING_DIR);
    if (auto endIter = request->runtimeinstanceinfo().runtimeconfig().posixenvs().end();
        iter != endIter && fileIter != endIter) {
        YRLOG_DEBUG("posix custom runtime will use user defined job entrypoint");
        if (iter->second.empty() || fileIter->second.empty()) {
            YRLOG_ERROR("{}|{}|params working dir({}) or unzipped dir({}) is empty",
                        request->runtimeinstanceinfo().traceid(), request->runtimeinstanceinfo().requestid(),
                        fileIter->second, iter->second);
            return { Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND,
                            "params working dir or unzipped dir is empty"),
                     {} };
        }

        if (!litebus::os::ExistPath(iter->second)) {
            YRLOG_ERROR("{}|{}|enter working dir failed, path: {}", request->runtimeinstanceinfo().traceid(),
                        request->runtimeinstanceinfo().requestid(), iter->second);
            return { Status(StatusCode::RUNTIME_MANAGER_WORKING_DIR_FOR_APP_NOTFOUND, "job working dir is invalid"),
                     {} };
        }

        (*request->mutable_runtimeinstanceinfo()
              ->mutable_deploymentconfig()
              ->mutable_deployoptions())[CHDIR_PATH_CONFIG] = iter->second;
        YRLOG_DEBUG("change job entrypoint execute dir to {}", iter->second);
        return { Status::OK(), {} };
    }

    // entry path + '/bootstrap' case
    std::string entryFile = request->runtimeinstanceinfo().runtimeconfig().entryfile();
    if (entryFile.empty()) {
        YRLOG_ERROR("{}|{}|entryFile is empty", request->runtimeinstanceinfo().traceid(),
                    request->runtimeinstanceinfo().requestid());
        return { Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "entryFile is empty"), {} };
    }

    if (!litebus::os::ExistPath(entryFile)) {
        YRLOG_ERROR("{}|{}|enter entryfile path failed, path: {}", request->runtimeinstanceinfo().traceid(),
                    request->runtimeinstanceinfo().requestid(), entryFile);
        return { Status(StatusCode::RUNTIME_MANAGER_EXECUTABLE_PATH_INVALID, "chdir entryfile path failed"), {} };
    }

    (*request->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->mutable_deployoptions())[CHDIR_PATH_CONFIG] =
        entryFile;
    YRLOG_DEBUG("entrypoint: {}", entryFile + "/bootstrap");
    return { Status::OK(), { entryFile + "/bootstrap" } };
}
}