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

#ifndef RUNTIME_MANAGER_EXECUTOR_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_EXECUTOR_H

#include <nlohmann/json.hpp>
#include <regex>

#include "actor/actor.hpp"
#include "async/async.hpp"
#include "async/future.hpp"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "exec/exec.hpp"
#include "runtime_manager/config/flags.h"
#include "common/utils/test_util.h"

namespace functionsystem::runtime_manager {

const std::string NODE_JS = "nodejs";
const std::string NODE_JS_CMD = "nodejs18.14";
const std::string JAVA_LANGUAGE_PREFIX = "java";
const std::string JAVA_LANGUAGE = "java1.8";
const std::string JAVA11_LANGUAGE = "java11";
const std::string JAVA17_LANGUAGE = "java17";
const std::string JAVA21_LANGUAGE = "java21";
const std::string CPP_LANGUAGE = "cpp";
const std::string GO_LANGUAGE = "go";
const std::string PYTHON_LANGUAGE = "python";
const std::string PYTHON3_LANGUAGE = "python3";
const std::string PYTHON36_LANGUAGE = "python3.6";
const std::string PYTHON37_LANGUAGE = "python3.7";
const std::string PYTHON38_LANGUAGE = "python3.8";
const std::string PYTHON39_LANGUAGE = "python3.9";
const std::string PYTHON310_LANGUAGE = "python3.10";
const std::string PYTHON311_LANGUAGE = "python3.11";
const std::string POSIX_CUSTOM_RUNTIME = "posix-custom-runtime";

struct RuntimeConfig {
    std::string ip;
    std::string hostIP;
    std::string proxyIP;
    std::string nodeID = "nodeID";
    std::string runtimePath;
    std::string runtimeLogPath;
    std::string runtimeStdLogDir;
    std::string runtimeLogLevel;
    int runtimeMaxLogSize;
    int runtimeMaxLogFileNum;
    std::string runtimeLdLibraryPath;
    std::string pythonDependencyPath;
    std::string pythonLogConfigPath;
    std::string javaSystemProperty;
    std::string javaSystemLibraryPath;
    std::string dataSystemPort;
    std::string snuserLibDir;
    std::string driverServerPort;
    std::string runtimeConfigPath;
    bool setCmdCred;
    std::vector<std::string> jvmArgs;
    std::vector<std::string> jvmArgsForJava11;
    std::vector<std::string> jvmArgsForJava17;
    std::vector<std::string> jvmArgsForJava21;
    std::map<std::string, int> runtimePrestartConfigs;
    std::string proxyGrpcServerPort;
    std::string clusterID;
    int runtimeUID;
    int runtimeGID;
    int maxJvmMemory;
    bool isProtoMsgToRuntime;
    bool massifEnable;
    bool inheritEnv;
    bool separatedRedirectRuntimeStd;
    bool runtimeDirectConnectionEnable;
    std::string runtimeHomeDir;
    std::string nodeJsEntryPath;
    uint32_t runtimeDsConnectTimeout;
    uint32_t killProcessTimeoutSeconds{ 0 };
    std::string userLogExportMode;
    bool cleanStreamProducerEnable;
    int virtualEnvIdleTimeLimit;
};

struct PrestartProcess {
    std::string port;
    std::string runtimeID;
    std::shared_ptr<litebus::Exec> execPtr;
};

class Executor : public litebus::ActorBase {
public:
    explicit Executor(const std::string &name);

    ~Executor() override = default;

    /**
     * Start Instance when receive message from function agent.
     *
     * @param request Include start instance arguments.
     * @return response Include start instance result arguments.
     */
    virtual litebus::Future<::messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<int> &cardIDs) = 0;

    /**
     * Stop Instance when receive message from function agent.
     *
     * @param request Include stop instance arguments.
     * @return response Include stop instance result arguments.
     */
    virtual Status StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                bool oomKilled = false) = 0;

    /**
     * Get runtime instance infos.
     *
     * @return Runtime infos.
     */
    virtual std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos();

    void InitConfig();

    /**
     * Set runtime config from flags.
     *
     * @param flags Parsed from main function.
     */
    void SetRuntimeConfig(const Flags &flags);

    /**
     * Get Exec from executor.
     *
     * @param runtimeID Runtime id.
     * @return Exec pointer.
     */
    virtual std::shared_ptr<litebus::Exec> GetExecByRuntimeID(const std::string &runtimeID);

    /**
     * Check if the runtime is active.
     *
     * @param runtimeID Runtime id.
     * @return True if the runtime is active, false otherwise.
     */
    virtual bool IsRuntimeActive(const std::string &runtimeID);

    /**
     * Check if the ds client is active.
     *
     * @param pid ds client pid.
     * @return True if the ds client is active, false otherwise.
     */
    virtual bool IsRuntimeActiveByPid(const pid_t &pid);

    virtual void UpdatePrestartRuntimePromise(pid_t pid){};

    virtual void ClearCapability() = 0;

    virtual litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request) = 0;

    virtual litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                        const int limit) = 0;

