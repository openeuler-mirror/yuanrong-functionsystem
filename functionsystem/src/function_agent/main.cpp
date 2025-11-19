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

#include <csignal>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "common/utils/exec_utils.h"
#include "common/utils/module_switcher.h"
#include "common/utils/ssl_config.h"
#include "common/utils/version.h"
#include "common/aksk/aksk_util.h"
#include "function_agent/driver/function_agent_driver.h"
#include "function_agent/flags/function_agent_flags.h"
#include "runtime_manager/config/flags.h"
#include "runtime_manager/driver/runtime_manager_driver.h"
#include "async/future.hpp"
#include "async/option.hpp"
#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/param_check.h"
#include "common/utils/s3_config.h"
#include "common/utils/sensitive_value.h"

using namespace functionsystem;

namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_FUNCTION_AGENT;  // NOLINT
std::shared_ptr<functionsystem::ModuleSwitcher> g_functionAgentSwitcher{ nullptr };
std::shared_ptr<function_agent::FunctionAgentDriver> g_functionAgentDriver{ nullptr };
std::shared_ptr<runtime_manager::RuntimeManagerDriver> g_runtimeManagerDriver{ nullptr };

std::shared_ptr<litebus::Promise<bool>> g_stopSignal{ nullptr };
std::atomic_bool g_isCentOs = { false };

functionsystem::messages::CodePackageThresholds GetCodePackageThresholds(
    const function_agent::FunctionAgentFlags &flags)
{
    functionsystem::messages::CodePackageThresholds codePackageThresholds;
    codePackageThresholds.set_filecountsmax(flags.GetFileCountMax());
    codePackageThresholds.set_zipfilesizemaxmb(flags.GetZipFileSizeMaxMB());
    codePackageThresholds.set_unzipfilesizemaxmb(flags.GetUnzipFileSizeMaxMB());
    codePackageThresholds.set_dirdepthmax(flags.GetDirDepthMax());
    codePackageThresholds.set_codeagingtime(flags.GetCodeAgingTime());
    return codePackageThresholds;
}

S3Config GetS3Config(const function_agent::FunctionAgentFlags &flags)
{
    S3Config s3Config;
    s3Config.credentialType = flags.GetCredentialType();
    if (s3Config.credentialType == CREDENTIAL_TYPE_ROTATING_CREDENTIALS) {
        YRLOG_INFO("S3 auth type({}) enabled", s3Config.credentialType);
    }
    if (!flags.GetAccessKey().empty()) {
    	s3Config.accessKey = flags.GetAccessKey();
    }
    if (!flags.GetSecretKey().empty()) {
    	s3Config.secretKey = flags.GetSecretKey();
    }
    s3Config.endpoint = flags.GetS3Endpoint();
    s3Config.protocol = flags.GetS3Protocol();

    return s3Config;
}

functionsystem::function_agent::FunctionAgentStartParam BuildStartParam(const function_agent::FunctionAgentFlags &flags)
{
    function_agent::FunctionAgentStartParam startParam{
        .ip = flags.GetIP(),
        .localSchedulerAddress = flags.GetLocalSchedulerAddress(),
        .nodeID = flags.GetNodeID(),
        .alias = flags.GetAlias(),
        .modelName = COMPONENT_NAME,
        .agentPort = flags.GetAgentListenPort(),
        .decryptAlgorithm = flags.GetDecryptAlgorithm(),
        .s3Enable = flags.GetS3Enable(),
        .s3Config = GetS3Config(flags),
        .codePackageThresholds = GetCodePackageThresholds(flags),
        .heartbeatTimeoutMs = flags.GetSystemTimeout(),
        .agentUid = flags.GetAgentUID(),
        .localNodeID = flags.GetLocalNodeID(),
        .enableSignatureValidation = flags.GetEnableSignatureValidation(),
        .componentName = COMPONENT_NAME,
    };
    return startParam;
}

void OnStopHandler(int signum)
{
    YRLOG_INFO("function_agent receives signal: {}", signum);
    if (g_isCentOs) {
        // Temporary workaround: core dump occurs when the system exits. Deleted after the logs function is merged.
        std::cout << "the operating system is CentOS and raise signal kill" << std::endl;
        raise(SIGKILL);
    }
    g_stopSignal->SetValue(true);
}

void OnCreateFunctionAgent(const function_agent::FunctionAgentFlags &flags)
{
    YRLOG_INFO("{} is starting...", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);
    g_functionAgentDriver =
        std::make_shared<function_agent::FunctionAgentDriver>(flags.GetNodeID(), BuildStartParam(flags));
    if (auto status = g_functionAgentDriver->Start(); status.IsError()) {
        YRLOG_ERROR("failed to start function_agent, errMsg: {}", status.ToString());
        g_functionAgentSwitcher->SetStop();
        return;
    }
}

void OnCreateRuntimeManager(const runtime_manager::Flags &runtimeManagerFlags)
{
    // function agent and runtime manager deploy in the same process
    g_runtimeManagerDriver =
        std::make_shared<runtime_manager::RuntimeManagerDriver>(runtimeManagerFlags, "runtime_manager");
    if (auto status = g_runtimeManagerDriver->Start(); status.IsError()) {
        YRLOG_ERROR("failed to start runtime_manager, errMsg: {}", status.ToString());
        g_functionAgentSwitcher->SetStop();
        return;
    }
}

