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

#include <iostream>
#include <memory>
#include <string>
#include <sys/capability.h>
#include <linux/capability.h>

#include "common/constants/constants.h"
#include "common/logs/logging.h"
#include "common/utils/module_switcher.h"
#include "common/utils/param_check.h"
#include "common/utils/version.h"
#include "driver/runtime_manager_driver.h"
#include "runtime_manager/config/flags.h"
#include "async/future.hpp"
#include "async/option.hpp"
#include "common/status/status.h"
#include "utils/os_utils.hpp"

using namespace functionsystem;
using namespace functionsystem::runtime_manager;

namespace {
const std::string COMPONENT_NAME = COMPONENT_NAME_RUNTIME_MANAGER;

std::shared_ptr<litebus::Promise<bool>> g_stopSignal{ nullptr };
std::shared_ptr<functionsystem::ModuleSwitcher> g_runtimeManagerSwitcher{ nullptr };
std::shared_ptr<RuntimeManagerDriver> g_runtimeManagerDriver{ nullptr };

bool CheckFlags(const Flags &flags)
{
    if (!IsNodeIDValid(flags.GetNodeID())) {
        std::cerr << COMPONENT_NAME << " node id: " << flags.GetNodeID() << " is invalid." << std::endl;
        return false;
    }

    if (!IsIPValid(flags.GetIP())) {
        std::cerr << COMPONENT_NAME << " ip: " << flags.GetIP() << " is invalid." << std::endl;
        return false;
    }

    if (!IsPortValid(flags.GetPort())) {
        std::cerr << COMPONENT_NAME << " port: " << flags.GetPort() << " is invalid." << std::endl;
        return false;
    }

    if (!IsAddressValid(flags.GetAgentAddress())) {
        std::cerr << COMPONENT_NAME << " agent address: " << flags.GetAgentAddress() << "is invalid." << std::endl;
        return false;
    }

    return true;
}

void OnStopHandler(int signum)
{
    // register SIGINT, SIGTERM
    YRLOG_INFO("receive signal: {}", signum);
    g_stopSignal->SetValue(true);
}

void SetCapabilities(const cap_value_t *capList, int ncap)
{
    cap_t caps = cap_get_proc();
    YRLOG_INFO("Set capabilities flag result: ", cap_set_flag(caps, CAP_INHERITABLE, ncap, capList, CAP_SET));
    YRLOG_INFO("Set capabilities proc result: ", cap_set_proc(caps));
    cap_free(caps);
}

void OnCreate(const Flags &flags)
{
    YRLOG_INFO("{} is starting", COMPONENT_NAME);
    YRLOG_INFO("version:{} branch:{} commit_id:{}", BUILD_VERSION, GIT_BRANCH_NAME, GIT_HASH);
    cap_value_t caps[1] = { CAP_DAC_OVERRIDE };
    SetCapabilities(caps, 1);

    // 4.init litebus
    auto address = litebus::os::Join(flags.GetIP(), flags.GetPort(), ':');
    if (!g_runtimeManagerSwitcher->InitLiteBus(address, flags.GetLitebusThreadNum())) {
        g_runtimeManagerSwitcher->SetStop();
        return;
    }

    g_runtimeManagerDriver = std::make_shared<RuntimeManagerDriver>(flags, COMPONENT_NAME);
    if (auto status = g_runtimeManagerDriver->Start(); status.IsError()) {
        YRLOG_ERROR("failed to start {}, errMsg: {}", COMPONENT_NAME, status.ToString());
        g_runtimeManagerSwitcher->SetStop();
        return;
    }

    YRLOG_INFO("{} is started", COMPONENT_NAME);
}

void OnDestroy()
{
    if (g_runtimeManagerDriver != nullptr && g_runtimeManagerDriver->Stop().IsOk()) {
        g_runtimeManagerDriver->Await();
        g_runtimeManagerDriver = nullptr;
        YRLOG_INFO("success to stop {}", COMPONENT_NAME);
    } else {
        YRLOG_WARN("failed to stop {}", COMPONENT_NAME);
    }
    g_runtimeManagerSwitcher->CleanMetrics();
    g_runtimeManagerSwitcher->FinalizeLiteBus();
    g_runtimeManagerSwitcher->StopLogger();
}

}  // namespace

int main(int argc, char **argv)
{
    // 1.parse flags
    Flags flags;
    litebus::Option<std::string> parse = flags.ParseFlags(argc, argv);
    if (parse.IsSome()) {
        std::cerr << COMPONENT_NAME << " parse flag error, flags: " << parse.Get() << std::endl
                  << flags.Usage() << std::endl;
        return EXIT_COMMAND_MISUSE;
    }

    if (!CheckFlags(flags)) {
        return EXIT_COMMAND_MISUSE;
    }

    // 2.init log
    g_runtimeManagerSwitcher = std::make_shared<functionsystem::ModuleSwitcher>(COMPONENT_NAME, flags.GetNodeID());
    if (!g_runtimeManagerSwitcher->InitLogger(flags)) {
        return EXIT_ABNORMAL;
    }
    g_runtimeManagerSwitcher->InitMetrics(flags.GetEnableMetrics(), flags.GetMetricsConfig(),
                                          flags.GetMetricsConfigFile());
    // 3.register signal
    if (!g_runtimeManagerSwitcher->RegisterHandler(OnStopHandler, g_stopSignal)) {
        return EXIT_ABNORMAL;
    }

    OnCreate(flags);

    // 5. wait stop
    g_runtimeManagerSwitcher->WaitStop();

    // 6. stop
    OnDestroy();

    return 0;
}