protected:
    void Init() override;

    void Finalize() override;

    RuntimeConfig config_{};

    std::map<std::string, std::shared_ptr<litebus::Exec>> runtime2Exec_;

    std::set<pid_t> pids_;

    std::map<std::string, std::deque<PrestartProcess>> prestartRuntimePool_;

    std::map<pid_t, std::shared_ptr<litebus::Promise<bool>>> prestartRuntimePromiseMap_;

    virtual void InitPrestartRuntimePool() = 0;

    virtual void InitVirtualEnvIdleTimeLimit() = 0;

private:
    void InitDefaultArgs(const std::string &configJsonString);

    void ParseJvmArgs(const std::string &language, const nlohmann::json &confJson, std::vector<std::string> &jvmArgs);

    void InitPrestartConfig(const std::string &configJsonString);

    int GetPrestartCountFromConfig(const nlohmann::json &configJson) const;

    std::vector<std::string> VerifyCustomJvmArgs(const std::vector<std::string> &customArgs);

    FRIEND_TEST(RuntimeExecutorTest, VerifyCustomJvmArgs_ShouldReturnValidArgs_WhenArgsAreValid);
};

class ExecutorProxy {
public:
    explicit ExecutorProxy(const std::shared_ptr<Executor> &executor) : executor_(executor){};

    virtual ~ExecutorProxy()
    {
    }

    /**
     * Start Instance when receive message from function agent.
     *
     * @param request Include start instance arguments.
     * @return response Include start instance result arguments.
     */
    virtual litebus::Future<::messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<int> &cardIDs) = 0;

    /**
     * Stop Instance when receive message from function agent.
     *
     * @param request Include stop instance arguments.
     * @param oomKilled is inner oom killed by runtime-manager
     * @return response Include stop instance result arguments.
     */
    virtual litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                                 bool oomKilled = false) = 0;

    /**
     * Get runtime instance infos.
     *
     * @return Runtime infos.
     */
    virtual litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() = 0;

    virtual void UpdatePrestartRuntimePromise(pid_t pid) = 0;

    virtual void ClearCapability() = 0;

    /**
     * Start executor
     *
     * @param flags Parsed from main function.
     */
    virtual void SetRuntimeConfig(const Flags &flags)
    {
        return litebus::Async(executor_->GetAID(), &Executor::SetRuntimeConfig, flags);
    }

    /**
     * Get Exec from executor.
     *
     * @param runtimeID Runtime id.
     * @return Exec pointer.
     */
    virtual litebus::Future<std::shared_ptr<litebus::Exec>> GetExecByRuntimeID(const std::string &runtimeID)
    {
        return litebus::Async(executor_->GetAID(), &Executor::GetExecByRuntimeID, runtimeID);
    }

    /**
     * Update token for runtime when receive message from function agent.
     *
     * @param runtimeID Runtime id.
     * @return Exec pointer.
     */
    virtual litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request)
    {
        return litebus::Async(executor_->GetAID(), &Executor::UpdateCredForRuntime, request);
    }

    virtual litebus::Future<bool> GracefulShutdown() = 0;
    /**
     * Stop executor
     */
    virtual void Stop()
    {
        litebus::Terminate(executor_->GetAID());
        litebus::Await(executor_->GetAID());
    }

    virtual litebus::Future<bool> IsRuntimeActive(const std::string &runtimeID)
    {
        return litebus::Async(executor_->GetAID(), &Executor::IsRuntimeActive, runtimeID);
    }

    virtual litebus::Future<bool> IsRuntimeActiveByPid(const pid_t &pid)
    {
        return litebus::Async(executor_->GetAID(), &Executor::IsRuntimeActiveByPid, pid);
    }

    const std::string GetName() const
    {
        return executor_->GetAID().Name();
    }

    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description, const int limit)
    {
        return litebus::Async(executor_->GetAID(), &Executor::NotifyInstancesDiskUsageExceedLimit, description, limit);
    }

protected:
    std::shared_ptr<Executor> executor_;
};
}  // namespace functionsystem::runtime_manager
#endif  // RUNTIME_MANAGER_EXECUTOR_EXECUTOR_H