void StopFunctionAgent()
{
    if (g_functionAgentDriver == nullptr) {
        YRLOG_WARN("function agent is not started");
        return;
    }
    g_functionAgentDriver->GracefulShutdown();
    if (g_functionAgentDriver->Stop().IsOk()) {
        g_functionAgentDriver->Await();
        g_functionAgentDriver = nullptr;
        YRLOG_INFO("success to stop {}", COMPONENT_NAME);
    } else {
        YRLOG_WARN("failed to stop {}", COMPONENT_NAME);
    }
    return;
}

void StopRuntimeManager()
{
    if (g_runtimeManagerDriver == nullptr) {
        YRLOG_WARN("runtime manager is not started");
        return;
    }
    if (g_runtimeManagerDriver->Stop().IsOk()) {
        g_runtimeManagerDriver->Await();
        g_runtimeManagerDriver = nullptr;
        YRLOG_INFO("success to stop runtime_manager");
    } else {
        YRLOG_WARN("failed to stop runtime_manager");
    }
    return;
}

void OnDestroy()
{
    StopRuntimeManager();
    if (g_runtimeManagerDriver != nullptr) {
        if (g_runtimeManagerDriver->Stop().IsOk()) {
            g_runtimeManagerDriver->Await();
            g_runtimeManagerDriver = nullptr;
            YRLOG_INFO("success to stop runtime_manager");
        } else {
            YRLOG_WARN("failed to stop runtime_manager");
        }
    }
    StopFunctionAgent();
    g_functionAgentSwitcher->CleanMetrics();
    g_functionAgentSwitcher->FinalizeLiteBus();
    g_functionAgentSwitcher->StopLogger();
    YRLOG_INFO("success to Stop function_agent.");
}

bool InitSSL(const function_agent::FunctionAgentFlags &flags)
{
    auto sslCertConfig = GetSSLCertConfig(flags);
    if (flags.GetSslEnable()) {
        if (!InitLitebusSSLEnv(sslCertConfig).IsOk()) {
            g_functionAgentSwitcher->SetStop();
            return false;
        }
    }

    std::string agentID = FUNCTION_AGENT_ID_PREFIX + flags.GetIP() + "-" + flags.GetAgentListenPort();
    if (!flags.GetAgentUID().empty()) {
        agentID = flags.GetAgentUID();
    }
    functionsystem::metrics::MetricsAdapter::GetInstance().SetContextAttr("agent_id", agentID);
    g_functionAgentSwitcher->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                         flags.GetMetricsConfigFile(), sslCertConfig);
    return true;
}

bool CheckFlags(const function_agent::FunctionAgentFlags &flags)
{
    if (!IsNodeIDValid(flags.GetNodeID())) {
        std::cerr << COMPONENT_NAME << " node id: " << flags.GetNodeID() << " is invalid." << std::endl;
        return false;
    }

    if (!IsAliasValid(flags.GetAlias())) {
        std::cerr << COMPONENT_NAME << " alias: " << flags.GetAlias() << " is invalid." << std::endl;
        return false;
    }
    return true;
}
}  // namespace

int main(int argc, char **argv)
{
    g_isCentOs = IsCentos();

    // 1.parse flags
    function_agent::FunctionAgentFlags flags;
    if (auto parse = flags.ParseFlags(argc, argv, true); parse.IsSome()) {
        std::cerr << "<function_agent> parse flag error: " << parse.Get() << std::endl << flags.Usage() << std::endl;
        return EXIT_COMMAND_MISUSE;
    }
    if (!CheckFlags(flags)) {
        return EXIT_COMMAND_MISUSE;
    }
    runtime_manager::Flags runtimeManagerFlags;
    if (flags.GetEnableMergeProcess()) {
        if (auto parse = runtimeManagerFlags.ParseFlags(argc, argv, true); parse.IsSome()) {
            std::cerr << "<runtime_manager> parse flag error, flags: " << parse.Get() << std::endl
                      << runtimeManagerFlags.Usage() << std::endl;
            return EXIT_COMMAND_MISUSE;
        }
    }

    g_functionAgentSwitcher = std::make_shared<functionsystem::ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!g_functionAgentSwitcher->InitLogger(flags)) {
        return EXIT_ABNORMAL;
    }
    // 3.register signal
    if (!g_functionAgentSwitcher->RegisterHandler(OnStopHandler, g_stopSignal)) {
        return EXIT_ABNORMAL;
    }

    // 4.startup function agent
    auto address = flags.GetIP() + ":" + flags.GetAgentListenPort();
    if (!InitSSL(flags)) {
        YRLOG_ERROR("failed to get sslConfig");
        g_functionAgentSwitcher->SetStop();
        return EXIT_ABNORMAL;
    }
    if (!InitLitebusAKSKEnv(flags).IsOk()) {
        YRLOG_ERROR("failed to get aksk config");
        g_functionAgentSwitcher->SetStop();
        return EXIT_ABNORMAL;
    }

    if (!g_functionAgentSwitcher->InitLiteBus(address, flags.GetLitebusThreadNum())) {
        g_functionAgentSwitcher->SetStop();
    } else {
        if (flags.GetEnableMergeProcess()) {
            OnCreateRuntimeManager(runtimeManagerFlags);
        }
        OnCreateFunctionAgent(flags);
    }
    g_functionAgentSwitcher->WaitStop();

    OnDestroy();

    return 0;
}
