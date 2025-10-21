/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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

#include "yr/yr.h"

#include "cluster_mode_runtime.h"
#include "api/cpp/src/code_manager.h"
#include "api/cpp/src/config_manager.h"
#include "api/cpp/src/internal.h"
#include "src/libruntime/err_type.h"
#include "src/libruntime/libruntime_manager.h"
#include "yr/api/runtime.h"
#include "yr/api/runtime_manager.h"
thread_local std::unordered_set<std::string> localNestedObjList;

namespace YR {

static bool g_isInit = false;

bool IsInitialized()
{
    return g_isInit;
}

void CheckInitialized()
{
    if (!IsInitialized()) {
        throw YR::Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_INIT_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                            "the current yr init status is abnormal, please init YR first");
    }
}

void SetInitialized(bool flag)
{
    g_isInit = flag;
}

void ReentrantFinalize()
{
    if (!IsInitialized()) {
        return;
    }
    YR::internal::RuntimeManager::GetInstance().Stop();
}

struct Defer {
    explicit Defer(std::function<void()> &&f)
    {
        deferFunc = std::move(f);
    }
    ~Defer()
    {
        deferFunc();
    }
    std::function<void()> deferFunc = nullptr;
};

void AtExitHandler()
{
    ReentrantFinalize();
}

ClientInfo Init(const Config &conf, int argc, char **argv)
{
    static bool hasRegisteredAtExit = false;
    Defer registerAtExitHandler([]() {
        if (!hasRegisteredAtExit) {
            atexit(AtExitHandler);
            hasRegisteredAtExit = true;
        }
    });
    std::string serverVersion = "";
    if (!IsInitialized()) {
        ConfigManager::Singleton().Init(conf, argc, argv);
        YR::internal::RuntimeManager::GetInstance().Initialize(conf.mode);
        if (!ConfigManager::Singleton().isDriver) {
            auto err = internal::CodeManager::LoadFunctions(ConfigManager::Singleton().loadPaths);
            if (!err.OK()) {
                YRLOG_INFO("load function error: Code:{}, MCode:{}, Msg:{}", err.Code(), err.MCode(), err.Msg());
            }
        }
        SetInitialized();
    }
    auto clientInfo = ConfigManager::Singleton().GetClientInfo();
    if (conf.mode == Config::Mode::CLUSTER_MODE) {
        clientInfo.serverVersion = YR::internal::GetRuntime()->GetServerVersion();
    }
    YRLOG_INFO("Current SDK Version: {}, Server Version: {}", clientInfo.version, clientInfo.serverVersion);
    return clientInfo;
}

ClientInfo Init(const Config &conf)
{
    return Init(conf, 0, nullptr);
}

ClientInfo Init(int argc, char **argv)
{
    Config conf;
    return Init(conf, argc, argv);
}

void Finalize(void)
{
    CheckInitialized();
    if (ConfigManager::Singleton().inCluster && !ConfigManager::Singleton().isDriver) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "Finalize is not allowed to use on cloud");
    }
    ReentrantFinalize();
    SetInitialized(false);
}

void Exit(void)
{
    CheckInitialized();
    bool isRemoteRuntime = ConfigManager::Singleton().inCluster && !ConfigManager::Singleton().isDriver;
    if (!isRemoteRuntime) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "Not support exit out of cluster");
    } else if (!YR::internal::IsLocalMode()) {
        YR::internal::GetRuntime()->Exit();
    }
}

void ReceiveRequestLoop(void)
{
    YR::Libruntime::LibruntimeManager::Instance().GetLibRuntime()->ReceiveRequestLoop();
}

bool IsOnCloud()
{
    return !ConfigManager::Singleton().isDriver;
}

bool IsLocalMode()
{
    if (!g_isInit) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_INIT_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "Please init YR first");
    }
    return ConfigManager::Singleton().IsLocalMode();
}

void SaveState(const int &timeout)
{
    CheckInitialized();
    bool isRemoteRuntime = ConfigManager::Singleton().inCluster && !ConfigManager::Singleton().isDriver;
    if (!isRemoteRuntime) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "SaveState is only supported on cloud with posix api");
    }
    if (YR::internal::IsLocalMode()) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "SaveState is not supported in local mode");
    }
    YR::internal::GetRuntime()->SaveState(timeout);
}

void LoadState(const int &timeout)
{
    CheckInitialized();
    bool isRemoteRuntime = ConfigManager::Singleton().inCluster && !ConfigManager::Singleton().isDriver;
    if (!isRemoteRuntime) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "LoadState is only supported on cloud with posix api");
    }

    if (YR::internal::IsLocalMode()) {
        throw Exception(YR::Libruntime::ErrorCode::ERR_INCORRECT_FUNCTION_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                        "LoadState is not supported in local mode");
    }
    YR::internal::GetRuntime()->LoadState(timeout);
}

}  // namespace YR
