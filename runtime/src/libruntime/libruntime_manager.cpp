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

#include "src/libruntime/libruntime_manager.h"

#include "auto_init.h"
#include "src/dto/config.h"
#include "src/libruntime/utils/constants.h"
#include "src/utility/logger/log_handler.h"
#include "src/utility/timer_worker.h"
namespace YR {
namespace Libruntime {
using YR::utility::CloseGlobalTimer;
using YR::utility::DEFAULT_JOB_ID;
using YR::utility::InitGlobalTimer;
using YR::utility::InitLog;
using YR::utility::InstallFailureSignalHandler;
using YR::utility::LOGGER_NAME;
using YR::utility::LogParam;
using YR::utility::LogRollingCompress;
using YR::utility::SetGetLoggerNameFunc;
std::pair<uint32_t, ErrorInfo> GetValidMaxLogFileNum(uint32_t maxLogFileNum)
{
    ErrorInfo err;
    if (maxLogFileNum > 0) {  // higher priority than env
        return std::make_pair(maxLogFileNum, err);
    }
    maxLogFileNum = YR::Libruntime::Config::Instance().YR_MAX_LOG_FILE_NUM();
    if (maxLogFileNum == 0) {
        return std::make_pair(
            maxLogFileNum, ErrorInfo(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                                     "maxLogFileNum should be positive"));
    }
    return std::make_pair(maxLogFileNum, err);
}

std::pair<uint32_t, ErrorInfo> GetValidMaxLogSizeMb(uint32_t maxLogSizeMb)
{
    ErrorInfo err;
    if (maxLogSizeMb > 0) {  // higher priority than env
        return std::make_pair(maxLogSizeMb, err);
    }
    maxLogSizeMb = YR::Libruntime::Config::Instance().YR_MAX_LOG_SIZE_MB();
    if (maxLogSizeMb == 0) {
        return std::make_pair(
            maxLogSizeMb, ErrorInfo(YR::Libruntime::ErrorCode::ERR_PARAM_INVALID, YR::Libruntime::ModuleCode::RUNTIME,
                                    "maxLogSizeMb should be positive"));
    }
    return std::make_pair(maxLogSizeMb, err);
}

static void SetClusterAccessInfo(std::shared_ptr<LibruntimeConfig> librtConfig)
{
    if (librtConfig->functionMasters.empty()) {
        if (ClusterAccessInfo::isMasterCluster) {
            librtConfig->functionMasters = ClusterAccessInfo::masterAddrList;
        } else {
            librtConfig->functionMasters.push_back(ClusterAccessInfo::masterAddr);
        }
    }
}

ErrorInfo LibruntimeManager::HandleInitialized(const LibruntimeConfig &config, const std::string &rtCtx)
{
    std::shared_ptr<LibruntimeConfig> librtConfig;
    {
        std::lock_guard<std::mutex> rtCfgLK(rtCfgMtx);
        auto iter = librtConfigs.find(rtCtx);
        if (iter != librtConfigs.end()) {
            librtConfig = iter->second;
        } else {
            librtConfigs[rtCtx] = std::make_shared<LibruntimeConfig>(config);
            return ErrorInfo();
        }
    }
    YRLOG_INFO("merge config, selfLanguage: {} {}", config.selfLanguage, librtConfig->selfLanguage);
    for (auto it = config.functionIds.begin(); it != config.functionIds.end(); it++) {
        YRLOG_INFO("merge config, functionId {} : {}", it->first, it->second);
    }
    for (auto it = librtConfig->functionIds.begin(); it != librtConfig->functionIds.end(); it++) {
        YRLOG_INFO("merge config, functionId {} : {}", it->first, it->second);
    }
    return librtConfig->MergeConfig(config);
}

LibruntimeManager::LibruntimeManager()
{
    clientsMgr = std::make_shared<ClientsManager>();
    metricsAdaptor = std::make_shared<MetricsAdaptor>();
    socketClient_ = std::make_shared<DomainSocketClient>(std::string(DEFAULT_SOCKET_PATH));
    logManager_ = std::make_shared<LogManager>();
}

ErrorInfo LibruntimeManager::Init(const LibruntimeConfig &config, const std::string &rtCtx)
{
    auto err = config.Check();
    if (!err.OK()) {
        YRLOG_ERROR("config check failed, job id is {}, err code is {}, err msg is {}", config.jobId, err.Code(),
                    err.Msg());
        return err;
    }

    if (config.attach) {
        YRLOG_INFO("should attach to an initialized instance.");
        if (!IsInitialized(rtCtx)) {
            return ErrorInfo(YR::Libruntime::ERR_INCORRECT_INIT_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                             "there is no initialized instance to attach");
        }
        return ErrorInfo();
    }

    if (IsInitialized(rtCtx)) {
        return HandleInitialized(config, rtCtx);
    }

    {
        std::lock_guard<std::mutex> rtLk(rtMtx);
        if (librts.empty()) {
            InitGlobalTimer();
        }
    }

    LogParam logParam;
    if (!config.logLevel.empty()) {
        logParam.logLevel = config.logLevel;
    } else {
        logParam.logLevel = Config::Instance().YR_LOG_LEVEL();
    }
    logParam.logDir = config.logDir;
    logParam.logBufSecs = config.logFlushInterval;
    auto result = GetValidMaxLogSizeMb(config.logFileSizeMax);
    if (!result.second.OK()) {
        YRLOG_ERROR("invalid log file size max: {}, err code is {}, err msg is {}", result.first, result.second.Code(),
                    result.second.Msg());
        return result.second;
    }
    logParam.maxSize = result.first;
    result = GetValidMaxLogFileNum(config.logFileNumMax);
    if (!result.second.OK()) {
        YRLOG_ERROR("invalid log file num: {}, err code is {}, err msg is {}", result.first, result.second.Code(),
                    result.second.Msg());
        return result.second;
    }
    logParam.maxFiles = result.first;
    logParam.nodeName = config.jobId;
    logParam.modelName = config.runtimeId;
    logParam.isLogMerge = config.isLogMerge;
    this->isLogMerge = config.isLogMerge;
    InitLog(logParam);
    logManager_->AddLogParam(rtCtx, logParam);
    logManager_->StartRollingCompress(LogRollingCompress);
    runtimeContext = std::make_shared<RuntimeContext>(config.jobId);
    auto getLoggerNameFunc = [this]() -> std::string {
        if (this->isLogMerge) {
            auto jobId = runtimeContext->GetJobIdThreadlocal();
            if (jobId == DEFAULT_JOB_ID) {
                return LOGGER_NAME;
            }
            return jobId;
        }
        return LOGGER_NAME;
    };
    SetGetLoggerNameFunc(getLoggerNameFunc);
    YRLOG_INFO("Job ID: {}, runtime ID: {}, log dir: {}, log level is {}, is Driver {}", config.jobId, config.runtimeId,
               config.logDir, config.logLevel, config.isDriver);

    if (config.enableSigaction) {
        InstallSigtermHandler();
    }

    // Not install in java, because sigsegv in java is for internal use.
    if (config.selfLanguage != libruntime::LanguageType::Java &&
        config.selfLanguage != libruntime::LanguageType::Golang) {
        InstallFailureSignalHandler("libruntime");  // must be called after logger initialization
    }

    auto librtConfig = std::make_shared<LibruntimeConfig>(config);
    librtConfig->rtCtx = rtCtx;
    {
        std::lock_guard<std::mutex> rtCfgLK(rtCfgMtx);
        librtConfigs[rtCtx] = librtConfig;
    }
    std::lock_guard<std::mutex> rtLk(rtMtx);
    if (librts.find(rtCtx) != librts.end() && librts[rtCtx] != nullptr) {
        YRLOG_INFO("libruntime has already initialized, job ID: {}", config.jobId);
        return ErrorInfo(YR::Libruntime::ERR_INCORRECT_INIT_USAGE, YR::Libruntime::ModuleCode::RUNTIME,
                         "libruntime has already initialized.");
    }

    std::shared_ptr<Libruntime> librt;
    auto initErr = this->CreateLibruntime(librtConfig, librt);
    if (initErr.OK()) {
        YRLOG_INFO("succeed to init libruntime, job ID: {}", config.jobId);
        librts[rtCtx] = librt;
    } else {
        YRLOG_ERROR("failed to init libruntime, job Id: {}, code: {}, msg: {}", config.jobId, initErr.Code(),
                    initErr.Msg());
        std::lock_guard<std::mutex> rtCfgLK(rtCfgMtx);
        librtConfigs.erase(rtCtx);
    }
    if (librts.empty()) {
        YRLOG_WARN("No libruntime in memory, close all gloabl timer");
        CloseGlobalTimer();
        logManager_->StopRollingCompress();
    }
    return initErr;
}

ErrorInfo LibruntimeManager::CreateLibruntime(std::shared_ptr<LibruntimeConfig> librtConfig,
                                              std::shared_ptr<Libruntime> &librt)
{
    SetClusterAccessInfo(librtConfig);
    if (librtConfig->ns.empty()) {
        librtConfig->ns = DEFAULT_YR_NAMESPACE;
    }
    auto security = std::make_shared<Security>();
    if (librtConfig->inCluster) {
        auto err = !librtConfig->isDriver ? security->Init() : security->InitWithDriver(librtConfig);
        if (!err.OK()) {
            YRLOG_ERROR("init security failed, is driver: {}, code is {}, msg is {}", librtConfig->isDriver, err.Code(),
                        err.Msg());
            return err;
        }
    }
    auto [enableDsAuth, encryptEnable] = security->GetDataSystemConfig(
        librtConfig->runtimePublicKey, librtConfig->runtimePrivateKey, librtConfig->dsPublicKey);
    librtConfig->enableAuth = enableDsAuth;
    librtConfig->encryptEnable = encryptEnable;
    auto finalizeHandler = [this, rtCtx(librtConfig->rtCtx)]() { this->Finalize(rtCtx); };
    librt = std::make_shared<Libruntime>(librtConfig, clientsMgr, metricsAdaptor, security, socketClient_);
    if (librtConfig->inCluster) {
        auto [datasystemClients, err] =
            clientsMgr->GetOrNewDsClient(librtConfig, Config::Instance().DS_CONNECT_TIMEOUT_SEC());
        if (!err.OK()) {
            YRLOG_ERROR("get or new ds client failed, code is {}, msg is {}", err.Code(), err.Msg());
            return err;
        }
        auto fsClient = std::make_shared<FSClient>();
        return librt->Init(fsClient, datasystemClients, finalizeHandler);
    } else {
        return ErrorInfo();
    }
}

void LibruntimeManager::Finalize(const std::string &rtCtx)
{
    std::shared_ptr<LibruntimeConfig> librtConfig = nullptr;
    {
        std::lock_guard<std::mutex> rtCfgLK(rtCfgMtx);
        auto it = librtConfigs.find(rtCtx);
        if (it != librtConfigs.end()) {
            librtConfig = it->second;
        }
    }
    if (!LibruntimeManager::IsInitialized(rtCtx) || librtConfig == nullptr) {
        YRLOG_ERROR("Not initialized, do nothing about it.");
        return;
    }

    std::shared_ptr<Libruntime> librt;
    {
        std::lock_guard<std::mutex> rtLK(rtMtx);
        if (librts.find(rtCtx) == librts.end()) {
            YRLOG_WARN("There is no lib runtime found in memory, threadID: {}.", rtCtx);
            return;
        }
        librt = librts[rtCtx];
        librts.erase(rtCtx);
    }
    {
        std::lock_guard<std::mutex> rtCfgLK(rtCfgMtx);
        librtConfigs.erase(rtCtx);
    }
    librt->Finalize(librtConfig->isDriver);
    librt.reset();
    librtConfig.reset();
    {
        std::lock_guard<std::mutex> rtLK(rtMtx);
        if (librts.empty()) {
            CloseGlobalTimer();
            logManager_->StopRollingCompress();
        }
    }
    YRLOG_INFO("finish to finalize libruntime with context: {}", rtCtx);
}

std::shared_ptr<Libruntime> LibruntimeManager::GetLibRuntime(const std::string &rtCtx)
{
    std::lock_guard<std::mutex> rtLk(rtMtx);
    std::shared_ptr<Libruntime> librt = nullptr;
    if (librts.find(rtCtx) != librts.end()) {
        librt = librts[rtCtx];
    }
    return librt;
}

void LibruntimeManager::SetLibRuntime(std::shared_ptr<Libruntime> libruntime, const std::string &rtCtx)
{
    std::lock_guard<std::mutex> rtLk(rtMtx);
    librts[rtCtx] = libruntime;
}

void LibruntimeManager::SetRuntimeContext(const std::string &jobId)
{
    runtimeContext->SetJobIdThreadlocal(jobId);
}

bool LibruntimeManager::IsInitialized(const std::string &rtCtx)
{
    std::lock_guard<std::mutex> rtLk(rtMtx);
    auto iter = librts.find(rtCtx);
    if (iter == librts.end() || iter->second == nullptr) {
        return false;
    }
    return true;
}

void LibruntimeManager::ReceiveRequestLoop(const std::string &rtCtx)
{
    return GetLibRuntime(rtCtx)->ReceiveRequestLoop();
}

void LibruntimeManager::InstallSigtermHandler()
{
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = SigtermHandler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        YRLOG_ERROR("Failed Install SIGTERM handler");
        return;
    }
    YRLOG_INFO("Succeeded to Install SIGTERM handler");
    return;
}

void LibruntimeManager::SigtermHandler(int signum, siginfo_t *info, void *ptr)
{
    YRLOG_DEBUG("Received signal {}, start to call SigtermHandler", signum);
    LibruntimeManager::Instance().ExecShutdownCallbackAsync(signum);
}

void LibruntimeManager::ExecShutdownCallbackAsync(int signum)
{
    std::call_once(flag_, [this, signum]() {
        auto t = std::thread([this, signum]() { ExecShutdownCallback(signum); });
        t.detach();
    });
}

void LibruntimeManager::ExecShutdownCallback(int signum, bool needExit)
{
    uint64_t gracePeriodSec = Config::Instance().GRACEFUL_SHUTDOWN_TIME();
    YRLOG_DEBUG("Start to execute SigtermHandler, graceful shutdown time: {}", gracePeriodSec);
    std::unordered_map<std::string, std::shared_ptr<Libruntime>> librtsCopied;
    {
        /* The user code executed in the 'ExecShutdownCallback' may invoke the Libruntime
        interface to invoke the 'GetLibRuntime'. As a result, a deadlock of rtMtx occurs.
        Therefore, it is required to copy 'librts' to release the lock in advance.
        */
        std::lock_guard<std::mutex> rtLk(rtMtx);
        for (auto &pair : librts) {
            librtsCopied.emplace(pair.first, pair.second);
        }
    }

    for (auto iter = librtsCopied.begin(); iter != librtsCopied.end(); iter++) {
        auto errInfo = librtsCopied[iter->first]->ExecShutdownCallback(gracePeriodSec);
        if (!errInfo.OK()) {
            YRLOG_ERROR("Failed to call ExecShutdownCallback for libruntime with context: {}, error: {}", iter->first,
                        errInfo.Msg());
            continue;
        }
        YRLOG_DEBUG("Succeeded to call ExecShutdownCallback for libruntime with context: {}", iter->first);
    }
    YRLOG_DEBUG("End to call SigtermHandler, signum: {}", signum);
    if (needExit) {
        exit(signum);
    }
}
}  // namespace Libruntime
}  // namespace YR
